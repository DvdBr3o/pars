// pars/backend/cuda/meta/emit.cuh — Layer 3 of the meta backend.
//
// `emit` assembles *specialized* kernels from a rewritten pipeline.  A fused
// `head<Kind>` is lowered to a kernel whose policy is a compile-time integral
// parameter (parity / affine / table) and whose structural alphabet comes from
// a compact `head_spec_t` passed by value (constant param space, ~26 bytes vs
// the existing multi-KB GpuSpec/GpuPlan).
//
// NOTE on toolchain: a struct NTTP on a __global__ template (the "bake the
// whole spec as immediates" approach) triggers MSVC C1064/C1903 in the CUDA
// stub on this toolchain (CUDA 13.3 + MSVC 2026) even for a 9-byte struct,
// while an integral NTTP compiles cleanly.  We therefore specialize on an
// integral policy; see tests/probe.cu for the minimal repro.
#pragma once

#include "pars/backend/cuda/meta/rewrite.hpp"
#include "pars/backend/cuda/pipeline.cuh"

#include <cstdint>

namespace pars::gpu::meta {

using pars::meta::head;
using pars::meta::head_spec_t;
using pars::meta::make_head_spec;
using pars::meta::pipeline;

// Compile-time policy tag for a specialized head.
enum head_policy : int { pol_parity = 0, pol_affine = 1, pol_table = 2, pol_plain = 3 };

// ---------------------------------------------------------------------------
// Fused head: P1 classify + R2 delimiter mask, specialized by policy.
// ---------------------------------------------------------------------------
template<int Policy>
__global__ void head_classify_kernel(
	const std::uint8_t* data,
	int					n_words,
	head_spec_t			spec,
	std::uint32_t*		struct_m,
	std::uint32_t*		openclose_m,
	std::uint32_t*		delim_m
) {
	int i = blockIdx.x * blockDim.x + threadIdx.x;
	if (i >= n_words)
		return;
	const std::uint8_t* p = data + (std::size_t)i * 32;
	std::uint32_t		s = 0, oc = 0, d = 0;
	for (int k = 0; k < (int)spec.n_punct; ++k) s |= detail::eq32(p, spec.punct[k]);
	for (int k = 0; k < (int)spec.n_brackets; ++k) {
		const std::uint32_t m = detail::eq32(p, spec.open[k]) | detail::eq32(p, spec.close[k]);
		s |= m;
		oc |= m;
	}
	if (spec.delim)
		d = detail::eq32(p, spec.delim);
	struct_m[i]	   = s;
	openclose_m[i] = oc;
	delim_m[i]	   = d;
}

// R3 (clip) fused with the parity in_string computation.  `excl_scan[i] & 1` is
// the delimiter-count parity of all preceding words (the cross-word carry).
__global__ void head_parity_apply_kernel(
	const std::uint32_t* struct_m,
	const std::uint32_t* openclose_m,
	const std::uint32_t* delim_m,
	const std::uint32_t* excl_scan,
	int					 n_words,
	std::uint32_t*		 struct_out,
	std::uint32_t*		 openclose_out
) {
	int i = blockIdx.x * blockDim.x + threadIdx.x;
	if (i >= n_words)
		return;
	const std::uint32_t parity	 = detail::prefix_xor32(delim_m[i]);
	const bool			overflow = excl_scan[i] & 1u;
	const std::uint32_t inside	 = overflow ? ~parity : parity;
	struct_out[i]				 = struct_m[i] & ~inside;
	openclose_out[i]			 = openclose_m[i] & ~inside;
}

// ---------------------------------------------------------------------------
// Stage dispatch.  `emit_head<Kind, S>` reports whether it handled the head.
// ---------------------------------------------------------------------------
template<scan_plan::lex_kind Kind, scan_plan S>
struct emit_head {
	static constexpr bool specialized = false;
	static void			  run(
				 detail::DeviceCache&, const std::uint8_t*, std::size_t, int, int, cudaStream_t
			 ) {}
};

// Parity: doubled delimiter, or no-escape single delim (CSV / no-escape).
template<scan_plan S>
struct emit_head<scan_plan::lex_kind::doubled, S> {
	static constexpr bool specialized = true;
	static void			  run(
				 detail::DeviceCache& b,
				 const std::uint8_t*  d_in,
				 std::size_t		  /*size*/,
				 int				  n_words,
				 int				  grid,
				 cudaStream_t		  s
			 ) {
		using detail::BLOCK;
		const head_spec_t spec = make_head_spec<S>();
		head_classify_kernel<pol_parity>
			<<<grid, BLOCK, 0, s>>>(d_in, n_words, spec, b.struct_m, b.openclose_m, b.delim_m);
		detail::popcount_kernel<<<grid, BLOCK, 0, s>>>(b.delim_m, n_words, b.counts);
		thrust::exclusive_scan(thrust::cuda::par.on(s), b.counts, b.counts + n_words, b.excl, 0u);
		head_parity_apply_kernel<<<grid, BLOCK, 0, s>>>(
			b.struct_m,
			b.openclose_m,
			b.delim_m,
			b.excl,
			n_words,
			b.struct_out,
			b.openclose_out
		);
	}
};

// ---------------------------------------------------------------------------
// Pipeline-level dispatch.
// ---------------------------------------------------------------------------
template<class Pipeline, scan_plan S>
struct is_parity_pipeline : std::false_type {};
template<scan_plan S, class... Tail>
struct is_parity_pipeline<pipeline<head<scan_plan::lex_kind::doubled>, Tail...>, S>
	: std::true_type {};

}  // namespace pars::gpu::meta
