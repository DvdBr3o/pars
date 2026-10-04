// pars/model/nest.hpp — scan-algebra primitive R6: well-nested (Dyck) reduction.
//
// The naive stack loop is replaced by: depth = prefix sum of (+1 open, -1
// close); stable-sort bracket tokens by depth; pair adjacent tokens; validate
// types.  This is the stack-free, data-parallel matching cuJSON uses.
#pragma once

#include "pars/plan.hpp"

#include <algorithm>
#include <cstdint>
#include <numeric>
#include <vector>

namespace pars::model {

struct pair_table {
	std::vector<std::int32_t> pair;	 // per-bracket-index partner, -1 if none
	bool					  balanced	  = true;
	bool					  well_formed = true;
	int						  max_depth	  = 0;
};

[[nodiscard]] inline bool is_open_char(char c, const scan_plan& S) {
	for (std::size_t i = 0; i < S.n_brackets; ++i)
		if (S.brackets[i].open == c)
			return true;
	return false;
}

[[nodiscard]] inline bool is_close_char(char c, const scan_plan& S) {
	for (std::size_t i = 0; i < S.n_brackets; ++i)
		if (S.brackets[i].close == c)
			return true;
	return false;
}

[[nodiscard]] inline bool match_brackets(char o, char c, const scan_plan& S) {
	for (std::size_t i = 0; i < S.n_brackets; ++i)
		if (S.brackets[i].open == o)
			return S.brackets[i].close == c;
	return false;
}

template<scan_plan S>
[[nodiscard]] pair_table
	pair_brackets(const std::vector<char>& bchar, const std::vector<std::uint32_t>& /*bpos*/) {
	pair_table		  t;
	const std::size_t n = bchar.size();
	t.pair.assign(n, -1);
	if (n == 0)
		return t;

	// depth (inclusive prefix sum of +-1)
	std::vector<std::int32_t> bal(n);
	int						  run = 0;
	for (std::size_t i = 0; i < n; ++i) {
		run += is_open_char(bchar[i], S) ? 1 : -1;
		bal[i] = run;
	}
	t.max_depth = 0;
	int min_bal = 0;
	for (std::size_t i = 0; i < n; ++i) {
		t.max_depth = std::max(t.max_depth, bal[i]);
		min_bal		= std::min(min_bal, bal[i]);
	}
	t.balanced = (min_bal >= 0) && (bal[n - 1] == 0);

	// stable sort by depth key: open uses bal-1, close uses bal
	std::vector<std::int32_t> order(n);
	std::iota(order.begin(), order.end(), 0);
	std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
		const int ka = is_open_char(bchar[a], S) ? bal[a] - 1 : bal[a];
		const int kb = is_open_char(bchar[b], S) ? bal[b] - 1 : bal[b];
		return ka < kb;
	});

	for (std::size_t k = 0; k + 1 < n; k += 2) {
		const int a = order[k], b = order[k + 1];
		if (is_open_char(bchar[a], S) && is_close_char(bchar[b], S)
			&& match_brackets(bchar[a], bchar[b], S)) {
			t.pair[a] = b;
			t.pair[b] = a;
		} else {
			t.well_formed = false;
		}
	}
	if (n % 2 != 0)
		t.well_formed = false;
	t.well_formed = t.well_formed && t.balanced;
	return t;
}

}  // namespace pars::model
