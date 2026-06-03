#pragma once

#include "pars/Utf.hpp"
#include "pars/Query.hpp"
#include "pars/Meta.hpp"

#include <tl/expected.hpp>

#include <utility>

namespace pars {
template<typename T>
concept SnapshotableC = requires(T t) { snapshot(t); };

template<typename T>
concept RollbackableC = requires(T t) { rollback(t, std::declval<decltype(snapshot(t))>()); };

inline constexpr auto snapshot(const u8::Cursor& cursor) {
	return cursor;
}

inline constexpr auto rollback(u8::Cursor& cursor, const u8::Cursor& snap) {
	cursor = snap;
}

template<typename T>
using snapshot_t = decltype(snapshot(std::declval<T>()));

template<typename StateT, typename... QueryTs>
struct SnapshotGuard : std::tuple<snapshot_t<query_t<StateT, QueryTs>>...> {
	StateT& state;

	constexpr SnapshotGuard(StateT& state) :
		state(state),
		std::tuple<snapshot_t<query_t<StateT, QueryTs>>...> {
			snapshot(query(state, QueryTs {}))...
		} {}

	constexpr auto apply_rollback() -> void {
		std::apply(
			[&](auto&&... snaps) { (rollback(query(state, QueryTs {}), snaps), ...); },
			static_cast<std::tuple<snapshot_t<query_t<StateT, QueryTs>>...>&>(*this)
		);
	}

	template<typename ValT>
	constexpr auto rolled_ok(ValT&& val) -> decltype(auto) {
		apply_rollback();
		return (val);
	}

	template<typename ValT>
	constexpr auto rolled(ValT&& val) -> decltype(auto) {
		apply_rollback();
		return (val);
	}

	template<typename ErrT>
	constexpr auto rolled_err(ErrT&& err) -> decltype(auto) {
		apply_rollback();
		return tl::make_unexpected(std::forward<ErrT>(err));
	}
};

template<typename StateT>
struct snap_guard {
	StateT& state;

	template<typename T>
	struct make_snap_guard_type {};

	template<typename... Ts>
	struct make_snap_guard_type<std::tuple<Ts...>> {
		using type = SnapshotGuard<StateT, Ts...>;
	};

	// template<typename... QueryTs>
	// inline constexpr auto of() -> SnapshotGuard<StateT, QueryTs...> {
	// 	return {state};
	// }

	template<Like<std::tuple> TupleT>
	inline constexpr auto of() -> make_snap_guard_type<TupleT>::type {
		return {state};
	}
};

template<typename StateT, typename... QueryTs>
struct AggregateSnapshot : std::tuple<snapshot_t<query_t<StateT, QueryTs>>...> {
	// TODO:
};

}  // namespace pars