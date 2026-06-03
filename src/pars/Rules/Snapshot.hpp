#pragma once

#include "pars/Meta.hpp"
#include "pars/Snapshot.hpp"

namespace pars {
struct state_snapshot_impl_t {};

inline constexpr auto state_snapshot_impl = state_snapshot_impl_t {};

template<typename QueryT, typename FnT>
struct StateSnapshotRule : FnImpl<state_snapshot_impl_t, FnT> {
	template<typename StateT>
	constexpr auto match(StateT&& st) const -> decltype(auto) {
		auto&	   q	= query(st, QueryT {});
		const auto snap = snapshot(q);
		auto	   res	= std::invoke(this->fn(state_snapshot_impl), std::forward<StateT>(st));

		if (!res)
			st = rollback(q, snap);

		return res;
	}
};

template<typename QueryT, typename FnT>
inline constexpr auto state_snap(FnT&& fn) -> StateSnapshotRule<QueryT, FnT> {
	return {std::forward<FnT>(fn)};
}

template<auto Query, typename FnT>
inline constexpr auto state_snap(FnT&& fn) -> StateSnapshotRule<decltype(Query), FnT> {
	return {std::forward<FnT>(fn)};
}
}  // namespace pars