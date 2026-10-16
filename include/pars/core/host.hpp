// pars/core/host.hpp — the reference executor for the unified IR.
//
// One loop: for each byte, read the running summary of every finite cell (a
// `prefix_reduce` over that cell's transition monoid), OR the per-transition
// mask bits, and emit the byte iff it is a structural atom and unmasked.  This
// is the whole host backend; there is no per-rule code, only per-schema.
#pragma once

#include "pars/algebra/summary.hpp"
#include "pars/core/plan.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace pars::core {

struct index {
	std::vector<std::uint32_t> pos;
	std::vector<char>		   ch;
};

template<plan P>
[[nodiscard]] inline std::array<bool, 256> structural_table() noexcept {
	std::array<bool, 256> t {};
	for (std::uint8_t a = 0; a < P.n_atoms; ++a) t[(unsigned char)P.atoms[a]] = true;
	for (std::uint8_t b = 0; b < P.n_brackets; ++b) {
		t[(unsigned char)P.opens[b]]	= true;
		t[(unsigned char)P.closes[b]]	= true;
	}
	return t;
}

template<plan P>
[[nodiscard]] index scan(const std::uint8_t* data, std::size_t n) {
	using SM = algebra::state_map<kMaxStates>;

	// per finite cell: the prefix product of its per-byte transition monoid
	std::array<std::vector<SM>, kMaxCells> pref;
	for (std::uint8_t d = 0; d < P.n_cells; ++d) {
		const cell& c = P.cells[d];
		if (c.sch != schema::finite && c.sch != schema::choice)
			continue;
		pref[d].resize(n);
		for (std::size_t i = 0; i < n; ++i) {
			const std::uint8_t k = c.rep_of[data[i]];
			SM				   e = SM::identity();
			for (std::size_t s = 0; s < c.n_states; ++s) e.f[s] = c.trans[s][k];
			pref[d][i] = e;
		}
		algebra::prefix_reduce(pref[d].data(), n);
	}

	const auto tbl = structural_table<P>();
	index	   out;
	for (std::size_t i = 0; i < n; ++i) {
		bool masked = false;
		for (std::uint8_t d = 0; d < P.n_cells; ++d) {
			const cell& c = P.cells[d];
			if (c.sch != schema::finite && c.sch != schema::choice)
				continue;
			const std::uint8_t st = (i == 0) ? c.start : pref[d][i - 1].f[c.start];
			if (c.masked[st][c.rep_of[data[i]]])
				masked = true;
		}
		if (masked)
			continue;
		const unsigned char c = data[i];
		if (tbl[c]) {
			out.pos.push_back(static_cast<std::uint32_t>(i));
			out.ch.push_back(static_cast<char>(c));
		}
	}
	return out;
}

}  // namespace pars::core
