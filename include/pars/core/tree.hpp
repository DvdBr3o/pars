// pars/core/tree.hpp — the *tree* schema (R7) as a first-class parallel op.
//
// This is the primitive the old algebra lacked and that PEG needs for
// non-well-nested recursion (`e op e`, `A (op A)*`, ...).  The point of the
// unified model is that it needs no new machinery: a single self-recursion with
// an associative operator is a summary monoid (`algebra::reducer`), and the
// SAME `prefix_reduce` folds the leaves in O(log n) depth.  The tokens delimited
// by the structural index are the leaves; the operator is the monoid.
//
//   grammar:  expr ::= term (op term)*        (op associative)
//   schema :  tree
//   meaning:  reduce(op, term...)             computed by prefix_reduce
#pragma once

#include "pars/algebra/summary.hpp"
#include "pars/core/plan.hpp"

#include <cstdint>
#include <vector>

namespace pars::core {

struct add_i64 {
	PARS_HD std::int64_t operator()(std::int64_t a, std::int64_t b) const { return a + b; }
};
struct mul_i64 {
	PARS_HD std::int64_t operator()(std::int64_t a, std::int64_t b) const { return a * b; }
};
struct min_i64 {
	PARS_HD std::int64_t operator()(std::int64_t a, std::int64_t b) const { return a < b ? a : b; }
};
struct max_i64 {
	PARS_HD std::int64_t operator()(std::int64_t a, std::int64_t b) const { return a > b ? a : b; }
};

// Reduce all integer tokens of the input (maximal digit runs) with a monoid op.
// Operates on the structural leaves directly; `Op` must be associative (checked
// by the algebra-core fuzz for `reducer`).
template<class Op>
[[nodiscard]] inline std::int64_t reduce_ints(const std::uint8_t* base, std::size_t n) {
	using R = algebra::reducer<std::int64_t, Op>;

	std::vector<R> p;
	p.reserve(n / 8 + 1);
	std::size_t i = 0;
	while (i < n) {
		if (base[i] >= '0' && base[i] <= '9') {
			std::int64_t v = 0;
			while (i < n && base[i] >= '0' && base[i] <= '9') {
				v = v * 10 + static_cast<std::int64_t>(base[i] - '0');
				++i;
			}
			p.push_back({true, v});
		} else {
			++i;
		}
	}
	if (p.empty())
		return 0;
	// parallel prefix product; the fold of the whole span is the last element
	algebra::prefix_reduce(p.data(), p.size());
	return p.back().v;
}

// Drive the tree schema from a lowered plan: find the `tree` cell and fold the
// integer leaves with its operator.
template<plan P>
[[nodiscard]] inline std::int64_t reduce(const std::uint8_t* base, std::size_t n) {
	char op = 0;
	for (std::uint8_t d = 0; d < P.n_cells; ++d)
		if (P.cells[d].sch == schema::tree)
			op = P.cells[d].op;
	if (op == '+')
		return reduce_ints<add_i64>(base, n);
	if (op == '*')
		return reduce_ints<mul_i64>(base, n);
	if (op == '&')
		return reduce_ints<min_i64>(base, n);
	if (op == '|')
		return reduce_ints<max_i64>(base, n);
	return 0;
}

}  // namespace pars::core
