#pragma once

#include "pars/Meta.hpp"

#include <utility>

namespace pars {
template<typename T>
concept QueryTagC = requires { typename T::Queryable; };

template<QueryTagC QueryT>
struct QueryState {
	QueryT::Queryable queryable;

	constexpr auto	  query(this auto&& self, QueryT) -> decltype(auto) {
		return (self.QueryState::queryable);
	}
};

template<typename Self, typename QueryableT>
struct QueryTag {
	using Queryable = QueryableT;

	template<typename QueryableTT>
	constexpr auto operator()(QueryableTT&& queryable) const -> decltype(auto) {
		return query(std::forward<QueryableTT>(queryable), Self {});
	}
};

template<typename T, typename QueryT>
inline constexpr auto query(T&& t, QueryT q) -> decltype(auto)
	requires requires { t.query(q); }
{
	return (t.query(q));
}

template<typename T, typename QueryT>
using query_t = decltype(query(std::declval<T>(), std::declval<QueryT>()));

template<typename... Ts>
using required_queries_of_rules_t =
	tuple_cup_t<typename std::remove_cvref_t<Ts>::required_queries_type...>;

}  // namespace pars
