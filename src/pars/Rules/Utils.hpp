#pragma once

#include "pars/Rules/Transform.hpp"

#include <utility>

namespace pars {
inline constexpr auto embraced(char32_t l, char32_t r) {
	return [=]<typename RuleT>(RuleT&& rule) constexpr {
		return c(l) >> std::forward<RuleT>(rule) >> c(r)  //
			%= value_to([](auto&&, auto&& r, auto&&) { return std::forward<decltype(r)>(r); });
	};
}

inline constexpr auto parenthesised = embraced('(', ')');
inline constexpr auto braced		= embraced('[', ']');
inline constexpr auto bracketed		= embraced('{', '}');

}  // namespace pars