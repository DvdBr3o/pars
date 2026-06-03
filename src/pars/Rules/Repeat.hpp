#pragma once

#include "pars/Meta.hpp"
#include "pars/Query.hpp"
#include "pars/Result.hpp"
#include "pars/Rules/Effect.hpp"
#include "pars/Snapshot.hpp"
#include "tl/expected.hpp"

#include <optional>
#include <variant>

namespace pars {
template<typename RuleT>
struct OptionalRule : RuleT {
	template<typename StateT>
	constexpr auto match(StateT&& st) const
		-> Expected<std::optional<rule_value_t<StateT, RuleT>>, std::monostate> {
		if (auto res = this->RuleT::match(std::forward<StateT>(st)))
			return std::move(res.value());
		else
			return std::nullopt;
	}
};

template<typename RuleT>
inline constexpr auto optional(RuleT&& rule) -> OptionalRule<RuleT> {
	return {std::forward<RuleT>(rule)};
}

template<typename RuleT>
inline constexpr auto operator-(RuleT&& rule) -> OptionalRule<RuleT> {
	return {std::forward<RuleT>(rule)};
}

template<typename RuleT>
struct OnceOrMoreRule : RuleT {
	template<typename StateT>
	using Value = std::vector<rule_value_t<StateT, RuleT>>;
	template<typename StateT>
	using Error = rule_error_t<StateT, RuleT>;

	template<typename StateT>
	constexpr auto match(StateT&& st) const -> Expected<Value<StateT>, Error<StateT>> {
		Value<StateT> value;
		if (auto res = this->RuleT::match(st))
			value.emplace_back(std::move(res.value()));
		else
			return tl::make_unexpected(std::move(res.error()));
		while (auto res = this->RuleT::match(st)) value.emplace_back(std::move(res.value()));
		return value;
	}
};

template<typename RuleT>
inline constexpr auto once_or_more(RuleT&& rule) -> OnceOrMoreRule<RuleT> {
	return {std::forward<RuleT>(rule)};
}

template<typename RuleT>
inline constexpr auto operator+(RuleT&& rule) -> OnceOrMoreRule<RuleT> {
	return {std::forward<RuleT>(rule)};
}

template<typename RuleT>
struct RepeatableRule : RuleT {
	template<typename StateT>
	using Value = std::vector<rule_value_t<StateT, RuleT>>;

	template<typename StateT>
	constexpr auto match(StateT&& st) const -> Expected<Value<StateT>, std::monostate> {
		Value<StateT> value;
		while (auto res = this->RuleT::match(st)) value.emplace_back(std::move(res.value()));
		return value;
	}
};

template<typename RuleT>
inline constexpr auto repeatable(RuleT&& rule) -> RepeatableRule<RuleT> {
	return {std::forward<RuleT>(rule)};
}

template<typename RuleT>
inline constexpr auto operator*(RuleT&& rule) -> RepeatableRule<RuleT> {
	return {std::forward<RuleT>(rule)};
}

}  // namespace pars