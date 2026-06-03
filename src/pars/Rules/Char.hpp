#pragma once

#include "Sequential.hpp"
#include "pars/Rules/Effect.hpp"
#include "pars/Result.hpp"
#include "pars/Rules/Transform.hpp"
#include "pars/Utf.hpp"
#include "pars/Meta.hpp"
#include "utf8/cpp20.h"

#include <algorithm>
#include <utility>
#include <array>
#include <concepts>
#include <format>
#include <ranges>

namespace pars {
struct CharMismatchError {
	char32_t expected;
	char32_t mismatched;
};

template<size_t N>
struct CharSetMismatchError {
	std::array<char32_t, N> expected;
	char32_t				mismatched;
};

struct CharRangeMismatchError {
	struct {
		char32_t lo;
		char32_t hi;
	} expected;

	char32_t mismatched;
};

template<typename PredT, typename ErrT>
inline constexpr auto single_char_rule(PredT&& pred, ErrT&& err) {
	return	//
		query_effect<u8::query_text_cursor>(
			[predicate = std::forward<PredT>(pred), error_builder = std::forward<ErrT>(err)](
				u8::Cursor& text_cursor
			) -> Expected<char32_t, decltype(std::declval<ErrT>()(std::declval<char32_t>()))> {
				if (const auto cnxt = text_cursor.peek())
					if (std::invoke(predicate, cnxt)) {
						text_cursor.bump();
						return cnxt;
					} else
						return tl::make_unexpected(std::invoke(error_builder, cnxt));
				else
					return tl::make_unexpected(std::invoke(error_builder, u8::Cursor::eof));
			}
		);
}

inline constexpr auto c(char32_t ch) {
	return single_char_rule(
		[=](char32_t c) { return c == ch; },
		[=](char32_t c) -> CharMismatchError {
			return {
				.expected	= ch,
				.mismatched = c,
			};
		}
	);
}

inline constexpr auto cset(std::convertible_to<char32_t> auto... cs) {
	return single_char_rule(
		[=](char32_t c) { return ((c == cs) || ...); },
		[=](char32_t c)->CharSetMismatchError<sizeof...(cs)> {
			auto expected = std::array<char32_t, sizeof...(cs)> {cs...};
			std::ranges::sort(expected);
			return {
				.expected	= std::move(expected),
				.mismatched = c,
			};
		}
	);
}

inline constexpr auto cran(char32_t lo, char32_t hi) {
	return single_char_rule(
		[=](char32_t c) { return lo <= c && c <= hi; },
		[=](char32_t c) -> CharRangeMismatchError {
			return {
				.expected	= {lo, hi},
				.mismatched = c,
			};
		}
	);
}

template<size_t N>
inline constexpr auto cstr(const char32_t (&sv)[N]) {
	return [&sv]<size_t... Is>(std::index_sequence<Is...>) {
		return sequential(c(sv[Is])...);
	}(std::make_index_sequence<N - 1>());  // N - 1 => discarding trailing '\0' in string literals.
}

template<typename StateT, RuleC<StateT> RuleT>
inline constexpr auto operator>>(RuleT&& rule, char32_t ch) {
	return rule >> c(ch);
}

template<typename RuleT>
inline constexpr auto operator>>(char32_t ch, RuleT&& rule) {
	return c(ch) >> rule;
}

}  // namespace pars

namespace pars {
inline auto to_string(const CharMismatchError& err) -> std::string {
	return std::format(
		"CharMismatchError(expected: '{}', mismatched: '{}')",
		utf8::utf32to8({err.expected}),
		utf8::utf32to8({err.mismatched})
	);
}

template<size_t N>
inline auto to_string(const CharSetMismatchError<N>& err) -> std::string {
	using namespace std::views;
	return std::format(
		"CharMismatchError(expected: '{}', mismatched: '{}')",
		err.expected
			| transform([](char32_t c) { return std::format("'{}', ", utf8::utf32to8({c})); }),
		utf8::utf32to8({err.mismatched})
	);
}

inline auto to_string(const CharRangeMismatchError& err) -> std::string {
	return std::format(
		"CharMismatchError(expected: '{}' ~ '{}', mismatched: '{}')",
		utf8::utf32to8({err.expected.lo}),
		utf8::utf32to8({err.expected.hi}),
		utf8::utf32to8({err.mismatched})
	);
}
}  // namespace pars