// pars/backend/cuda/meta/scan.cuh — Layer 3 entry point.
//
// `scan_device_meta<S>` / `scan_host_meta<S>` run the *rewritten* pipeline.  The
// fused parity head is assembled from the specialized kernels in emit.cuh; all
// other pipelines delegate to the existing runtime-descriptor implementation so
// correctness is preserved while the specialization is brought online.
#pragma once

#include "pars/backend/cuda/meta/emit.cuh"
#include "pars/backend/cuda/pipeline.cuh"

#include <cstdint>
#include <cstring>
#include <vector>

namespace pars::gpu::meta {

// P3: compaction (R5).  Shared by every emitted pipeline; identical to the
// reference backend but written as a reusable stage.
template<scan_plan S>
inline void compact_p3(
	detail::DeviceCache& b, const std::uint8_t* d_in, int n_words, int grid, cudaStream_t s,
	std::uint32_t& n_struct, std::uint32_t& n_oc
) {
	using namespace detail;
	popcount_kernel<<<grid, BLOCK, 0, s>>>(b.struct_out, n_words, b.counts);
	thrust::exclusive_scan(thrust::cuda::par.on(s), b.counts, b.counts + n_words, b.struct_excl, 0u);
	popcount_kernel<<<grid, BLOCK, 0, s>>>(b.openclose_out, n_words, b.counts);
	thrust::exclusive_scan(thrust::cuda::par.on(s), b.counts, b.counts + n_words, b.oc_excl, 0u);
	pack_counts_kernel<<<1, 1, 0, s>>>(
		b.struct_excl,
		b.oc_excl,
		b.struct_out,
		b.openclose_out,
		n_words,
		b.excl
	);
	std::uint32_t packed[2] = {0, 0};
	cudaMemcpyAsync(packed, b.excl, 8, cudaMemcpyDeviceToHost, s);
	cudaStreamSynchronize(s);
	check(cudaGetLastError(), "meta pack counts");
	n_struct = packed[0];
	n_oc	 = packed[1];
	b.ensure_struct(n_struct);
	b.ensure_oc(n_oc);
	scatter_kernel<<<grid, BLOCK, 0, s>>>(
		d_in,
		b.struct_out,
		b.struct_excl,
		b.openclose_out,
		b.oc_excl,
		n_words,
		b.struct_idx,
		b.oc_struct_pos,
		b.oc_char
	);
	check(cudaGetLastError(), "meta scatter");
}

// Zero-copy meta scan.  Specialized for the parity pipeline (fusion already
// happened in the rewrite), otherwise delegates to the reference backend.
template<scan_plan S>
inline device_result scan_device_meta(
	const std::uint8_t* d_in, std::size_t size, detail::DeviceCache& b, bool want_pairs,
	bool run_pairs = true, stage_times* st = nullptr, cudaStream_t s = 0
) {
	using P = pars::meta::rewritten_pipeline_t<S>;
	if constexpr (is_parity_pipeline<P, S>::value && S.n_brackets == 0) {
		using namespace detail;
		device_result out;
		if (size == 0)
			return out;
		const std::size_t padded  = (size + 31) / 32 * 32;
		const int		  n_words = (int)(padded / 32);
		const int		  grid	  = (n_words + BLOCK - 1) / BLOCK;
		b.ensure_words(n_words, padded);

		cudaEvent_t ev[3] {};
		if (st)
			for (int i = 0; i < 3; ++i) cudaEventCreate(&ev[i]);
		auto mark = [&](int i) {
			if (st)
				cudaEventRecord(ev[i], s);
		};
		mark(0);
		emit_head<scan_plan::lex_kind::doubled, S>::run(b, d_in, size, n_words, grid, s);
		mark(1);

		std::uint32_t n_struct = 0, n_oc = 0;
		compact_p3<S>(b, d_in, n_words, grid, s, n_struct, n_oc);
		mark(2);

		out.structural = b.struct_idx;
		out.pair_pos   = nullptr;
		out.n_struct   = n_struct;
		out.balanced = out.well_formed = true;
		out.max_depth				   = 0;

		if (st) {
			cudaEventSynchronize(ev[2]);
			float ms = 0;
			st->p1	 = 0;
			cudaEventElapsedTime(&ms, ev[0], ev[1]);
			st->p2 = ms;
			cudaEventElapsedTime(&ms, ev[1], ev[2]);
			st->p3 = ms;
			st->p4 = 0;
			for (int i = 0; i < 3; ++i) cudaEventDestroy(ev[i]);
		}
		return out;
	} else {
		return scan_device_impl<S>(d_in, size, b, want_pairs, run_pairs, st, s);
	}
}

// Host adapter for the meta scan (pinned H2D, D2H of the structural index).
template<scan_plan S>
inline host_result scan_host_meta(const std::uint8_t* h_data, std::size_t n, cudaStream_t s = 0) {
	detail::DeviceCache b;
	const std::size_t	 padded = (n + 31) / 32 * 32;
	std::uint8_t*		 pin	= nullptr;
	std::uint8_t*		 dev	= nullptr;
	detail::check(cudaHostAlloc(&pin, padded ? padded : 1, cudaHostAllocDefault), "meta pinned");
	detail::check(cudaMalloc(&dev, padded ? padded : 1), "meta dev_in");
	std::memcpy(pin, h_data, n);
	if (padded > n)
		std::memset(pin + n, 0, padded - n);
	cudaMemcpyAsync(dev, pin, padded, cudaMemcpyHostToDevice, s);
	device_result dr =
		scan_device_meta<S>(dev, n, b, false, true, nullptr, s);
	host_result out;
	out.balanced	= dr.balanced;
	out.well_formed = dr.well_formed;
	out.max_depth	= dr.max_depth;
	out.structural.resize(dr.n_struct);
	if (dr.n_struct)
		cudaMemcpyAsync(
			out.structural.data(),
			dr.structural,
			(std::size_t)dr.n_struct * 4,
			cudaMemcpyDeviceToHost,
			s
		);
	cudaStreamSynchronize(s);
	cudaFreeHost(pin);
	cudaFree(dev);
	return out;
}

}  // namespace pars::gpu::meta
