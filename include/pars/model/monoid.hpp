// pars/model/monoid.hpp — scan-algebra primitive R4's algebraic core.
//
// The whole "branch masking" story reduces to: a stateful per-byte recurrence
// is a prefix product in a transformation monoid, and a prefix product is a
// parallel scan.  A 1-bit flag is the affine case  x' = (a & x) ^ b, whose
// prefix is simdjson's prefix_xor / cuJSON's escape scan.
#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace pars::model {

// ---- transformation monoid over a finite state set ------------------------
template<std::size_t NS>
using state_map = std::array<std::uint8_t, NS>;

template<std::size_t NS>
[[nodiscard]] constexpr state_map<NS> identity_map() noexcept {
	state_map<NS> m {};
	for (std::size_t i = 0; i < NS; ++i) m[i] = static_cast<std::uint8_t>(i);
	return m;
}

template<std::size_t NS>
[[nodiscard]] constexpr state_map<NS> compose(
	const state_map<NS>& a, const state_map<NS>& b
) noexcept {
	state_map<NS> r {};
	for (std::size_t i = 0; i < NS; ++i) r[i] = b[a[i]];
	return r;
}

// Inclusive Hillis-Steele prefix-compose, in place.
template<std::size_t NS>
inline void prefix_compose(state_map<NS>* p, std::size_t n) noexcept {
	for (std::size_t d = 1; d < n; d <<= 1)
		for (std::size_t i = n; i-- > d;) p[i] = compose(p[i - d], p[i]);
}

// ---- affine 1-bit monoid --------------------------------------------------
struct affine {
	std::uint8_t a = 1, b = 0;	// x' = (a & x) ^ b
};

[[nodiscard]] constexpr affine affine_compose(affine f, affine g) noexcept {
	return affine {
		static_cast<std::uint8_t>(f.a & g.a),
		static_cast<std::uint8_t>((f.b & g.a) ^ g.b)
	};
}

inline void prefix_compose(affine* p, std::size_t n) noexcept {
	for (std::size_t d = 1; d < n; d <<= 1)
		for (std::size_t i = n; i-- > d;) p[i] = affine_compose(p[i - d], p[i]);
}

[[nodiscard]] inline std::uint64_t prefix_xor64(std::uint64_t x) noexcept {
	x ^= x << 1;
	x ^= x << 2;
	x ^= x << 4;
	x ^= x << 8;
	x ^= x << 16;
	x ^= x << 32;
	return x;
}

}  // namespace pars::model
