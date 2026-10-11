// pars/backend/cuda/meta/ir.hpp — Layer 1 of the meta backend.
//
// `scan_plan` -> a typed pipeline IR.  The theory (R1..R7) is unchanged: this
// header only re-expresses "which primitives run" as a *compile-time stage
// list* so that later layers (rewrite.hpp, emit.cuh) can transform and emit
// specialized kernels instead of launching fixed kernels with runtime
// descriptors.
//
// Host-only: no CUDA types here, so the IR + rewrite can be unit-tested with
// the plain C++ toolchain.
#pragma once

#include "pars/dsl.hpp"	 // type_list
#include "pars/plan.hpp"

#include <cstdint>
#include <type_traits>

namespace pars::meta {

// ---------------------------------------------------------------------------
// IR stage tags — 1:1 with the scan-algebra primitives.
// ---------------------------------------------------------------------------
struct classify {};	 // R1: terminals -> byte-class bitmasks
struct clip {};		 // R3: structural & ~masked
struct compact {};	 // R5: filter -> dense structural index
struct nest {};		 // R6: well-nested depth-sort pairing

// R4: one node per lexical masking rule.  `Kind` is the provable factorization
// read off the DFA; `D` is the descriptor slot index (for plan lookup).
template<scan_plan::lex_kind Kind, std::uint8_t D>
struct mask {};

// After the head-fusion rewrite, a single lexical mode collapses
// classify + mask + clip into one specialized kernel head.
template<scan_plan::lex_kind Kind>
struct head {};

// The no-mode case (a grammar with no lexical masking): classify + clip only.
struct head_plain {};

// ---------------------------------------------------------------------------
// Layer 4: a compact, value-mangled spec for `__global__` templates.  The full
// `scan_plan` is far too large to mangle as a kernel NTTP (MSVC C1064), so the
// specialized kernels take this small spec instead: only the structural chars
// and the single delimiter, which is all the fused head needs.
// ---------------------------------------------------------------------------
struct head_spec_t {
	char		 punct[8] = {};
	char		 open[8]  = {};
	char		 close[8] = {};
	std::uint8_t n_punct	= 0;
	std::uint8_t n_brackets = 0;
	char		 delim		= 0;
};

template<scan_plan S>
[[nodiscard]] constexpr head_spec_t make_head_spec() noexcept {
	head_spec_t h {};
	h.n_punct = S.n_punctuation;
	for (int i = 0; i < (int)S.n_punctuation; ++i) h.punct[i] = S.punctuation[i];
	h.n_brackets = S.n_brackets;
	for (int i = 0; i < (int)S.n_brackets; ++i) {
		h.open[i]  = S.brackets[i].open;
		h.close[i] = S.brackets[i].close;
	}
	h.delim = (S.n_modes > 0) ? S.modes[0].delim : 0;
	return h;
}

// A pipeline is an ordered, typed stage list.
template<class... Stages>
struct pipeline {
	using stages = type_list<Stages...>;
};

// ---------------------------------------------------------------------------
// mask policy tags (the "kind" a specialized kernel is emitted for)
// ---------------------------------------------------------------------------
struct affine_policy {};   // prefix-escape:  x' = (a & x) ^ b
struct parity_policy {};   // no-escape / doubled delimiter: prefix_xor toggle
struct comment_policy {};  // line comment
struct table_policy {};	   // general transition-monoid
struct fallback_policy {};

template<scan_plan::lex_kind Kind>
struct policy_of {
	using type = table_policy;
};
template<>
struct policy_of<scan_plan::lex_kind::affine> {
	using type = affine_policy;
};
template<>
struct policy_of<scan_plan::lex_kind::doubled> {
	using type = parity_policy;
};
template<>
struct policy_of<scan_plan::lex_kind::comment> {
	using type = comment_policy;
};
template<>
struct policy_of<scan_plan::lex_kind::fallback> {
	using type = fallback_policy;
};
template<scan_plan::lex_kind Kind>
using policy_of_t = typename policy_of<Kind>::type;

// ---------------------------------------------------------------------------
// Small type-list helpers used by the rewrite layer.
// ---------------------------------------------------------------------------
template<class A, class B>
struct concat;
template<class... As, class... Bs>
struct concat<type_list<As...>, type_list<Bs...>> {
	using type = type_list<As..., Bs...>;
};
template<class A, class B>
using concat_t = typename concat<A, B>::type;

template<class T, class List>
struct contains : std::false_type {};
template<class T, class... Ts>
struct contains<T, type_list<T, Ts...>> : std::true_type {};
template<class T, class U, class... Ts>
struct contains<T, type_list<U, Ts...>> : contains<T, type_list<Ts...>> {};
template<class T, class List>
inline constexpr bool contains_v = contains<T, List>::value;

// is_mask<T> — matches any mask<Kind, D>.
template<class T>
struct is_mask : std::false_type {};
template<scan_plan::lex_kind K, std::uint8_t D>
struct is_mask<mask<K, D>> : std::true_type {};

// mask_count<List> — how many mask nodes a stage list has.
template<class List>
struct mask_count;
template<>
struct mask_count<type_list<>> {
	static constexpr int value = 0;
};
template<class H, class... Ts>
struct mask_count<type_list<H, Ts...>> {
	static constexpr int value = (is_mask<H>::value ? 1 : 0) + mask_count<type_list<Ts...>>::value;
};
template<class List>
inline constexpr int mask_count_v = mask_count<List>::value;

}  // namespace pars::meta
