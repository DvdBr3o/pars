#pragma once

#include "pars/Query.hpp"
#include "pars/Result.hpp"
#include "pars/Meta.hpp"
#include "pars/Snapshot.hpp"

#include <tl/expected.hpp>

#include <tuple>
#include <utility>

namespace pars {
template<typename... RuleTs>
struct ChoiceRule : std::tuple<RuleTs...> {
	using std::tuple<RuleTs...>::tuple;

	template<typename StateT>
	struct Error : std::tuple<rule_error_t<StateT, RuleTs>...> {
		using std::tuple<rule_error_t<StateT, RuleTs>...>::tuple;

		inline friend auto to_string(const Error& error) -> std::string {
			std::string res	  = "ChoiceError[";
			bool		first = true;
			std::apply(
				[&](const auto&... errs) {
					(
						[&] {
							if (!first)
								res += ", ";
							first = false;
							res += to_string(errs);
						}(),
						...);
				},
				static_cast<const std::tuple<rule_error_t<StateT, RuleTs>...>&>(error)
			);
			res += "]";
			return res;
		}
	};

	template<typename StateT>
	constexpr auto match(StateT&& st) const
		-> Expected<unique_type_variant_t<rule_value_t<StateT, RuleTs>...>, Error<StateT>> {
		using Value	 = unique_type_variant_t<rule_value_t<StateT, RuleTs>...>;
		using Error	 = Error<StateT>;
		using Result = Expected<Value, Error>;

		Value value;
		Error error;

		if ([&]<size_t... Is>(std::index_sequence<Is...>) {
				return ([&]() {
					auto g	 = snap_guard(st).template of<required_queries_of_rules_t<RuleTs>>();
					auto res = std::get<Is>(*this).match(std::forward<StateT>(st));
					if (res) {
						value = std::move(res.value());
						return true;
					} else {
						std::get<Is>(error) = std::move(res.error());
						return g.rolled(false);
					}
				}() || ...);
			}(std::make_index_sequence<sizeof...(RuleTs)>())) {
			return value;
		} else {
			return tl::make_unexpected(std::move(error));
		}
	}

	template<typename Self>
	constexpr auto as_tuple(this Self&& self) -> decltype(auto) {
		return forward_like<Self>(static_cast<std::tuple<RuleTs...>&&>(self));
	}
};

template<typename... RuleTs>
ChoiceRule(RuleTs&&...) -> ChoiceRule<RuleTs...>;

template<typename... RuleTs>
inline constexpr auto choice(RuleTs&&... rules) -> ChoiceRule<RuleTs...> {
	return {std::forward<RuleTs>(rules)...};
}

template<typename... Ls, typename... Rs>
inline constexpr auto operator|(ChoiceRule<Ls...>&& l, ChoiceRule<Rs...>&& r)
	-> ChoiceRule<Ls..., Rs...> {
	return std::apply(
		[&](auto&&... ls) {
			return std::apply(
				[&](auto&&... rs) {
					return ChoiceRule<Ls..., Rs...> {
						std::forward_like<decltype(l)>(ls)...,
						std::forward_like<decltype(r)>(rs)...
					};
				},
				r.as_tuple()
			);
		},
		l.as_tuple()
	);
}

template<typename L, typename... Rs>
inline constexpr auto operator|(L&& l, ChoiceRule<Rs...>&& r) -> ChoiceRule<L, Rs...> {
	return std::apply(
		[&](auto&&... rs) {
			return ChoiceRule<L, Rs...> {std::forward<L>(l), std::forward_like<decltype(r)>(rs)...};
		},
		r.as_tuple()
	);
}

template<typename... Ls, typename R>
inline constexpr auto operator|(ChoiceRule<Ls...>&& l, R&& r) -> ChoiceRule<Ls..., R> {
	return std::apply(
		[&](auto&&... ls) {
			return ChoiceRule<Ls..., R> {std::forward_like<decltype(l)>(ls)..., std::forward<R>(r)};
		},
		l.as_tuple()
	);
}

template<typename L, typename R>
inline constexpr auto operator|(L&& l, R&& r) -> ChoiceRule<L, R> {
	return {std::forward<L>(l), std::forward<R>(r)};
}

}  // namespace pars
