// pars/model/cursor.hpp — generic random-access access to the structural index.
//
// The streaming hook (visit.hpp) replays the index once; this header exposes the
// same index for *random access*, which is what query-driven, lazy value
// materialisation needs (cf. cuJSON's host iterator).  It is deliberately small
// and format-agnostic: it knows byte offsets and bracket partners, nothing about
// JSON/TOML/... semantics.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace pars::model {

struct structural_cursor {
	const std::int32_t*		   pos	= nullptr;	// byte offset of structural token i
	const std::int32_t*		   pair = nullptr;	// partner *index* of token i, or -1
	const std::uint8_t*		   base = nullptr;	// input bytes
	std::size_t				   n	= 0;		// number of structural tokens

	[[nodiscard]] bool		   valid(std::size_t i) const noexcept { return i < n; }

	[[nodiscard]] std::int32_t off(std::size_t i) const noexcept { return pos[i]; }

	[[nodiscard]] char ch(std::size_t i) const noexcept { return static_cast<char>(base[pos[i]]); }

	[[nodiscard]] std::int32_t match_index(std::size_t i) const noexcept {
		return pair ? pair[i] : std::int32_t {-1};
	}

	// The run of non-structural bytes immediately before token i.
	[[nodiscard]] std::size_t gap_begin(std::size_t i) const noexcept {
		return i ? static_cast<std::size_t>(pos[i - 1]) + 1 : 0;
	}

	[[nodiscard]] std::size_t gap_end(std::size_t i) const noexcept {
		return static_cast<std::size_t>(pos[i]);
	}

	[[nodiscard]] std::string_view gap(std::size_t i) const noexcept {
		const std::size_t b = gap_begin(i);
		return {reinterpret_cast<const char*>(base) + b, gap_end(i) - b};
	}
};

// Trim ASCII whitespace from both ends.
[[nodiscard]] inline std::string_view trim(std::string_view s) noexcept {
	while (!s.empty()
		   && (s.front() == ' ' || s.front() == '\t' || s.front() == '\n' || s.front() == '\r'))
		s.remove_prefix(1);
	while (!s.empty()
		   && (s.back() == ' ' || s.back() == '\t' || s.back() == '\n' || s.back() == '\r'))
		s.remove_suffix(1);
	return s;
}

}  // namespace pars::model
