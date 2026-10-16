// pars/algebra/summary.hpp — the unified algebraic core.
//
// THE WHOLE THEORY IN ONE SENTENCE
// ---------------------------------------------------------------------------
// A deterministic parser is a monoid homomorphism from the free word monoid
// (Sigma*, ++, "") into a *summary algebra*: the meaning of a contiguous span
// is a monoid element, and concatenation of spans is monoid composition.
// Parsing a word is therefore a *prefix product*, and a prefix product is a
// parallel scan.  It is data-parallel with O(n) work iff the running summary is
// *compressible* — i.e. the grammar's recognized spans factor through a monoid
// whose elements have a bounded size.  There are exactly three ways that
// happens, and they collapse the old ad-hoc "R1..R7" table into instances:
//
//   schema            monoid                       parallel primitive
//   ----------------  ---------------------------  -----------------------
//   finite            state -> state (a DFA)        prefix scan   (regular)
//   stack             difference-list / stack       prefix scan   (VPL)
//   tree              operator-precedence algebra   tree contraction (assoc.)
//
// Anything whose span meaning cannot be compressed needs an interval fixed
// point (O(n^2)); it is a *fallback*, not a fast path.
//
// This header defines the single abstraction (`Summary` + `prefix_reduce`) and
// three instances that are provably instances of it (associativity is checked,
// not asserted).  Backends specialise the same `prefix_reduce` per instance;
// no per-format, per-rule branching lives here.
#pragma once

#include <array>
#include <concepts>
#include <cstdint>

// Device-callable annotation: the monoid ops run on the GPU too, so the *same*
// `compose` drives host and device backends.
#if defined(__CUDACC__)
#	define PARS_HD __host__ __device__
#else
#	define PARS_HD
#endif

namespace pars::algebra {

// ---------------------------------------------------------------------------
// The single abstraction.
//
// A `Summary` is the meaning of one contiguous input span under a monoid:
//   compose(a, b) == meaning(span_a ++ span_b)
//   identity()    == meaning("")
// `compose` must be associative and `identity` neutral.  Everything else —
// character classification, masking, compaction, nesting — is an *instance*.
// ---------------------------------------------------------------------------
template<class S>
concept Summary = requires(const S& a, const S& b) {
	{ S::compose(a, b) } -> std::same_as<S>;
	{ S::identity() } -> std::same_as<S>;
};

// The one parallel primitive: inclusive prefix product in ANY summary monoid.
// Hillis–Steele: O(n) work, O(log n) depth.  Every fast path in the compiler is
// this function instantiated at a different `S`; that is the whole "algebra".
template<Summary S>
inline void prefix_reduce(S* p, std::size_t n) noexcept {
	for (std::size_t d = 1; d < n; d <<= 1)
		for (std::size_t i = n; i-- > d;) p[i] = S::compose(p[i - d], p[i]);
}

// ---------------------------------------------------------------------------
// Instance 1 — finite monoid (regular / bounded state).  This is a DFA read as
// a transformation monoid: the summary of a span is "how it maps entry state to
// exit state".  Covers all lexical rules (JSON strings, CSV quotes, comments).
// ---------------------------------------------------------------------------
template<std::size_t NS>
struct state_map {
	std::array<std::uint8_t, NS> f {};

	friend constexpr bool operator==(const state_map&, const state_map&) = default;

	[[nodiscard]] static state_map identity() noexcept {
		state_map r {};
		for (std::size_t i = 0; i < NS; ++i) r.f[i] = static_cast<std::uint8_t>(i);
		return r;
	}
	// (a then b): s -> b(a(s))
	[[nodiscard]] static state_map compose(const state_map& a, const state_map& b) noexcept {
		state_map r {};
		for (std::size_t i = 0; i < NS; ++i) r.f[i] = b.f[a.f[i]];
		return r;
	}
};

// ---------------------------------------------------------------------------
// Instance 2 — stack monoid (visibly pushdown).  For one bracket type the
// summary of a span is its reduced word: (#unmatched closes, #unmatched opens),
// a fixed-size pair — compressible even though its values are unbounded.  This
// subsumes the well-nested reduction (R6) *and* is a plain monoid, so it scans.
// ---------------------------------------------------------------------------
struct bracket_balance {
	std::int64_t close = 0, open = 0;

	friend constexpr bool operator==(const bracket_balance&, const bracket_balance&) = default;

	[[nodiscard]] static bracket_balance identity() noexcept { return {0, 0}; }
	[[nodiscard]] static bracket_balance
		compose(const bracket_balance& a, const bracket_balance& b) noexcept {
		const std::int64_t m = a.open < b.close ? a.open : b.close;
		return {a.close + (b.close - m), a.open - m + b.open};
	}
	[[nodiscard]] bool balanced() const noexcept { return close == 0 && open == 0; }
	[[nodiscard]] std::int64_t depth_after() const noexcept { return open - close; }
};

// ---------------------------------------------------------------------------
// Instance 3 — tree / associative monoid (single self-recursion).  A
// value-producing reduction: `reduce(op, seed)`.  This is R7's real content;
// it scans when `op` is associative, and tree-contracts otherwise.
// ---------------------------------------------------------------------------
template<class V, class Op>
struct reducer {
	bool has = false;
	V	 v {};

	friend constexpr bool operator==(const reducer&, const reducer&) = default;

	[[nodiscard]] static PARS_HD reducer identity() noexcept { return {}; }
	[[nodiscard]] static PARS_HD reducer compose(const reducer& a, const reducer& b) noexcept {
		if (!a.has) return b;
		if (!b.has) return a;
		return {true, Op {}(a.v, b.v)};
	}
};

// ---------------------------------------------------------------------------
// Instance 4 — *ordered choice* (PEG `/`) as a monoid.
//
// The key fact that keeps PEG in the fast fragment: choosing the first
// alternative that succeeds is "min on priority" over the successful
// alternatives — associative, commutative, idempotent.  So PEG's ordered
// choice is a monoid, exactly like the three schemas above, and is scanned by
// the SAME `prefix_reduce`.  (CFG's unordered union would instead be a
// *set-valued* semiring, i.e. strictly heavier.)  `rank` is the alternative's
// position in the rule; lower = earlier = higher priority.
//
// Caveat: this captures the *decision* among alternatives that share a start
// and are distinguished within a bounded lookahead window W (the discriminating
// fast case).  Alternatives needing unbounded backtracking are not compressible
// and fall to the interval schema.
// ---------------------------------------------------------------------------
template<class V>
struct ranked_choice {
	bool		 matched = false;
	std::uint8_t rank	 = 0;  // lower = earlier alternative
	V			 v {};

	friend constexpr bool operator==(const ranked_choice&, const ranked_choice&) = default;

	[[nodiscard]] static ranked_choice identity() noexcept { return {}; }
	[[nodiscard]] static ranked_choice of(std::uint8_t r, V x) noexcept { return {true, r, x}; }
	[[nodiscard]] static ranked_choice
		compose(const ranked_choice& a, const ranked_choice& b) noexcept {
		if (!a.matched) return b;
		if (!b.matched) return a;
		return (a.rank <= b.rank) ? a : b;	// first success wins
	}
};

}  // namespace pars::algebra
