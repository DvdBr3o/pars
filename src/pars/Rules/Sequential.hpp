#pragma once

#include "pars/Query.hpp"
#include "pars/Result.hpp"
#include "pars/Meta.hpp"

#include <tl/expected.hpp>

#include <format>
#include <tuple>
#include <utility>
#include <variant>

namespace pars {
template<typename... RuleTs>
struct SequentialRule : std::tuple<RuleTs...> {
	using std::tuple<RuleTs...>::tuple;

	using required_queries_type = required_queries_of_rules_t<RuleTs...>;

	template<typename StateT>
	struct Error {
		using Raw = templ_from_type_tuple_t<
			std::variant, tuple_cup_t<std::tuple<rule_error_t<StateT, RuleTs>>...>>;

		size_t			   pos = 0;
		Raw				   error;

		inline friend auto to_string(const Error& err) -> std::string {
			return std::format("SequentialError(pos: {}, err: {})", err.pos, to_string(err.error));
		}
	};

	template<typename StateT>
	constexpr auto match(StateT&& st) const -> Expected<
		std::tuple<rule_value_t<std::remove_cvref_t<StateT>, RuleTs>...>,
		Error<std::remove_cvref_t<StateT>>> {
		using Value	 = std::tuple<rule_value_t<StateT, RuleTs>...>;
		using Error	 = Error<std::remove_cvref_t<StateT>>;
		using Result = Expected<Value, Error>;

		Value value;
		Error error;

		if ([&]<size_t... Is>(std::index_sequence<Is...>) {
				return ([&]() {
					auto res = std::get<Is>(*this).match(std::forward<StateT>(st));
					if (res) {
						std::get<Is>(value) = std::move(res.value());
						return true;
					} else {
						error.error = std::move(res.error());
						return false;
					}
					++error.pos;
				}() && ...);
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
SequentialRule(RuleTs&&...) -> SequentialRule<RuleTs...>;

template<typename... RuleTs>
inline constexpr auto sequential(RuleTs&&... rules) -> SequentialRule<RuleTs...> {
	return {std::forward<RuleTs>(rules)...};
}

template<typename... Ls, typename... Rs>
inline constexpr auto operator>>(SequentialRule<Ls...>&& l, SequentialRule<Rs...>&& r)
	-> SequentialRule<Ls..., Rs...> {
	return std::apply(
		[&](auto&&... ls) {
			return std::apply(
				[&](auto&&... rs) {
					return SequentialRule<Ls..., Rs...> {
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
inline constexpr auto operator>>(L&& l, SequentialRule<Rs...>&& r) -> SequentialRule<L, Rs...> {
	return std::apply(
		[&](auto&&... rs) {
			return SequentialRule<L, Rs...> {
				std::forward<L>(l),
				std::forward_like<decltype(r)>(rs)...
			};
		},
		r.as_tuple()
	);
}

template<typename... Ls, typename R>
inline constexpr auto operator>>(SequentialRule<Ls...>&& l, R&& r) -> SequentialRule<Ls..., R> {
	return std::apply(
		[&](auto&&... ls) {
			return SequentialRule<Ls..., R> {
				std::forward_like<decltype(l)>(ls)...,
				std::forward<R>(r)
			};
		},
		l.as_tuple()
	);
}

template<typename L, typename R>
inline constexpr auto operator>>(L&& l, R&& r) -> SequentialRule<L, R> {
	return {std::forward<L>(l), std::forward<R>(r)};
}

}  // namespace pars