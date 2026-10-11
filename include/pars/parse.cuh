// pars/parse.cuh — the CUDA-accelerated user-facing API.
//
//   auto r = pars<json_grammar>(script_buffer);        // host boundary
//
// `pars<Grammar>` compiles the grammar to a scan plan and launches the
// meta-assembled CUDA pipeline for it.  `parser<Grammar>` is the same engine
// with the device-level entry points (zero-copy `scan_device`, `scan_chunk`)
// that the tiered benchmarks use.
#pragma once

#include "pars/pars.hpp"
#include "pars/backend/cuda/meta/scan.cuh"

#include <cstdint>
#include <cstring>
#include <string_view>
#include <utility>
#include <vector>

namespace pars {

// ---------------------------------------------------------------------------
// Result of a stage-1 parse: the structural index (byte offsets of structural
// symbols) plus, optionally, the matching-bracket table.  This is exactly what
// the scan algebra produces; language semantics live in the caller's visitor.
// ---------------------------------------------------------------------------
struct parse_result {
	std::vector<std::int32_t> structural;	 // byte offsets
	std::vector<std::int32_t> pair;			 // partner index into `structural`, or -1
	bool					  balanced	  = true;
	bool					  well_formed = true;
	int						  max_depth	  = 0;

	[[nodiscard]] std::size_t tokens() const noexcept { return structural.size(); }

	// Replay the structural index into a user visitor (see model/visit.hpp).
	template<class Visitor>
	void visit(const std::uint8_t* base, std::size_t n, Visitor&& v) const {
		model::structural_view sv {
			structural.data(),
			pair.empty() ? nullptr : pair.data(),
			structural.size()
		};
		model::replay(base, n, sv, std::forward<Visitor>(v));
	}
	template<class Visitor>
	void visit(std::string_view s, Visitor&& v) const {
		visit(reinterpret_cast<const std::uint8_t*>(s.data()), s.size(), std::forward<Visitor>(v));
	}
};

struct parse_options {
	bool want_pairs = false;
};

namespace detail {

// Per-plan, per-thread host-side buffers: persistent device cache + pinned/dev
// input staging, reused across calls so repeated parses are not malloc-bound.
template<scan_plan S>
struct parse_buffers {
	gpu::detail::DeviceCache cache;
	std::uint8_t*			 pinned = nullptr;
	std::uint8_t*			 dev	= nullptr;
	std::size_t				 cap	= 0;

	~parse_buffers() {
		if (pinned)
			cudaFreeHost(pinned);
		if (dev)
			cudaFree(dev);
	}

	void ensure(std::size_t n) {
		if (n <= cap)
			return;
		if (pinned)
			cudaFreeHost(pinned);
		if (dev)
			cudaFree(dev);
		gpu::detail::check(cudaHostAlloc(&pinned, n, cudaHostAllocDefault), "pars pinned");
		gpu::detail::check(cudaMalloc(&dev, n), "pars dev");
		cap = n;
	}
};

template<scan_plan S>
[[nodiscard]] inline parse_buffers<S>& buffers() {
	static thread_local parse_buffers<S> b;
	return b;
}

}  // namespace detail

// ---------------------------------------------------------------------------
// The generated parser for one grammar.
// ---------------------------------------------------------------------------
template<class Grammar>
struct parser {
	static constexpr scan_plan kPlan = compile::make_scan_plan<Grammar>();

	// Host boundary: H2D + assembled kernels + D2H of the structural index.
	[[nodiscard]] parse_result operator()(
		std::string_view input, parse_options opt = {}
	) const {
		parse_result r;
		if (input.empty())
			return r;
		auto&			  b		 = detail::buffers<kPlan>();
		const std::size_t padded = (input.size() + 31) / 32 * 32;
		b.ensure(padded);
		std::memcpy(b.pinned, input.data(), input.size());
		if (padded > input.size())
			std::memset(b.pinned + input.size(), 0, padded - input.size());
		cudaMemcpyAsync(b.dev, b.pinned, padded, cudaMemcpyHostToDevice, 0);

		gpu::device_result dr =
			gpu::meta::scan_device_meta<kPlan>(b.dev, input.size(), b.cache, opt.want_pairs, true, nullptr, 0);
		r.balanced	 = dr.balanced;
		r.well_formed = dr.well_formed;
		r.max_depth	 = dr.max_depth;
		r.structural.resize(dr.n_struct);
		if (dr.n_struct != 0)
			cudaMemcpy(
				r.structural.data(),
				dr.structural,
				std::size_t(dr.n_struct) * 4,
				cudaMemcpyDeviceToHost
			);
		if (opt.want_pairs && dr.pair_pos && dr.n_struct != 0) {
			r.pair.resize(dr.n_struct);
			cudaMemcpy(
				r.pair.data(),
				dr.pair_pos,
				std::size_t(dr.n_struct) * 4,
				cudaMemcpyDeviceToHost
			);
		}
		return r;
	}

	// Device-resident, zero-copy.  `d_data` must be readable for
	// ceil(n/32)*32 bytes (caller pads).  Results stay on the device.
	[[nodiscard]] gpu::device_result scan_device(
		const std::uint8_t* d_data, std::size_t n, cudaStream_t s = 0, bool want_pairs = false,
		bool run_pairs = true, gpu::stage_times* st = nullptr
	) const {
		return gpu::meta::scan_device_meta<kPlan>(
			d_data,
			n,
			detail::buffers<kPlan>().cache,
			want_pairs,
			run_pairs,
			st,
			s
		);
	}

	// Chunked stage-1 (parity path), for the chunked/overlapped tier.
	[[nodiscard]] gpu::chunk_carry scan_chunk(
		const std::uint8_t* d_chunk, std::size_t n, gpu::chunk_carry in, cudaStream_t s = 0,
		std::size_t byte_base = 0
	) const {
		// Must share this parser's cache with reserve()/struct_ptr(), otherwise
		// the compaction output lands in a different buffer.
		return gpu::scan_chunk_impl<kPlan>(
			d_chunk,
			n,
			in,
			detail::buffers<kPlan>().cache,
			s,
			byte_base
		);
	}

	void reserve(std::size_t cap) const { detail::buffers<kPlan>().cache.ensure_struct(cap); }

	[[nodiscard]] const std::int32_t* struct_ptr() const {
		return detail::buffers<kPlan>().cache.struct_idx;
	}
};

// ---------------------------------------------------------------------------
// Sugar: `pars<json_grammar>(buffer)`.
// ---------------------------------------------------------------------------
template<class Grammar>
[[nodiscard]] inline parse_result pars(std::string_view input, parse_options opt = {}) {
	return parser<Grammar> {}(input, opt);
}

}  // namespace pars
