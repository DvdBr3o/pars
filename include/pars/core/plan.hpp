// pars/core/plan.hpp — the NEW unified IR (destructive replacement of the old
// `scan_plan`).
//
// A plan is a list of *cells*.  A cell is one summary generator over bytes:
//   schema::finite   a finite transducer   (lexical rules, quoting, escapes)
//   schema::stack    a bracket pair        (visibly-pushdown recursion)
//   schema::tree     an associative op     (single self-recursion)
//   schema::choice   ordered alternatives  (PEG `/`)
//   schema::interval non-compressible residue (fallback, not a fast path)
//
// Everything the old IR kept as separate arrays (punctuation / brackets /
// modes / descs) is now either a `cell` or an *atom*; the executor has exactly
// one loop: build each byte's summary element, `prefix_reduce`, read the masks
// off the running summary.  No per-rule branching.
#pragma once

#include <array>
#include <cstdint>

namespace pars::core {

enum class schema : std::uint8_t { finite, stack, tree, choice, interval };

// the provable factorization of a finite transducer (a performance choice, not
// a semantic one): affine (prefix escape), doubled (CSV ""), comment, or the
// general table.
enum class factor : std::uint8_t { affine, doubled, comment, table, fallback };

constexpr std::size_t kMaxCells	  = 8;
constexpr std::size_t kMaxStates  = 8;
constexpr std::size_t kMaxClasses = 8;

struct cell {
	schema sch = schema::finite;

	// byte -> class
	std::array<std::uint8_t, 256> rep_of {};
	std::uint8_t				  n_classes = 0;

	// finite / choice: class x state -> next state, and the per-transition mask
	// bit ("this byte is consumed by an in-progress token")
	std::uint8_t												   start	= 0;
	std::uint8_t												   n_states = 0;
	std::array<std::array<std::uint8_t, kMaxClasses>, kMaxStates> trans {};
	std::array<std::array<std::uint8_t, kMaxClasses>, kMaxStates> masked {};

	// factorization + delimiter/escape (for the affine/doubled fast path)
	factor		 factor_ = factor::table;
	char		 delim	 = 0, escape = 0;

	// stack: the bracket endpoints this cell matches
	char open = 0, close = 0;

	// tree: the associative operator literal (one of '+', '*', '&', '|')
	char op = 0;

	bool ok = true;

	friend constexpr bool operator==(const cell&, const cell&) = default;
};

struct plan {
	std::array<cell, kMaxCells> cells {};
	std::uint8_t				n_cells = 0;

	// structural atoms (':' ',' ';' ...) — a byte is structural iff it is an
	// atom or a bracket endpoint, and it is not masked by any cell.
	std::array<char, kMaxCells> atoms {};
	std::uint8_t				n_atoms = 0;

	std::array<char, kMaxCells> opens {};
	std::array<char, kMaxCells> closes {};
	std::uint8_t				n_brackets = 0;

	friend constexpr bool operator==(const plan&, const plan&) = default;
};

}  // namespace pars::core
