#pragma once

#include <concepts>

namespace pars {
template<typename T, typename MatchableT>
concept RuleMatchableC = requires(T t, MatchableT matchable) { t.match(matchable); };

template<typename T>
concept RuleComparableC = requires(const T& tl, const T& tr) {
	{ tl == tr } -> std::convertible_to<bool>;
};

template<typename T, typename MatchableT>
concept RuleC = RuleMatchableC<T, MatchableT> && RuleComparableC<T>;
}  // namespace pars