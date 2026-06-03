#pragma once

#include "pars/Meta.hpp"
#include "pars/Query.hpp"

#include <tuple>
#include <utility>

namespace pars {
template<typename FnT>
struct StateEffectRule {
	FnT fn;

	template<typename StateT>
	constexpr auto match(StateT&& st) const {
		return std::invoke(fn, std::forward<StateT>(st));
	}

	inline friend constexpr auto operator==(const StateEffectRule& l, const StateEffectRule& r)
		-> bool {
		return true;
	}
};

template<typename FnT>
inline constexpr auto state_effect(FnT&& fn) -> StateEffectRule<FnT> {
	return {std::forward<FnT>(fn)};
}

struct query_effect_impl_t {};

inline constexpr auto query_effect_impl = query_effect_impl_t {};

template<typename FnT, typename... QueryTs>
struct QueryEffectRule : FnImpl<query_effect_impl_t, FnT> {
	using required_queries_type = std::tuple<QueryTs...>;

	template<typename StateT>
	constexpr auto match(StateT&& st) const -> decltype(auto) {
		auto&& state = std::forward<StateT>(st);
		return std::apply(
			this->fn(query_effect_impl),
			std::forward_as_tuple(query(state, QueryTs {})...)
		);
	}
};

template<typename... QueryTs, typename FnT>
inline constexpr auto query_effect(FnT&& fn) -> QueryEffectRule<FnT, QueryTs...> {
	return {std::forward<FnT>(fn)};
}

template<typename... QueryTs, typename FnT>
inline constexpr auto query_effect(QueryTs&&... query, FnT&& fn)
	-> QueryEffectRule<FnT, QueryTs...> {
	return {std::forward<FnT>(fn)};
}

template<auto... Queries, typename FnT>
inline constexpr auto query_effect(FnT&& fn) -> QueryEffectRule<FnT, decltype(Queries)...> {
	return {std::forward<FnT>(fn)};
}

template<Like<std::tuple> QueryTs, typename FnT>
inline constexpr auto query_effec1t(FnT&& fn)
	-> templ_from_type_tuple_t<QueryEffectRule, tuple_cup_t<std::tuple<FnT>, QueryTs>> {
	return {std::forward<FnT>(fn)};
}

template<typename RuleT, typename FnT>
struct ValueEffectRule {
	RuleT rule;
	FnT	  fn;

	template<typename StateT>
	constexpr auto match(StateT&& st) const {
		auto res = rule.match(std::forward<StateT>(st));
		if (res) {
			std::invoke(fn, std::forward<StateT>(st), value_of(res));
			return res;
		} else
			return error_of(res);
	}

	inline friend constexpr auto operator==(const ValueEffectRule& l, const ValueEffectRule& r)
		-> bool {
		return l.rule == r.rule;
	}
};

template<typename FnT>
struct ValueEffectFunctor {
	FnT fn;
};

template<typename RuleT, typename FnT>
inline constexpr auto operator%=(RuleT&& rule, const ValueEffectFunctor<FnT>& functor)
	-> ValueEffectRule<RuleT, FnT> {
	return {std::forward<RuleT>(rule), std::forward<FnT>(functor.fn)};
}

template<typename FnT>
inline constexpr auto value_effect(FnT&& f) -> ValueEffectFunctor<FnT> {
	return {std::forward<FnT>(f)};
}

}  // namespace pars
