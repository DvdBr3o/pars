// pars/core/cuda.hpp — GPU backend for the unified IR.
//
// STAGING (S2): the fast kernels are re-expressed off `core::plan`.  The
// adapter `to_scan_plan` must reproduce the reference `scan_plan` bit-for-bit —
// that is asserted by the callers, so any drift in the new IR is a compile
// error.  The next cut fuses the kernels and drops the adapter.
#pragma once

#include "pars/backend/cuda/pipeline.cuh"
#include "pars/core/plan.hpp"
#include "pars/core/tree.hpp"

#include <thrust/reduce.h>

#include <cstdint>

namespace pars::core {

template<plan P>
[[nodiscard]] constexpr scan_plan to_scan_plan() noexcept {
	scan_plan S {};

	// finite cells -> descriptors (+ the mode view)
	for (std::uint8_t d = 0; d < P.n_cells; ++d) {
		const cell& c = P.cells[d];
		if (c.sch != schema::finite && c.sch != schema::choice)
			continue;
		if (S.n_descs >= kMaxModes)
			break;
		auto& dd = S.descs[S.n_descs];
		switch (c.factor_) {
			case factor::affine: dd.kind = scan_plan::lex_kind::affine; break;
			case factor::doubled: dd.kind = scan_plan::lex_kind::doubled; break;
			case factor::comment: dd.kind = scan_plan::lex_kind::comment; break;
			case factor::fallback: dd.kind = scan_plan::lex_kind::fallback; break;
			default: dd.kind = scan_plan::lex_kind::table; break;
		}
		dd.n_states	 = c.n_states;
		dd.n_classes = c.n_classes;
		dd.start	 = c.start;
		dd.delim	 = c.delim;
		dd.escape	 = c.escape;
		dd.ok		 = c.ok;
		dd.rep_of	 = c.rep_of;
		for (std::size_t s = 0; s < scan_plan::kMaxDfaStates; ++s)
			for (std::size_t k = 0; k < scan_plan::kMaxDfaClasses; ++k) {
				dd.trans[s][k]	= c.trans[s][k];
				dd.masked[s][k] = c.masked[s][k];
			}
		++S.n_descs;
		if (c.delim != 0 && S.n_modes < kMaxModes) {
			mode_spec m {c.delim, c.escape};
			bool	  ex = false;
			for (std::size_t i = 0; i < S.n_modes; ++i)
				if (S.modes[i] == m)
					ex = true;
			if (!ex)
				S.modes[S.n_modes++] = m;
		}
	}

	// stack cells -> bracket pairs
	for (std::uint8_t b = 0; b < P.n_brackets && S.n_brackets < kMaxBrackets; ++b)
		S.brackets[S.n_brackets++] = bracket_spec {P.opens[b], P.closes[b]};

	// atoms -> punctuation
	for (std::uint8_t a = 0; a < P.n_atoms && S.n_punctuation < kMaxPunct; ++a)
		S.punctuation[S.n_punctuation++] = P.atoms[a];

	return S;
}

template<plan P>
class scanner {
public:
	[[nodiscard]] gpu::device_result scan_device(
		const std::uint8_t* d, std::size_t n, cudaStream_t s = 0, bool want_pairs = false,
		bool run_pairs = true, gpu::stage_times* st = nullptr
	) const {
		return sc_.scan_device(d, n, s, want_pairs, run_pairs, st);
	}
	[[nodiscard]] gpu::host_result scan_host(
		const std::uint8_t* h, std::size_t n, cudaStream_t s = 0, bool want_pairs = false
	) const {
		return sc_.scan_host(h, n, s, want_pairs);
	}

private:
	gpu::scanner<to_scan_plan<P>()> sc_;
};

// ---------------------------------------------------------------------------
// Tree schema on the GPU: the same monoid, executed as a device reduction.
// ---------------------------------------------------------------------------
namespace detail {
constexpr int kTreeBlock = 256;

template<class Op>
__global__ void tree_partial_kernel(
	const std::uint8_t* data, std::size_t n, std::size_t chunk,
	algebra::reducer<std::int64_t, Op>* out
) {
	using R				  = algebra::reducer<std::int64_t, Op>;
	const std::size_t tid = static_cast<std::size_t>(blockIdx.x) * blockDim.x + threadIdx.x;
	const std::size_t lo0 = tid * chunk;
	if (lo0 >= n) {
		out[tid] = R::identity();
		return;
	}
	std::size_t lo = lo0;
	// don't split a number that started in the previous chunk
	if (lo > 0 && data[lo - 1] >= '0' && data[lo - 1] <= '9')
		while (lo < n && data[lo] >= '0' && data[lo] <= '9') ++lo;
	const std::size_t hi = (lo0 + chunk < n) ? lo0 + chunk : n;
	R				  acc = R::identity();
	std::size_t		  i	  = lo;
	while (i < hi) {
		if (data[i] >= '0' && data[i] <= '9') {
			std::int64_t v = 0;
			// bounded by n, not hi: finish a number that straddles the chunk end
			// (the next chunk skips its continuation).
			while (i < n && data[i] >= '0' && data[i] <= '9') {
				v = v * 10 + (data[i] - '0');
				++i;
			}
			acc = R::compose(acc, R {true, v});
		} else {
			++i;
		}
	}
	out[tid] = acc;
}

template<class Op>
struct CombineR {
	__host__ __device__ algebra::reducer<std::int64_t, Op>
	operator()(algebra::reducer<std::int64_t, Op> a, algebra::reducer<std::int64_t, Op> b) const {
		return algebra::reducer<std::int64_t, Op>::compose(a, b);
	}
};
}  // namespace detail

template<class Op>
[[nodiscard]] inline std::int64_t tree_reduce_gpu(
	const std::uint8_t* d, std::size_t n, int chunk_bytes = 1024, cudaStream_t s = 0
) {
	using R = algebra::reducer<std::int64_t, Op>;
	if (n == 0)
		return R::identity().v;
	const std::size_t csz = (chunk_bytes > 0) ? static_cast<std::size_t>(chunk_bytes) : 1024;
	const std::size_t chunk = csz;
	const int		  nch	= static_cast<int>((n + chunk - 1) / chunk);
	R*				  dpart = nullptr;
	cudaMalloc(&dpart, sizeof(R) * static_cast<std::size_t>(nch));
	detail::tree_partial_kernel<Op>
		<<<(nch + detail::kTreeBlock - 1) / detail::kTreeBlock, detail::kTreeBlock, 0, s>>>(
			d, n, chunk, dpart
		);
	R res = thrust::reduce(
		thrust::cuda::par.on(s), dpart, dpart + nch, R::identity(), detail::CombineR<Op> {}
	);
	cudaFree(dpart);
	return res.v;
}

template<plan P>
[[nodiscard]] inline std::int64_t reduce_device(
	const std::uint8_t* d, std::size_t n, cudaStream_t s = 0
) {
	char op = 0;
	for (std::uint8_t i = 0; i < P.n_cells; ++i)
		if (P.cells[i].sch == schema::tree)
			op = P.cells[i].op;
	if (op == '+')
		return tree_reduce_gpu<add_i64>(d, n, 1024, s);
	if (op == '*')
		return tree_reduce_gpu<mul_i64>(d, n, 1024, s);
	if (op == '&')
		return tree_reduce_gpu<min_i64>(d, n, 1024, s);
	if (op == '|')
		return tree_reduce_gpu<max_i64>(d, n, 1024, s);
	return 0;
}

}  // namespace pars::core
