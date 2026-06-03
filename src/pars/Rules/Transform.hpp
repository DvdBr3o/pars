#pragma once

#include "pars/Meta.hpp"
#include "tl/expected.hpp"

#include <concepts>
#include <type_traits>
#include <utility>

namespace pars {
template<typename RuleT, typename FnT>
struct ValueTransformRule {
	RuleT rule;
	FnT	  fn;

	template<typename StateT>
	using OriginValueT = rule_value_t<StateT, RuleT>;
	template<typename StateT>
	inline static constexpr auto origin_value_like_tuple_v =
		is_like_v<OriginValueT<StateT>, std::tuple>
		|| requires(OriginValueT<StateT> v) { to_tuple(v); };
	template<typename StateT>
	using InvokeValue = std::invoke_result_t<FnT, OriginValueT<StateT>>;
	template<typename StateT>
	using ApplyValue = apply_result_t<FnT, OriginValueT<StateT>>;
	template<typename StateT>
	using Error = rule_error_t<StateT, RuleT>;

	template<typename StateT>
	constexpr auto match(StateT&& st) const -> Expected<ApplyValue<StateT>, Error<StateT>>
		requires origin_value_like_tuple_v<StateT>
	{
		auto res = rule.match(std::forward<StateT>(st));
		if (res)
			return std::apply(
				[&]<typename... ResTs>(ResTs&&... res) {
					return std::invoke(fn, std::forward<ResTs>(res)...);
				},
				std::move(value_of(res))
			);
		else
			return tl::make_unexpected(std::move(error_of(res)));
	}

	template<typename StateT>
	constexpr auto match(StateT&& st) const -> Expected<InvokeValue<StateT>, Error<StateT>>
		requires(!origin_value_like_tuple_v<StateT>)
	{
		auto res = rule.match(std::forward<StateT>(st));
		if (res)
			std::invoke(fn, std::move(value_of(res)));
		else
			return tl::make_unexpected(std::move(error_of(res)));
	}

	inline friend constexpr auto operator==(
		const ValueTransformRule& l, const ValueTransformRule& r
	) -> bool {
		return l.rule == r.rule;
	}
};	// namespace pars

template<typename FnT>
struct ValueTransformFunctor {
	FnT fn;
};

template<typename RuleT, typename FnT>
inline constexpr auto operator%=(RuleT&& rule, ValueTransformFunctor<FnT>&& functor)
	-> ValueTransformRule<RuleT, FnT> {
	return {std::forward<RuleT>(rule), std::forward<FnT>(functor.fn)};
}

template<typename RuleT, typename FnT>
inline constexpr auto operator^(RuleT&& rule, ValueTransformFunctor<FnT>&& functor)
	-> ValueTransformRule<RuleT, FnT> {
	return {std::forward<RuleT>(rule), std::forward<FnT>(functor.fn)};
}

template<typename FnT>
inline constexpr auto value_to(FnT&& fn) -> ValueTransformFunctor<FnT> {
	return {std::forward<FnT>(fn)};
}

template<typename RuleT, typename FnT>
struct ResultTransformRule : RuleT {
	FnT fn;

	template<typename StateT>
	constexpr auto match(StateT&& st) const {
		return std::invoke(fn, this->RuleT::match(std::forward<StateT>(st)));
	}

	inline friend constexpr auto operator==(
		const ResultTransformRule& l, const ResultTransformRule& r
	) -> bool {
		return l.rule == r.rule;
	}
};

template<typename FnT>
struct ResultTransformFunctor {
	FnT fn;
};

template<typename RuleT, typename FnT>
inline constexpr auto operator%=(RuleT&& rule, ResultTransformFunctor<FnT>&& functor)
	-> ResultTransformRule<RuleT, FnT> {
	return {std::forward<RuleT>(rule), std::forward<FnT>(functor.fn)};
}

template<typename RuleT, typename FnT>
inline constexpr auto operator^(RuleT&& rule, ResultTransformFunctor<FnT>&& functor)
	-> ResultTransformRule<RuleT, FnT> {
	return {std::forward<RuleT>(rule), std::forward<FnT>(functor.fn)};
}

template<typename FnT>
inline constexpr auto result_to(FnT&& fn) -> ResultTransformFunctor<FnT> {
	return {std::forward<FnT>(fn)};
}

}  // namespace pars
