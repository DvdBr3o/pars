// pars/model/classify.hpp — scan-algebra primitive R1: terminals -> byte classes.
//
// Purely derived from the lowered scan plan: no format knowledge.
#pragma once

#include "pars/plan.hpp"

#include <array>
#include <cstdint>

namespace pars::model {

namespace cbit {
constexpr std::uint32_t structural = 1u << 0;
constexpr std::uint32_t open	   = 1u << 1;
constexpr std::uint32_t close	   = 1u << 2;

constexpr std::uint32_t mode_delim(std::size_t i) noexcept {
	return 1u << (3 + 2 * i);
}

constexpr std::uint32_t mode_escape(std::size_t i) noexcept {
	return 1u << (4 + 2 * i);
}

constexpr std::uint32_t comment(std::size_t i) noexcept {
	return 1u << (19 + i);
}
}  // namespace cbit

struct classifier {
	std::array<std::uint32_t, 256>		  bits {};

	[[nodiscard]] constexpr std::uint32_t of(unsigned char c) const noexcept { return bits[c]; }

	constexpr void add(unsigned char c, std::uint32_t b) noexcept { bits[c] |= b; }
};

[[nodiscard]] constexpr classifier build_classifier(const scan_plan& S) noexcept {
	classifier t {};
	for (std::size_t i = 0; i < S.n_punctuation; ++i)
		t.add(static_cast<unsigned char>(S.punctuation[i]), cbit::structural);
	for (std::size_t i = 0; i < S.n_brackets; ++i) {
		t.add(static_cast<unsigned char>(S.brackets[i].open), cbit::structural | cbit::open);
		t.add(static_cast<unsigned char>(S.brackets[i].close), cbit::structural | cbit::close);
	}
	for (std::size_t i = 0; i < S.n_modes; ++i) {
		t.add(static_cast<unsigned char>(S.modes[i].delim), cbit::mode_delim(i));
		if (S.modes[i].escape && S.modes[i].escape != S.modes[i].delim)
			t.add(static_cast<unsigned char>(S.modes[i].escape), cbit::mode_escape(i));
	}
	for (std::size_t i = 0; i < S.n_comments; ++i)
		t.add(static_cast<unsigned char>(S.comments[i]), cbit::comment(i));
	return t;
}

}  // namespace pars::model
