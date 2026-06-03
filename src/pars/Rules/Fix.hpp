#pragma once

#include "pars/Meta.hpp"

namespace pars {
/// @brief Fix point Rule, you can view it as a y-combinator in pars rule combination.
///
/// This is rule receives a function/lambda to construct rule with itself recursively.
template<typename ResultT>
struct FixRule {
	template<typename FnT, typename... QueryTs>
	struct Of {
		PARS_NO_UNIQUE_ADDRESS FnT fn;

		using required_queries_type = std::tuple<QueryTs...>;

		template<typename StateT>
		constexpr auto match(StateT&& st) const -> ResultT {
			return std::invoke(fn, *this).match(std::forward<StateT>(st));
		}

		inline friend constexpr auto operator==(const Of& l, const Of& r) -> bool { return true; }
	};

	template<typename... QueryTs, typename FnT>
	inline static constexpr auto of(FnT&& fn) -> Of<FnT, QueryTs...> {
		return {std::forward<FnT>(fn)};
	}
};

template<typename ResultT>
using fix = FixRule<ResultT>;

}  // namespace pars
