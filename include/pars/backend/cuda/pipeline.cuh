// prototypes/cupars/src/pars_gpu.cuh
//
// CUDA backend for the pars parser generator.  A grammar-derived lexical_spec
// (from derive_lexical_spec<Grammar>()) drives a GPU pipeline that mirrors
// cuJSON's three parallel phases:
//   P1 bitmap classification -> P2 lexical-mode masking -> P3 compaction
//   -> P4 depth-sort bracket matching (stack-free, GPU-friendly).
//
// Current scope: zero or one lexical mode (all five built-in grammars have one).
#pragma once

#include "pars/plan.hpp"

#include <thrust/execution_policy.h>
#include <thrust/scan.h>
#include <thrust/sort.h>
#include <thrust/extrema.h>
#include <thrust/copy.h>
#include <thrust/device_ptr.h>
#include <thrust/iterator/counting_iterator.h>
#include <thrust/sequence.h>
#include <thrust/functional.h>

#include <cuda_runtime.h>
#include <bit>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace pars::gpu {

namespace detail {

constexpr int BLOCK = 256;

enum : std::uint8_t { B_STRUCT = 1, B_OPEN = 2, B_CLOSE = 4, B_DELIM = 8, B_ESC = 16 };

struct GpuSpec {
	char		 chars[32];
	std::uint8_t buckets[32];
	int			 n_chars = 0;
	char		 open[8], close[8];
	int			 n_brackets = 0;
	char		 delim = 0, escape = 0;
	int			 n_modes = 0;
};

template<scan_plan S>
GpuSpec make_gpu_spec() {
	GpuSpec g {};
	auto	add = [&](char c, std::uint8_t b) {
		for (int i = 0; i < g.n_chars; ++i)
			if (g.chars[i] == c) {
				g.buckets[i] |= b;
				return;
			}
		g.chars[g.n_chars]	 = c;
		g.buckets[g.n_chars] = b;
		++g.n_chars;
	};
	for (std::size_t i = 0; i < S.n_punctuation; ++i) add(S.punctuation[i], B_STRUCT);
	for (std::size_t i = 0; i < S.n_brackets; ++i) {
		add(S.brackets[i].open, B_STRUCT | B_OPEN);
		add(S.brackets[i].close, B_STRUCT | B_CLOSE);
		g.open[g.n_brackets]  = S.brackets[i].open;
		g.close[g.n_brackets] = S.brackets[i].close;
		++g.n_brackets;
	}
	for (std::size_t i = 0; i < S.n_modes; ++i) {
		g.delim = S.modes[i].delim;
		// Doubled delimiters (CSV '""') are equivalent to a plain toggle for
		// masking on valid input: every quote flips in/out.  Route them
		// through the no-escape parity path (the old adjacency hack was wrong).
		const bool doubled = (S.modes[i].escape == S.modes[i].delim);
		g.escape		   = doubled ? 0 : S.modes[i].escape;
		add(S.modes[i].delim, B_DELIM);
		if (g.escape)
			add(S.modes[i].escape, B_ESC);
		++g.n_modes;
	}
	return g;
}

__host__ __device__ inline bool is_open_char(char c, const GpuSpec& s) {
	for (int i = 0; i < s.n_brackets; ++i)
		if (s.open[i] == c)
			return true;
	return false;
}

__host__ __device__ inline bool is_close_char(char c, const GpuSpec& s) {
	for (int i = 0; i < s.n_brackets; ++i)
		if (s.close[i] == c)
			return true;
	return false;
}

__host__ __device__ inline bool match_brackets(char o, char c, const GpuSpec& s) {
	for (int i = 0; i < s.n_brackets; ++i)
		if (s.open[i] == o)
			return s.close[i] == c;
	return false;
}

// ---------------------------------------------------------------------------
// P1c: per-rule automaton descriptor on the GPU.  Execution is a per-byte state
// transform; the prefix product (monoid scan) gives the running state.  The mask
// is the per-transition "consumed by an in-progress token" bit — no byte-wise
// divergent table lookup.
// ---------------------------------------------------------------------------
struct GpuDesc {
	std::uint8_t rep_of[256];
	std::uint8_t trans[8][8];
	std::uint8_t masked[8][8];
	std::uint8_t n_states  = 0;
	std::uint8_t n_classes = 0;
	std::uint8_t start	   = 0;
};

struct GpuPlan {
	GpuDesc		  d[8];
	int			  n = 0;
	int			  offset[8] {};		 // absolute slot offset of each desc
	int			  total_states = 0;
	std::uint8_t  start_slot[8] {};	 // absolute slot of each desc's start state
	std::uint64_t byte_t[256] {};	 // per-byte combined transform (absolute slots)
};

inline GpuPlan build_gpu_plan(const scan_plan& S) {
	GpuPlan p {};
	p.n		= S.n_descs;
	int off = 0;
	for (int i = 0; i < p.n; ++i) {
		const auto& dd = S.descs[i];
		GpuDesc&	g  = p.d[i];
		for (int b = 0; b < 256; ++b) g.rep_of[b] = dd.rep_of[b];
		for (int s = 0; s < 8; ++s)
			for (int c = 0; c < 8; ++c) {
				g.trans[s][c]  = dd.trans[s][c];
				g.masked[s][c] = dd.masked[s][c];
			}
		g.n_states		= dd.n_states;
		g.n_classes		= dd.n_classes;
		g.start			= dd.start;
		p.offset[i]		= off;
		p.start_slot[i] = static_cast<std::uint8_t>(off + dd.start);
		off += dd.n_states;
	}
	p.total_states		= off;

	std::uint64_t ident = 0;
	for (int s = 0; s < 16; ++s) ident |= static_cast<std::uint64_t>(s) << (4 * s);
	for (int b = 0; b < 256; ++b) {
		std::uint64_t T = ident;
		for (int d = 0; d < p.n; ++d) {
			const GpuDesc& g = p.d[d];
			const int	   c = g.rep_of[b];
			for (int s = 0; s < g.n_states; ++s) {
				const int slot = p.offset[d] + s;
				T &= ~(static_cast<std::uint64_t>(0xF) << (4 * slot));
				T |= static_cast<std::uint64_t>(p.offset[d] + g.trans[s][c]) << (4 * slot);
			}
		}
		p.byte_t[b] = T;
	}
	return p;
}

// packed transforms: 16 slots x 4 bits in a uint64
__device__ __forceinline__ int pk_apply64(std::uint64_t P, int s) {
	return (int)((P >> (4 * s)) & 0xF);
}

__device__ __forceinline__ std::uint64_t pk_compose64(std::uint64_t A, std::uint64_t B, int ns) {
	std::uint64_t r = 0;
	for (int s = 0; s < ns; ++s) {
		const int sa = (int)((A >> (4 * s)) & 0xF);
		const int sb = (int)((B >> (4 * sa)) & 0xF);
		r |= static_cast<std::uint64_t>(sb) << (4 * s);
	}
	return r;
}

struct TCompose64 {
	int		   ns;

	__device__ std::uint64_t operator()(std::uint64_t a, std::uint64_t b) const {
		return pk_compose64(a, b, ns);
	}
};

__global__ void compose_carry64_kernel(
	std::uint64_t* pref, int n_words, std::uint64_t carry, int ns
) {
	int i = blockIdx.x * blockDim.x + threadIdx.x;
	if (i < n_words)
		pref[i] = pk_compose64(carry, pref[i], ns);
}

__global__ void desc_wordt64_kernel(
	const std::uint8_t* data, int n, int n_words, GpuPlan gp, std::uint64_t* wt
) {
	__shared__ std::uint64_t s_t[256];
	for (int i = threadIdx.x; i < 256; i += blockDim.x) s_t[i] = gp.byte_t[i];
	__syncthreads();
	int w = blockIdx.x * blockDim.x + threadIdx.x;
	if (w >= n_words)
		return;
	std::uint64_t ident = 0;
	for (int s = 0; s < gp.total_states; ++s) ident |= static_cast<std::uint64_t>(s) << (4 * s);
	std::uint64_t t	   = ident;
	const int	  base = w * 32;
	for (int j = 0; j < 32; ++j) {
		const int i = base + j;
		if (i >= n)
			break;
		t = pk_compose64(t, s_t[data[i]], gp.total_states);
	}
	wt[w] = t;
}

__global__ void desc_replay64_kernel(
	const std::uint8_t* data, const std::uint64_t* prefix, int n, int n_words, GpuPlan gp,
	std::uint32_t* mask, std::uint64_t carry_prev = 0
) {
	__shared__ std::uint64_t s_t[256];
	__shared__ std::uint8_t s_rep[8][256];
	__shared__ std::uint8_t s_masked[8][8][8];
	for (int i = threadIdx.x; i < 256; i += blockDim.x) s_t[i] = gp.byte_t[i];
	for (int d = 0; d < gp.n; ++d) {
		for (int b = threadIdx.x; b < 256; b += blockDim.x) s_rep[d][b] = gp.d[d].rep_of[b];
		if (threadIdx.x < 64)
			s_masked[d][threadIdx.x / 8][threadIdx.x % 8] =
				gp.d[d].masked[threadIdx.x / 8][threadIdx.x % 8];
	}
	__syncthreads();

	int w = blockIdx.x * blockDim.x + threadIdx.x;
	if (w >= n_words)
		return;
	int slot[8];
	for (int d = 0; d < gp.n; ++d)
		slot[d] = (w == 0) ? (carry_prev ? pk_apply64(carry_prev, gp.start_slot[d]) :
										   (int)gp.start_slot[d]) :
							 pk_apply64(prefix[w - 1], gp.start_slot[d]);
	const int	  base = w * 32;
	std::uint32_t m	   = 0;
	for (int j = 0; j < 32; ++j) {
		const int i = base + j;
		if (i >= n)
			break;
		const unsigned char byte = data[i];
		const std::uint64_t T	 = s_t[byte];
		for (int d = 0; d < gp.n; ++d) {
			const int state = slot[d] - gp.offset[d];
			if (s_masked[d][state][s_rep[d][byte]])
				m |= (1u << j);
			slot[d] = pk_apply64(T, slot[d]);
		}
	}
	mask[w] = m;
}

// Packed descriptor state budget: the fused path packs each desc's transition
// into 4-bit slots of a uint64, so the total state count must fit in 16.
template<scan_plan S>
constexpr int total_packed_states() noexcept {
	int t = 0;
	for (std::uint8_t d = 0; d < S.n_descs; ++d) t += S.descs[d].n_states;
	return t;
}

// 32 bytes -> 1-bit-per-byte mask for a single char, via __vcmpeq4.
__device__ inline std::uint32_t eq32(const std::uint8_t* p, char c) {
	const std::uint32_t rep = (std::uint8_t)c * 0x01010101u;
	std::uint32_t		m	= 0;
#pragma unroll
	for (int j = 0; j < 8; ++j) {
		std::uint32_t w;
		std::memcpy(&w, p + j * 4, 4);
		std::uint32_t r	  = __vcmpeq4(w, rep) & 0x01010101u;
		std::uint32_t nib = (r | (r >> 7) | (r >> 14) | (r >> 21)) & 0xFu;
		m |= nib << (j * 4);
	}
	return m;
}

// ---- P1 ----
__global__ void classify_kernel(
	const std::uint8_t* data, int n_words, GpuSpec spec, std::uint32_t* struct_m,
	std::uint32_t* openclose_m, std::uint32_t* delim_m, std::uint32_t* escape_m
) {
	int i = blockIdx.x * blockDim.x + threadIdx.x;
	if (i >= n_words)
		return;
	const std::uint8_t* p = data + (std::size_t)i * 32;
	std::uint32_t		s = 0, oc = 0, d = 0, e = 0;
	for (int k = 0; k < spec.n_chars; ++k) {
		std::uint32_t m = eq32(p, spec.chars[k]);
		std::uint8_t  b = spec.buckets[k];
		if (b & B_STRUCT)
			s |= m;
		if (b & (B_OPEN | B_CLOSE))
			oc |= m;
		if (b & B_DELIM)
			d |= m;
		if (b & B_ESC)
			e |= m;
	}
	struct_m[i]	   = s;
	openclose_m[i] = oc;
	delim_m[i]	   = d;
	escape_m[i]	   = e;
}

// ---- P2: escape scan (prefix escape) as a 2-state automaton prefix scan ----
struct Esc32 {
	std::uint32_t escaped, escape;
};

__host__ __device__ inline Esc32 escaped_scan32(std::uint32_t e, std::uint32_t prev) {
	if (e == 0)
		return {prev, 0};
	std::uint32_t pot	  = e & ~prev;
	std::uint32_t maybe	  = pot << 1;
	std::uint32_t and_odd = maybe | 0xAAAAAAAAu;
	std::uint32_t even	  = and_odd - pot;
	std::uint32_t eat	  = even ^ 0xAAAAAAAAu;
	return {eat ^ (e | prev), eat & e};
}

__global__ void escape_transition_kernel(const std::uint32_t* esc, int n_words, std::uint8_t* f2) {
	int i = blockIdx.x * blockDim.x + threadIdx.x;
	if (i >= n_words)
		return;
	Esc32		 a	= escaped_scan32(esc[i], 0);
	Esc32		 b	= escaped_scan32(esc[i], 1);
	std::uint8_t f0 = (a.escape >> 31) & 1;
	std::uint8_t f1 = (b.escape >> 31) & 1;
	f2[i]			= f0 | (f1 << 1);
}

struct ComposeF2 {
	__host__ __device__ std::uint8_t operator()(std::uint8_t a, std::uint8_t b) const {
		std::uint8_t a0 = a & 1, a1 = (a >> 1) & 1;
		std::uint8_t b0 = b & 1, b1 = (b >> 1) & 1;
		return (a0 ? b1 : b0) | ((a1 ? b1 : b0) << 1);
	}
};

__global__ void apply_escape_kernel(
	const std::uint32_t* esc, const std::uint32_t* delim, const std::uint8_t* f2_scan, int n_words,
	std::uint32_t* real_delim
) {
	int i = blockIdx.x * blockDim.x + threadIdx.x;
	if (i >= n_words)
		return;
	std::uint32_t c = (i == 0) ? 0 : (f2_scan[i - 1] & 1);
	Esc32		  r = escaped_scan32(esc[i], c);
	real_delim[i]	= delim[i] & ~r.escaped;
}

__device__ inline std::uint32_t prefix_xor32(std::uint32_t x) {
	x ^= x << 1;
	x ^= x << 2;
	x ^= x << 4;
	x ^= x << 8;
	x ^= x << 16;
	return x;
}

__global__ void popcount_kernel(const std::uint32_t* in, int n, std::uint32_t* out) {
	int i = blockIdx.x * blockDim.x + threadIdx.x;
	if (i >= n)
		return;
	out[i] = __popc(in[i]);
}

__global__ void in_string_kernel(
	const std::uint32_t* real_delim, const std::uint32_t* excl_scan, int n_words,
	std::uint32_t* in_string
) {
	int i = blockIdx.x * blockDim.x + threadIdx.x;
	if (i >= n_words)
		return;
	std::uint32_t parity   = prefix_xor32(real_delim[i]);
	bool		  overflow = excl_scan[i] & 1u;
	in_string[i]		   = overflow ? ~parity : parity;
}

__global__ void structural_out_kernel(
	const std::uint32_t* struct_m, const std::uint32_t* openclose_m, const std::uint32_t* inside,
	int n_words, std::uint32_t* struct_out, std::uint32_t* openclose_out
) {
	int i = blockIdx.x * blockDim.x + threadIdx.x;
	if (i >= n_words)
		return;
	struct_out[i]	 = struct_m[i] & ~inside[i];
	openclose_out[i] = openclose_m[i] & ~inside[i];
}

// ---- P3: compaction ----
__global__ void xor_mask_kernel(std::uint32_t* in, int n_words) {
	int i = blockIdx.x * blockDim.x + threadIdx.x;
	if (i < n_words)
		in[i] = ~in[i];
}

__global__ void scatter_kernel(
	const std::uint8_t* data, const std::uint32_t* struct_out, const std::uint32_t* struct_excl,
	const std::uint32_t* oc_out, const std::uint32_t* oc_excl, int n_words,
	std::int32_t* struct_idx, std::uint8_t* struct_char, std::int32_t* oc_struct_pos,
	std::uint8_t* oc_char, std::uint32_t out_base = 0, std::size_t byte_base = 0
) {
	int i = blockIdx.x * blockDim.x + threadIdx.x;
	if (i >= n_words)
		return;
	const std::uint8_t* p  = data + (std::size_t)i * 32;

	std::uint32_t		s  = struct_out[i];
	std::uint32_t		sb = out_base + struct_excl[i];
	while (s) {
		int b			= __ffs(s) - 1;
		struct_idx[sb]	= (std::int32_t)(byte_base + (std::size_t)i * 32 + b);
		struct_char[sb] = p[b];
		++sb;
		s &= s - 1;
	}

	std::uint32_t o	 = oc_out[i];
	std::uint32_t ob = oc_excl[i];
	while (o) {
		int			  b		= __ffs(o) - 1;
		std::uint32_t below = struct_out[i] & ((1u << b) - 1u);
		oc_struct_pos[ob]	= (std::int32_t)(out_base + struct_excl[i] + __popc(below));
		oc_char[ob]			= p[b];
		++ob;
		o &= o - 1;
	}
}

// ---- P4: depth-sort matching ----
__global__ void depth_init_kernel(
	const std::uint8_t* oc_char, int n_oc, GpuSpec spec, std::int32_t* delta, bool* bad
) {
	int i = blockIdx.x * blockDim.x + threadIdx.x;
	if (i >= n_oc)
		return;
	char c = (char)oc_char[i];
	if (is_open_char(c, spec))
		delta[i] = 1;
	else if (is_close_char(c, spec))
		delta[i] = -1;
	else {
		delta[i] = 0;
		*bad	 = true;
	}
}

__global__ void depth_final_kernel(
	const std::int32_t* bal, const std::uint8_t* oc_char, int n_oc, GpuSpec spec,
	std::int32_t* depth
) {
	int i = blockIdx.x * blockDim.x + threadIdx.x;
	if (i >= n_oc)
		return;
	char c	 = (char)oc_char[i];
	depth[i] = is_open_char(c, spec) ? bal[i] - 1 : bal[i];
}

__global__ void pair_validate_kernel(
	const std::int32_t* order, const std::uint8_t* oc_char, int n_oc, GpuSpec spec,
	std::int32_t* oc_pair, bool* mismatched
) {
	int k = blockIdx.x * blockDim.x + threadIdx.x;
	if (k * 2 + 1 >= n_oc)
		return;
	int	 a	= order[k * 2];
	int	 b	= order[k * 2 + 1];
	char ca = (char)oc_char[a], cb = (char)oc_char[b];
	if (is_open_char(ca, spec) && is_close_char(cb, spec) && match_brackets(ca, cb, spec)) {
		oc_pair[a] = b;
		oc_pair[b] = a;
	} else {
		*mismatched = true;
	}
}

__global__ void finalize_pairs_kernel(
	const std::int32_t* oc_pair, const std::int32_t* oc_struct_pos, int n_oc, std::int32_t* pair_pos
) {
	int p = blockIdx.x * blockDim.x + threadIdx.x;
	if (p >= n_oc)
		return;
	int q					   = oc_pair[p];
	pair_pos[oc_struct_pos[p]] = (q >= 0) ? oc_struct_pos[q] : -1;
}

// Combine the two final counts into one device buffer so P3 needs a single D2H.
__global__ void pack_counts_kernel(
	const std::uint32_t* se, const std::uint32_t* oe, const std::uint32_t* so,
	const std::uint32_t* oo, int n_words, std::uint32_t* out
) {
	if (blockIdx.x != 0 || threadIdx.x != 0)
		return;
	out[0] = se[n_words - 1] + __popc(so[n_words - 1]);
	out[1] = oe[n_words - 1] + __popc(oo[n_words - 1]);
}

// ---- P4: depth radix sort (bounded) ----
__global__ void depth_clamp_kernel(std::int32_t* depth, int n_oc) {
	int i = blockIdx.x * blockDim.x + threadIdx.x;
	if (i < n_oc && depth[i] < 0)
		depth[i] = 0;
}

inline void check(cudaError_t e, const char* what) {
	if (e != cudaSuccess) {
		std::size_t fr = 0, tot = 0;
		cudaMemGetInfo(&fr, &tot);
		std::fprintf(
			stderr,
			"cuda error %s: %s (free=%zu MiB, total=%zu MiB)\n",
			what,
			cudaGetErrorString(e),
			fr >> 20,
			tot >> 20
		);
		std::exit(1);
	}
}

// Device allocation that, on failure, reports exactly how much was requested
// versus how much is free, so an OOM can be attributed to an oversize working
// set or to the GPU being otherwise occupied.
inline void malloc_or_die(void** p, std::size_t bytes, const char* what) {
	cudaError_t e = cudaMalloc(p, bytes);
	if (e != cudaSuccess) {
		std::size_t fr = 0, tot = 0;
		cudaMemGetInfo(&fr, &tot);
		std::fprintf(
			stderr,
			"[pars-gpu] %s: need %.1f MiB, free %.1f MiB, total %.1f MiB (%s)\n",
			what,
			static_cast<double>(bytes) / 1048576.0,
			static_cast<double>(fr) / 1048576.0,
			static_cast<double>(tot) / 1048576.0,
			cudaGetErrorString(e)
		);
		std::exit(1);
	}
}

}  // namespace detail

// Persistent device-buffer cache: reused across scan() calls so that repeated
// parses are not bound by cudaMalloc/cudaFree.
namespace detail {
struct DeviceCache {
	std::size_t	   cap_words = 0, cap_struct = 0, cap_oc = 0, cap_pairs = 0;
	std::uint8_t * data = nullptr, *f2 = nullptr, *f2_scan = nullptr;
	std::uint32_t *struct_m = nullptr, *openclose_m = nullptr, *delim_m = nullptr,
				  *escape_m	  = nullptr;
	std::uint32_t *real_delim = nullptr, *in_string = nullptr, *counts = nullptr, *excl = nullptr;
	std::uint32_t *struct_out = nullptr, *openclose_out = nullptr, *struct_excl = nullptr,
				  *oc_excl	  = nullptr;
	std::int32_t * struct_idx = nullptr, *oc_struct_pos = nullptr, *delta = nullptr, *bal = nullptr;
	std::int32_t * depth = nullptr, *order = nullptr, *oc_pair = nullptr, *pair_pos = nullptr;
	std::uint8_t * struct_char = nullptr, *oc_char = nullptr;
	std::uint64_t *dwt = nullptr, *dpref = nullptr;
	std::uint32_t* dmask = nullptr;
	bool *		   bad = nullptr, *mismatched = nullptr;

	~DeviceCache() { free_all(); }

	void free_all() {
		auto f = [](void* p) {
			if (p)
				cudaFree(p);
		};
		f(data);
		f(f2);
		f(f2_scan);
		f(struct_m);
		f(openclose_m);
		f(delim_m);
		f(escape_m);
		f(real_delim);
		f(in_string);
		f(counts);
		f(excl);
		f(struct_out);
		f(openclose_out);
		f(struct_excl);
		f(oc_excl);
		f(struct_idx);
		f(oc_struct_pos);
		f(delta);
		f(bal);
		f(depth);
		f(order);
		f(oc_pair);
		f(pair_pos);
		f(struct_char);
		f(oc_char);
		f(dwt);
		f(dpref);
		f(dmask);
		f(bad);
		f(mismatched);
		cap_words = cap_struct = cap_oc = 0;
	}

	void ensure_words(std::size_t n_words, std::size_t padded) {
		if (n_words <= cap_words)
			return;
		auto f = [](void* p) {
			if (p)
				cudaFree(p);
		};
		f(data);
		f(f2);
		f(f2_scan);
		f(struct_m);
		f(openclose_m);
		f(delim_m);
		f(escape_m);
		f(real_delim);
		f(in_string);
		f(counts);
		f(excl);
		f(struct_out);
		f(openclose_out);
		f(struct_excl);
		f(oc_excl);
		f(dwt);
		f(dpref);
		f(dmask);
		malloc_or_die((void**)&data, padded, "cache data");
		malloc_or_die((void**)&f2, n_words, "cache f2");
		malloc_or_die((void**)&f2_scan, n_words, "cache f2_scan");
		malloc_or_die((void**)&struct_m, n_words * 4, "cache struct_m");
		malloc_or_die((void**)&openclose_m, n_words * 4, "cache openclose_m");
		malloc_or_die((void**)&delim_m, n_words * 4, "cache delim_m");
		malloc_or_die((void**)&escape_m, n_words * 4, "cache escape_m");
		malloc_or_die((void**)&real_delim, n_words * 4, "cache real_delim");
		malloc_or_die((void**)&in_string, n_words * 4, "cache in_string");
		malloc_or_die((void**)&counts, n_words * 4, "cache counts");
		malloc_or_die((void**)&excl, n_words * 4, "cache excl");
		malloc_or_die((void**)&struct_out, n_words * 4, "cache struct_out");
		malloc_or_die((void**)&openclose_out, n_words * 4, "cache openclose_out");
		malloc_or_die((void**)&struct_excl, n_words * 4, "cache struct_excl");
		malloc_or_die((void**)&oc_excl, n_words * 4, "cache oc_excl");
		malloc_or_die((void**)&dwt, (std::size_t)n_words * 8, "cache dwt");
		malloc_or_die((void**)&dpref, (std::size_t)n_words * 8, "cache dpref");
		malloc_or_die((void**)&dmask, (std::size_t)n_words * 4, "cache dmask");
		if (!bad)
			malloc_or_die((void**)&bad, sizeof(bool), "cache bad");
		if (!mismatched)
			malloc_or_die((void**)&mismatched, sizeof(bool), "cache mismatched");
		cap_words = n_words;
	}

	void ensure_struct(std::size_t n_struct) {
		if (n_struct <= cap_struct)
			return;
		auto f = [](void* p) {
			if (p)
				cudaFree(p);
		};
		f(struct_idx);
		f(struct_char);
		malloc_or_die((void**)&struct_idx, n_struct * 4, "cache struct_idx");
		malloc_or_die((void**)&struct_char, n_struct, "cache struct_char");
		cap_struct = n_struct;
	}

	// pair_pos is the same size as the result; allocate it only on demand.
	void ensure_pairs(std::size_t n) {
		if (n <= cap_pairs)
			return;
		if (pair_pos)
			cudaFree(pair_pos);
		malloc_or_die((void**)&pair_pos, n * 4, "cache pair_pos");
		cap_pairs = n;
	}

	void ensure_oc(std::size_t n_oc) {
		if (n_oc <= cap_oc)
			return;
		auto f = [](void* p) {
			if (p)
				cudaFree(p);
		};
		f(oc_struct_pos);
		f(oc_char);
		f(delta);
		f(bal);
		f(depth);
		f(order);
		f(oc_pair);
		malloc_or_die((void**)&oc_struct_pos, n_oc * 4, "cache oc_struct_pos");
		malloc_or_die((void**)&oc_char, n_oc, "cache oc_char");
		malloc_or_die((void**)&delta, n_oc * 4, "cache delta");
		malloc_or_die((void**)&bal, n_oc * 4, "cache bal");
		malloc_or_die((void**)&depth, n_oc * 4, "cache depth");
		malloc_or_die((void**)&order, n_oc * 4, "cache order");
		malloc_or_die((void**)&oc_pair, n_oc * 4, "cache oc_pair");
		cap_oc = n_oc;
	}
};
}  // namespace detail

// ---------------------------------------------------------------------------
// Public boundary.  The framework owns "boundary + kernel + chunking", never
// "transfer": bulk H2D/D2H exists ONLY in scan_host (the adapter).  scan_device
// is zero-copy: input is already on the device, results stay on the device.
// ---------------------------------------------------------------------------
struct device_result {
	const std::int32_t* structural = nullptr;  // device
	const std::int32_t* pair_pos   = nullptr;  // device, or nullptr
	std::uint32_t		n_struct   = 0;
	bool				balanced = true, well_formed = true;
	int					max_depth = 0;
};

struct host_result {
	std::vector<std::int32_t> structural;
	std::vector<std::int32_t> pair_pos;
	bool					  balanced = true, well_formed = true;
	int						  max_depth = 0;
};

// Carry between chunks.  `scan` is the prefix monoid element at the chunk start
// (affine: 1 bit; comment: 1 bit; table: packed uint64 transform); `out_base` is
// the compaction output offset.
struct chunk_carry {
	std::uint64_t scan	   = 0;
	std::uint32_t out_base = 0;
};

// Per-stage wall time (ms), filled when profiling is requested.
struct stage_times {
	double p1 = 0, p2 = 0, p3 = 0, p4 = 0;
};

// Core zero-copy kernel.  `d_in` must be readable for ceil(size/32)*32 bytes
// (the caller pads; scan_host does).  No bulk transfers here.
template<scan_plan S>
device_result scan_device_impl(
	const std::uint8_t* d_in, std::size_t size, detail::DeviceCache& b, bool want_pairs,
	bool run_pairs = true, stage_times* st = nullptr, cudaStream_t s = 0
) {
	using namespace detail;
	static_assert(
		total_packed_states<S>() <= 16,
		"[pars] descriptor state budget exceeded (total_states > 16): "
		"split modes or simplify lexical rules"
	);
	device_result out;
	if (size == 0)
		return out;

	cudaEvent_t ev[5] {};
	if (st)
		for (int i = 0; i < 5; ++i) cudaEventCreate(&ev[i]);
	auto mark = [&](int i) {
		if (st)
			cudaEventRecord(ev[i]);
	};
	mark(0);

	GpuSpec			  spec	  = make_gpu_spec<S>();
	const std::size_t padded  = (size + 31) / 32 * 32;
	const int		  n_words = (int)(padded / 32);
	const int		  grid	  = (n_words + BLOCK - 1) / BLOCK;

	b.ensure_words(n_words, padded);

	// P1 classify
	classify_kernel<<<grid, BLOCK, 0, s>>>(
		d_in,
		n_words,
		spec,
		b.struct_m,
		b.openclose_m,
		b.delim_m,
		b.escape_m
	);
	check(cudaGetLastError(), "classify");
	mark(1);

	// P2 mask
	std::uint32_t* inside = b.in_string;
	constexpr bool kFast  = (S.n_comments == 0 && S.n_modes <= 1)
						 && (S.n_descs == 0
							 || (S.n_descs == 1
								 && (S.descs[0].kind == scan_plan::lex_kind::doubled
									 || (S.descs[0].kind == scan_plan::lex_kind::affine
										 && S.descs[0].escape == 0))));
	if constexpr (kFast) {
		// parity only (no-escape / doubled): the old prefix-escape branch was
		// removed because it was not equivalent to the desc semantics on
		// adversarial input.
		if (spec.n_modes == 0) {
			cudaMemsetAsync(b.in_string, 0, n_words * 4, s);
		} else {
			cudaMemcpyAsync(b.real_delim, b.delim_m, n_words * 4, cudaMemcpyDeviceToDevice, s);
			popcount_kernel<<<grid, BLOCK, 0, s>>>(b.real_delim, n_words, b.counts);
			thrust::exclusive_scan(
				thrust::cuda::par.on(s),
				b.counts,
				b.counts + n_words,
				b.excl,
				0u
			);
			in_string_kernel<<<grid, BLOCK, 0, s>>>(b.real_delim, b.excl, n_words, b.in_string);
		}
		check(cudaGetLastError(), "mode");
	} else {
		GpuPlan gp = build_gpu_plan(S);
		cudaMemsetAsync(b.in_string, 0, n_words * 4, s);
		if (gp.n > 0) {
			const int wgrid = (n_words + BLOCK - 1) / BLOCK;
			desc_wordt64_kernel<<<wgrid, BLOCK, 0, s>>>(d_in, (int)size, n_words, gp, b.dwt);
			check(cudaGetLastError(), "desc wordt64");
			thrust::inclusive_scan(
				thrust::cuda::par.on(s),
				b.dwt,
				b.dwt + n_words,
				b.dpref,
				TCompose64 {gp.total_states}
			);
			desc_replay64_kernel<<<wgrid, BLOCK, 0, s>>>(
				d_in,
				b.dpref,
				(int)size,
				n_words,
				gp,
				b.dmask,
				0
			);
			check(cudaGetLastError(), "desc replay64");
			cudaMemcpyAsync(
				b.in_string,
				b.dmask,
				(std::size_t)n_words * 4,
				cudaMemcpyDeviceToDevice,
				s
			);
		}
		check(cudaGetLastError(), "desc mask");
	}
	mark(2);

	structural_out_kernel<<<grid, BLOCK, 0, s>>>(
		b.struct_m,
		b.openclose_m,
		inside,
		n_words,
		b.struct_out,
		b.openclose_out
	);
	check(cudaGetLastError(), "structural out");

	// P3 compaction
	popcount_kernel<<<grid, BLOCK, 0, s>>>(b.struct_out, n_words, b.counts);
	thrust::exclusive_scan(
		thrust::cuda::par.on(s),
		b.counts,
		b.counts + n_words,
		b.struct_excl,
		0u
	);
	popcount_kernel<<<grid, BLOCK, 0, s>>>(b.openclose_out, n_words, b.counts);
	thrust::exclusive_scan(thrust::cuda::par.on(s), b.counts, b.counts + n_words, b.oc_excl, 0u);

	// one scalar readback for both counts
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
	check(cudaGetLastError(), "pack counts");
	const std::uint32_t n_struct = packed[0];
	const std::uint32_t n_oc	 = packed[1];

	if (std::getenv("PARS_GPU_MEM_DEBUG")) {
		std::size_t fr = 0, tot = 0;
		cudaMemGetInfo(&fr, &tot);
		std::fprintf(
			stderr,
			"[pars-gpu] n=%zu B  n_struct=%u  n_oc=%u  free=%zu MiB / %zu MiB\n",
			size,
			n_struct,
			n_oc,
			fr >> 20,
			tot >> 20
		);
	}

	b.ensure_struct(n_struct);
	b.ensure_oc(n_oc);
	if (n_struct > 0 && want_pairs) {
		b.ensure_pairs(n_struct);
		cudaMemsetAsync(b.pair_pos, 0xFF, (std::size_t)n_struct * 4, s);
	}
	scatter_kernel<<<grid, BLOCK, 0, s>>>(
		d_in,
		b.struct_out,
		b.struct_excl,
		b.openclose_out,
		b.oc_excl,
		n_words,
		b.struct_idx,
		b.struct_char,
		b.oc_struct_pos,
		b.oc_char
	);
	check(cudaGetLastError(), "scatter");
	mark(3);

	// P4 nesting
	if (n_oc > 0 && run_pairs) {
		cudaMemsetAsync(b.bad, 0, sizeof(bool), s);
		cudaMemsetAsync(b.mismatched, 0, sizeof(bool), s);
		depth_init_kernel<<<(n_oc + BLOCK - 1) / BLOCK, BLOCK, 0, s>>>(
			b.oc_char,
			n_oc,
			spec,
			b.delta,
			b.bad
		);
		thrust::inclusive_scan(thrust::cuda::par.on(s), b.delta, b.delta + n_oc, b.bal);
		depth_final_kernel<<<(n_oc + BLOCK - 1) / BLOCK, BLOCK, 0, s>>>(
			b.bal,
			b.oc_char,
			n_oc,
			spec,
			b.depth
		);

		// sort bracket tokens by depth (primitive keys -> radix-backed in thrust)
		thrust::sequence(thrust::cuda::par.on(s), b.order, b.order + n_oc, 0);
		depth_clamp_kernel<<<(n_oc + BLOCK - 1) / BLOCK, BLOCK, 0, s>>>(b.depth, n_oc);
		thrust::sort_by_key(thrust::cuda::par.on(s), b.depth, b.depth + n_oc, b.order);
		check(cudaGetLastError(), "depth sort");

		cudaMemsetAsync(b.oc_pair, 0xFF, (std::size_t)n_oc * 4, s);
		int pgrid = (n_oc / 2 + BLOCK - 1) / BLOCK;
		if (pgrid < 1)
			pgrid = 1;
		pair_validate_kernel<<<pgrid, BLOCK, 0, s>>>(
			b.order,
			b.oc_char,
			n_oc,
			spec,
			b.oc_pair,
			b.mismatched
		);
		check(cudaGetLastError(), "pair");

		bool bad = false, mis = false;
		cudaMemcpyAsync(&bad, b.bad, 1, cudaMemcpyDeviceToHost, s);
		cudaMemcpyAsync(&mis, b.mismatched, 1, cudaMemcpyDeviceToHost, s);
		int	 min_bal = 0, last_bal = 0, max_bal = 0;
		auto min_it = thrust::min_element(thrust::cuda::par.on(s), b.bal, b.bal + n_oc);
		auto max_it = thrust::max_element(thrust::cuda::par.on(s), b.bal, b.bal + n_oc);
		cudaMemcpyAsync(&min_bal, min_it, 4, cudaMemcpyDeviceToHost, s);
		cudaMemcpyAsync(&max_bal, max_it, 4, cudaMemcpyDeviceToHost, s);
		cudaMemcpyAsync(&last_bal, b.bal + n_oc - 1, 4, cudaMemcpyDeviceToHost, s);
		cudaStreamSynchronize(s);

		out.balanced	= (min_bal >= 0) && (last_bal == 0) && !bad;
		out.well_formed = out.balanced && !mis;
		out.max_depth	= (max_bal > 0) ? max_bal : 0;

		if (want_pairs) {
			finalize_pairs_kernel<<<(n_oc + BLOCK - 1) / BLOCK, BLOCK, 0, s>>>(
				b.oc_pair,
				b.oc_struct_pos,
				n_oc,
				b.pair_pos
			);
			check(cudaGetLastError(), "finalize pairs");
		}
	}

	mark(4);
	if (st) {
		cudaEventSynchronize(ev[4]);
		float ms = 0;
		cudaEventElapsedTime(&ms, ev[0], ev[1]);
		st->p1 = ms;
		cudaEventElapsedTime(&ms, ev[1], ev[2]);
		st->p2 = ms;
		cudaEventElapsedTime(&ms, ev[2], ev[3]);
		st->p3 = ms;
		cudaEventElapsedTime(&ms, ev[3], ev[4]);
		st->p4 = ms;
		for (int i = 0; i < 5; ++i) cudaEventDestroy(ev[i]);
	}

	out.structural = b.struct_idx;
	out.pair_pos   = want_pairs ? b.pair_pos : nullptr;
	out.n_struct   = n_struct;
	return out;
}

// Chunked stage-1 for the parity fast path (no-escape / doubled): processes a
// chunk and writes its structural indices at `in.out_base`, returning the
// updated carry.  No P4 here (pairing is global; run it separately).
template<scan_plan S>
chunk_carry scan_chunk_impl(
	const std::uint8_t* d_chunk, std::size_t n, chunk_carry in, detail::DeviceCache& b,
	cudaStream_t s = 0, std::size_t byte_base = 0
) {
	using namespace detail;
	constexpr bool kParity =
		(S.n_comments == 0 && S.n_modes <= 1)
		&& (S.n_descs == 1
			&& (S.descs[0].kind == scan_plan::lex_kind::doubled
				|| (S.descs[0].kind == scan_plan::lex_kind::affine && S.descs[0].escape == 0)));
	chunk_carry out = in;
	if (n == 0)
		return out;

	GpuSpec			  spec	  = make_gpu_spec<S>();
	const std::size_t padded  = (n + 31) / 32 * 32;
	const int		  n_words = (int)(padded / 32);
	const int		  grid	  = (n_words + BLOCK - 1) / BLOCK;
	b.ensure_words(n_words, padded);
	if (padded > n)
		cudaMemsetAsync(const_cast<std::uint8_t*>(d_chunk) + n, 0, padded - n, s);

	classify_kernel<<<grid, BLOCK, 0, s>>>(
		d_chunk,
		n_words,
		spec,
		b.struct_m,
		b.openclose_m,
		b.delim_m,
		b.escape_m
	);

	std::uint32_t packed[3] = {0, 0, 0};
	std::uint64_t carry_out = 0;
	if constexpr (kParity) {
		if (spec.n_modes == 0) {
			cudaMemsetAsync(b.in_string, 0, n_words * 4, s);
		} else {
			cudaMemcpyAsync(b.real_delim, b.delim_m, n_words * 4, cudaMemcpyDeviceToDevice, s);
			popcount_kernel<<<grid, BLOCK, 0, s>>>(b.real_delim, n_words, b.counts);
			thrust::exclusive_scan(
				thrust::cuda::par.on(s),
				b.counts,
				b.counts + n_words,
				b.excl,
				0u
			);
			in_string_kernel<<<grid, BLOCK, 0, s>>>(b.real_delim, b.excl, n_words, b.in_string);
			if (in.scan & 1u)
				xor_mask_kernel<<<grid, BLOCK, 0, s>>>(b.in_string, n_words);
		}
	} else {
		// general descriptor path, seeded by the incoming packed transform
		GpuPlan gp = build_gpu_plan(S);
		cudaMemsetAsync(b.in_string, 0, n_words * 4, s);
		if (gp.n > 0) {
			const int wgrid = (n_words + BLOCK - 1) / BLOCK;
			desc_wordt64_kernel<<<wgrid, BLOCK, 0, s>>>(d_chunk, (int)n, n_words, gp, b.dwt);
			thrust::inclusive_scan(
				thrust::cuda::par.on(s),
				b.dwt,
				b.dwt + n_words,
				b.dpref,
				TCompose64 {gp.total_states}
			);
			if (in.scan != 0)
				compose_carry64_kernel<<<wgrid, BLOCK, 0, s>>>(
					b.dpref,
					n_words,
					in.scan,
					gp.total_states
				);
			desc_replay64_kernel<<<wgrid, BLOCK, 0, s>>>(
				d_chunk,
				b.dpref,
				(int)n,
				n_words,
				gp,
				b.dmask,
				in.scan
			);
			cudaMemcpyAsync(
				b.in_string,
				b.dmask,
				(std::size_t)n_words * 4,
				cudaMemcpyDeviceToDevice,
				s
			);
			cudaMemcpyAsync(&carry_out, b.dpref + (n_words - 1), 8, cudaMemcpyDeviceToHost, s);
		}
	}

	structural_out_kernel<<<grid, BLOCK, 0, s>>>(
		b.struct_m,
		b.openclose_m,
		b.in_string,
		n_words,
		b.struct_out,
		b.openclose_out
	);
	popcount_kernel<<<grid, BLOCK, 0, s>>>(b.struct_out, n_words, b.counts);
	thrust::exclusive_scan(
		thrust::cuda::par.on(s),
		b.counts,
		b.counts + n_words,
		b.struct_excl,
		0u
	);
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
	cudaMemcpyAsync(packed, b.excl, 8, cudaMemcpyDeviceToHost, s);
	if constexpr (kParity)
		cudaMemcpyAsync(&packed[2], b.in_string + (n_words - 1), 4, cudaMemcpyDeviceToHost, s);
	scatter_kernel<<<grid, BLOCK, 0, s>>>(
		d_chunk,
		b.struct_out,
		b.struct_excl,
		b.openclose_out,
		b.oc_excl,
		n_words,
		b.struct_idx,
		b.struct_char,
		b.oc_struct_pos,
		b.oc_char,
		in.out_base,
		byte_base
	);
	cudaStreamSynchronize(s);
	out.out_base = in.out_base + packed[0];
	out.scan	 = kParity ? ((packed[2] >> 31) & 1u) : carry_out;
	return out;
}

template<scan_plan S>
class scanner {
public:
	device_result scan_device(
		const std::uint8_t* d_data, std::size_t n, cudaStream_t s = 0, bool want_pairs = false,
		bool run_pairs = true, stage_times* st = nullptr
	) const {
		return scan_device_impl<S>(d_data, n, cache_, want_pairs, run_pairs, st, s);
	}

	// Adapter: the ONLY place with bulk transfers (pinned + async).  Callers who
	// own the transport may instead call scan_device / scan_chunk directly.
	host_result scan_host(
		const std::uint8_t* h_data, std::size_t n, cudaStream_t s = 0, bool want_pairs = false
	) const {
		const std::size_t padded = (n + 31) / 32 * 32;
		ensure_pinned(padded);
		ensure_dev_in(padded);
		std::memcpy(pinned_, h_data, n);
		if (padded > n)
			std::memset(pinned_ + n, 0, padded - n);
		cudaMemcpyAsync(dev_in_, pinned_, padded, cudaMemcpyHostToDevice, s);
		device_result dr = scan_device_impl<S>(dev_in_, n, cache_, want_pairs, true, nullptr, s);
		host_result	  out;
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
		if (want_pairs && dr.pair_pos) {
			out.pair_pos.resize(dr.n_struct);
			cudaMemcpyAsync(
				out.pair_pos.data(),
				dr.pair_pos,
				(std::size_t)dr.n_struct * 4,
				cudaMemcpyDeviceToHost,
				s
			);
		}
		cudaStreamSynchronize(s);
		return out;
	}

	// Chunked stage-1 (parity path): caller preallocates the output with
	// reserve_struct(total) then calls scan_chunk per chunk.
	void		reserve_struct(std::size_t cap) const { cache_.ensure_struct(cap); }

	chunk_carry scan_chunk(
		const std::uint8_t* d_chunk, std::size_t n, chunk_carry in, cudaStream_t s = 0,
		std::size_t byte_base = 0
	) const {
		return scan_chunk_impl<S>(d_chunk, n, in, cache_, s, byte_base);
	}

	[[nodiscard]] const std::int32_t* struct_ptr() const { return cache_.struct_idx; }

	~scanner() {
		if (pinned_)
			cudaFreeHost(pinned_);
		if (dev_in_)
			cudaFree(dev_in_);
	}

private:
	void ensure_pinned(std::size_t need) const {
		if (need <= pinned_cap_)
			return;
		if (pinned_)
			cudaFreeHost(pinned_);
		detail::check(cudaHostAlloc(&pinned_, need, cudaHostAllocDefault), "pinned");
		pinned_cap_ = need;
	}

	void ensure_dev_in(std::size_t need) const {
		if (need <= dev_in_cap_)
			return;
		if (dev_in_)
			cudaFree(dev_in_);
		detail::check(cudaMalloc(&dev_in_, need), "dev_in");
		dev_in_cap_ = need;
	}

	mutable detail::DeviceCache cache_;
	mutable std::uint8_t*		pinned_		= nullptr;
	mutable std::size_t			pinned_cap_ = 0;
	mutable std::uint8_t*		dev_in_		= nullptr;
	mutable std::size_t			dev_in_cap_ = 0;
};

template<scan_plan S>
device_result scan_device(
	const std::uint8_t* d_data, std::size_t n, cudaStream_t s = 0, bool want_pairs = false
) {
	static thread_local scanner<S> sc;
	return sc.scan_device(d_data, n, s, want_pairs);
}

template<scan_plan S>
host_result scan_host(const std::uint8_t* h_data, std::size_t n, bool want_pairs = false) {
	static thread_local scanner<S> sc;
	return sc.scan_host(h_data, n, 0, want_pairs);
}

template<scan_plan S>
chunk_carry scan_chunk(
	const std::uint8_t* d_chunk, std::size_t n, chunk_carry in, cudaStream_t s = 0,
	std::size_t byte_base = 0
) {
	static thread_local scanner<S> sc;
	return sc.scan_chunk(d_chunk, n, in, s, byte_base);
}

}  // namespace pars::gpu
