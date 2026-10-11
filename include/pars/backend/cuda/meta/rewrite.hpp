// pars/backend/cuda/meta/rewrite.hpp — Layer 2 of the meta backend.
//
// Two jobs:
//   1. `build_pipeline<S>`  : scan_plan -> pristine IR (classify, mask*, clip,
//      compact, nest) in stage order.
//   2. `rewrite<T>`         : compile-time passes over the IR (head fusion,
//      mask specialization).  `rewrite` is a metafunction, not a runtime pass.
//
// The theory is untouched; this only decides *which* specialized kernels the
// emitter instantiates.
#pragma once

#include "pars/backend/cuda/meta/ir.hpp"

#include <cstdint>

namespace pars::meta {

// ---------------------------------------------------------------------------
// Layer 1: pristine pipeline from a plan (constexpr, host-only).
// ---------------------------------------------------------------------------
template<bool Cond, scan_plan S, std::uint8_t D, class Acc>
struct build_masks_if;

template<scan_plan S, std::uint8_t D, class Acc>
struct build_masks_if<false, S, D, Acc> {
	using type = Acc;
};

template<scan_plan S, std::uint8_t D, class Acc>
struct build_masks_if<true, S, D, Acc> {
	using type = typename build_masks_if<
		(D + 1 < S.n_descs),
		S,
		static_cast<std::uint8_t>(D + 1),
		concat_t<Acc, type_list<mask<S.descs[D].kind, D>>>>::type;
};

// Collect mask<kind, D> nodes for descriptors D = 0 .. S.n_descs-1.
template<scan_plan S, std::uint8_t D = 0, class Acc = type_list<>>
struct build_masks : build_masks_if<(D < S.n_descs), S, D, Acc> {};

template<class MaskList>
struct make_pipeline;
template<class... Ms>
struct make_pipeline<type_list<Ms...>> {
	using type = pipeline<classify, Ms..., clip, compact, nest>;
};

// The pristine (un-rewritten) pipeline for a plan.
template<scan_plan S>
struct build_pipeline {
	using type = typename make_pipeline<typename build_masks<S>::type>::type;
};
template<scan_plan S>
using build_pipeline_t = typename build_pipeline<S>::type;

// ---------------------------------------------------------------------------
// Layer 2: rewrite passes.
// ---------------------------------------------------------------------------

// Pass 1 — head fusion: collapse classify + (single) mask + clip into one
// `head<Kind>` stage.  Single-mode grammars (JSON/CSV/TOML-array/clike) get the
// fused head; multi-mode grammars keep classify/mask/clip separate.
template<class P>
struct fuse_head {
	using type = P;
};
template<scan_plan::lex_kind Kind, std::uint8_t D, class... Rest>
struct fuse_head<pipeline<classify, mask<Kind, D>, clip, Rest...>> {
	using type = pipeline<head<Kind>, Rest...>;
};
template<class... Rest>
struct fuse_head<pipeline<classify, clip, Rest...>> {
	using type = pipeline<head_plain, Rest...>;
};

// Pass 2 — mask specialization tag: annotate each surviving mask with the
// policy its kind will be emitted as.  (Kept as a distinct type so the emitter
// can dispatch; identity when already applied.)
template<class T>
struct specialize_mask {
	using type = T;
};
template<scan_plan::lex_kind Kind, std::uint8_t D>
struct specialize_mask<mask<Kind, D>> {
	using type = mask<Kind, D>;	 // kind already carries the policy; policy_of_t<Kind>
};

// Apply all passes to a fixpoint (our passes are idempotent, one sweep is
// enough, but keep the driver general).
template<class P>
struct rewrite_once {
	using type = typename specialize_mask<typename fuse_head<P>::type>::type;
};
template<class P, int Fuel = 4>
struct rewrite {
	using next = typename rewrite_once<P>::type;
	using type = std::conditional_t<
		std::is_same_v<next, P>,
		P,
		typename rewrite<next, Fuel - 1>::type>;
};
template<class P>
struct rewrite<P, 0> {
	using type = P;
};
template<class P>
using rewrite_t = typename rewrite<P>::type;

// Convenience: plan -> rewritten pipeline.
template<scan_plan S>
using rewritten_pipeline_t = rewrite_t<build_pipeline_t<S>>;

}  // namespace pars::meta
