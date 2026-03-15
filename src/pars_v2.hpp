#pragma once

#include "pars/Utils.hpp"

#include <tl/expected.hpp>

#include <algorithm>
#include <concepts>
#include <ranges>
#include <utility>
#include <format>

namespace pars::v2::pars {
struct snapshot_t {};

struct rollback_t {};

struct query_t {};

struct match_t {};

struct required_query_t {};

struct to_string_t {};

inline constexpr auto snapshot = UnifiedCallOp<snapshot_t> {};
inline constexpr auto rollback = UnifiedCallOp<rollback_t> {};
// inline constexpr auto query	   = UnifiedCallOp<query_t> {};
inline constexpr auto match		= UnifiedCallOp<match_t> {};
inline constexpr auto to_string = UnifiedCallOp<to_string_t> {};

template<typename TagT>
concept QueryTag = requires() { typename TagT::queryable_type; };
template<typename T, typename TagT>
concept Queryable = requires(T t, TagT tag) {
	{ t.query(tag) } -> std::convertible_to<typename TagT::queryable_type>;
};
template<typename T, typename... TagT>
concept QueryableAll = (Queryable<T, TagT> && ...);

template<typename T, typename SigT>
struct QueryableAllSig : std::false_type {};

template<typename T, typename ResT, typename... Args>
	requires(Queryable<T, Args> && ...)
struct QueryableAllSig<T, ResT(Args...)> : std::true_type {};

template<typename T, typename SigT>
concept QueryableAllSigC = QueryableAllSig<T, SigT>::value;

template<typename TagT>
struct QueryTagBase {
	constexpr auto& operator()(Queryable<TagT> auto&& state) const { return state.query(TagT()); }
};

template<typename TagT>
inline constexpr auto query(Queryable<TagT> auto&& st) {
	return st.query(TagT());
}

template<typename TagT>
using QueryTagSnapshotType =
	decltype(TagT::snapshot(std::declval<decltype(TagT::queryable_type)>()));

using ParserCursor = u8::Cursor;

struct QueryParserCursor : QueryTagBase<QueryParserCursor> {
	using queryable_type = ParserCursor;

	inline static constexpr auto snapshot(const queryable_type& queryable) { return queryable; }

	inline static constexpr auto rollback(
		queryable_type& queryable, const queryable_type& snapshot
	) {
		queryable = snapshot;
	}

	[[nodiscard]] constexpr auto tag_invoke(
		snapshot_t, const queryable_type& queryable
	) const noexcept {
		return queryable;
	}

	[[nodiscard]] inline friend constexpr auto tag_invoke(
		snapshot_t, const Queryable<QueryParserCursor> auto& queryable, QueryParserCursor
	) noexcept {
		return queryable.query(QueryParserCursor {});
	}

	constexpr auto tag_invoke(
		rollback_t, queryable_type& queryable, const queryable_type& snapshot
	) const noexcept {
		queryable = snapshot;
	}

	inline friend constexpr auto tag_invoke(
		rollback_t, Queryable<QueryParserCursor> auto&& queryable, QueryParserCursor,
		const queryable_type& snapshot
	) noexcept {
		queryable.query(QueryParserCursor {}) = snapshot;
	}
};

inline constexpr QueryParserCursor query_parser_cursor;

template<typename TagT>
struct QueryableMixin {
	TagT::queryable_type  queryable;

	constexpr const auto& query(TagT) const { return queryable; }

	constexpr auto&		  query(TagT) { return queryable; }

	constexpr const auto& tag_invoke(query_t, TagT) const { return queryable; }

	constexpr auto&		  tag_invoke(query_t, TagT) { return queryable; }
};

struct DefaultParserState : QueryableMixin<QueryParserCursor> {
	DefaultParserState(std::u8string_view script) : QueryableMixin<QueryParserCursor>({script}) {}

	DefaultParserState(const DefaultParserState&)				 = default;
	DefaultParserState(DefaultParserState&&) noexcept			 = default;
	DefaultParserState& operator=(const DefaultParserState&)	 = default;
	DefaultParserState& operator=(DefaultParserState&&) noexcept = default;
};

template<typename... QueryableTs>
class AggregateParserState {
public:
	AggregateParserState(QueryableTs&&... queryables) :
		_queryables(std::forward<QueryableTs>(queryables)...) {}

public:
	constexpr auto& query(auto&& tag) {
		using TagT = std::remove_cvref_t<decltype(tag)>;
		static_assert((Queryable<QueryableTs, TagT> || ...));
		return std::apply(
			[&](auto&&... queryables) -> auto& {
				typename TagT::queryable_type* queryable;

				([&]() {
					if constexpr (Queryable<
									  std::remove_cvref_t<decltype(queryables)>,
									  std::remove_cvref_t<decltype(tag)>>) {
						queryable = &tag(queryables);
						return true;
					} else
						return false;
				}()
				 || ...);

				return *queryable;
			},
			_queryables
		);
	}

	constexpr const auto& query(auto&& tag) const {
		using TagT = std::remove_cvref_t<decltype(tag)>;
		static_assert((Queryable<QueryableTs, TagT> || ...));
		return std::apply(
			[&](auto&&... queryables) -> auto& {
				const typename TagT::queryable_type* queryable = nullptr;

				([&]() {
					if constexpr (Queryable<
									  std::remove_cvref_t<decltype(queryables)>,
									  std::remove_cvref_t<decltype(tag)>>) {
						queryable = &tag(queryables);
						return true;
					} else
						return false;
				}()
				 || ...);

				return *queryable;
			},
			_queryables
		);
	}

	constexpr const auto& tag_invoke(query_t, auto&& tag) const {
		return query(std::forward<decltype(tag)>(tag));
	}

private:
	std::tuple<QueryableTs...> _queryables;
};

template<typename... QueryableTs>
AggregateParserState(QueryableTs&&...) -> AggregateParserState<QueryableTs...>;

template<typename... QueryableTs>
inline constexpr auto aggregate(QueryableTs&&... queryables)
	-> AggregateParserState<QueryableTs...> {
	return {std::forward<QueryableTs>(queryables)...};
}

/**
 * @brief Wrapper around `RequiredQuery` attributes of parser rules.
 * @details
 * - `RuleT::RequiredQuery = T` $\Rightarrow$ `<required_query_t(T)``
 * - `RuleT::RequiredQuery = required_query_t(Ts...)` $\Rightarrow$ ``required_query_t(Ts...)`
 * - `RuleT::RequiredQuery` undefined $\Rightarrow$ `required_query_t()`
 *
 * @tparam RuleT
 */
template<typename RuleT>
using RequiredQueryOfRule = std::conditional_t<
	requires { typename RuleT::RequiredQuery; },
	std::conditional_t<
		signature_based_on<typename RuleT::RequiredQuery, required_query_t>::value,
		typename RuleT::RequiredQuery, required_query_t(typename RuleT::RequiredQuery)>,
	required_query_t()>;

template<typename... RuleTs>
using RequiredQueryOfRules = signature_cup_t<RequiredQueryOfRule<RuleTs>...>;

template<typename... QueryTagTs>
struct AggregateSnapshot : std::tuple<typename QueryTagTs::queryable_type...> {
	using Tuple = std::tuple<typename QueryTagTs::queryable_type...>;

	template<QueryableAll<QueryTagTs...> QueryableT>
	AggregateSnapshot(QueryableT&& queryable) :
		// Tuple {(QueryTagTs {} | snapshot(std::forward<QueryableT>(queryable)))...} {}
		Tuple {(std::forward<QueryableT>(queryable) | snapshot(QueryTagTs {}))...} {}

	auto as_tuple() const& -> const auto& { return static_cast<const Tuple&>(*this); }

	auto as_tuple() & -> auto& { return static_cast<Tuple&>(*this); }

	auto as_tuple() && -> auto&& { return static_cast<Tuple&&>(*this); }

	inline constexpr auto tag_invoke(
		rollback_t, QueryableAll<QueryTagTs...> auto&& queryable, const AggregateSnapshot& snapshot
	) {
		snapshot.as_tuple() | apply([&](auto&&... args) {
			((std::forward<decltype(queryable)>(queryable)
			  | rollback(QueryTagTs(), std::forward<decltype(args)>(args))),
			 ...);
		});
	}

	constexpr auto rollback(auto&& queryable) const {
		std::apply(
			[&](auto&&... snapshots) {
				((std::forward<decltype(queryable)>(queryable)
				  | pars::rollback(QueryTagTs(), std::forward<decltype(snapshots)>(snapshots))),
				 ...);
			},
			as_tuple()
		);
	}
};

template<typename... RuleTs>
inline constexpr auto aggregate_snapshot_of_rules(
	QueryableAllSigC<RequiredQueryOfRules<RuleTs...>> auto&& queryable
) -> apply_signature_t<AggregateSnapshot, RequiredQueryOfRules<RuleTs...>> {
	return {std::forward<decltype(queryable)>(queryable)};
}

template<typename RuleT, typename ParserStateT>
concept ParserRule = requires(RuleT rule, ParserStateT state) { rule.match(state); };

enum class ResultPolicy {
	Normal,		   // Result + Error, default.
	DiscardValue,  // Error Only, for error rebuild.
	DiscardError,  // Value Only, for quick parsing.
};

namespace ResultPolicies {
struct Normal {};

struct DiscardValue {};

struct DiscardError {};

inline constexpr Normal		  normal;
inline constexpr DiscardValue discard_value;
inline constexpr DiscardError discard_error;

}  // namespace ResultPolicies

template<typename T, typename PolicyT>
using PoliciedValue =
	std::conditional_t<!std::same_as<PolicyT, ResultPolicies::DiscardValue>, T, Ignore>;
template<typename T, typename PolicyT>
using PoliciedError =
	std::conditional_t<!std::same_as<PolicyT, ResultPolicies::DiscardValue>, T, Ignore>;

template<typename T, typename E, typename PolicyT>
using PoliciedResult = tl::expected<PoliciedValue<T, PolicyT>, PoliciedError<E, PolicyT>>;

namespace Errors {
struct CharMismatchError {
	char32_t		   mismatched;
	char32_t		   expect;

	[[nodiscard]] auto tag_invoke(to_string_t) const -> std::string {
		return std::format(
			"CharMismatch: expected `{}`, received `{}`",
			(char)expect,
			(char)mismatched
		);
	}
};
}  // namespace Errors

struct CharRule {
	char32_t ch;

	using RequiredQuery = QueryParserCursor;
	using ValueType		= char32_t;
	using ErrorType		= Errors::CharMismatchError;
	using ResultType	= tl::expected<ValueType, ErrorType>;

	constexpr auto match(Queryable<QueryParserCursor> auto&& state) const -> ResultType {
		auto& cursor = query_parser_cursor(state);
		if (const auto nxt = cursor.peek(); nxt == ch) {
			cursor.bump();
			return ch;
		} else
			return tl::make_unexpected<Errors::CharMismatchError>({
				.mismatched = nxt,
				.expect		= ch,
			});
	}
};

using c = CharRule;

template<size_t N>
struct CharSetRule {
	std::array<char32_t, N> chs;

	constexpr CharSetRule(const char32_t (&arr)[N]) : chs {std::to_array(arr)} {
		std::ranges::sort(chs);
	}

	constexpr CharSetRule(std::array<char32_t, N>&& arr) : chs(std::move(arr)) {
		std::ranges::sort(chs);
	}

	constexpr CharSetRule(const std::array<char32_t, N>& arr) : chs(arr) { std::ranges::sort(chs); }

	using RequiredQuery = QueryParserCursor;
	using ValueType		= char32_t;
	using ErrorType		= Errors::CharMismatchError;
	using ResultType	= tl::expected<ValueType, ErrorType>;

	constexpr auto match(Queryable<QueryParserCursor> auto&& state) const -> ResultType {
		auto& cursor = query_parser_cursor(state);
		if (const auto ch = cursor.peek()) {
			size_t lo = 0;
			size_t hi = N;
			while (lo < hi) {
				const auto mid	= (lo + hi) >> 1;
				const auto cand = chs[mid];
				if (ch < cand)
					hi = mid;
				else if (ch > cand)
					lo = mid;
				else {
					cursor.bump();
					return ch;
				}
			}
			return tl::make_unexpected<ErrorType>({ch});
		} else
			return tl::make_unexpected<ErrorType>({u8::Cursor::eof});
	}
};

template<size_t N>
inline constexpr auto cset(const char32_t (&arr)[N]) {
	return CharSetRule<N> {arr};
}

template<std::convertible_to<char32_t>... Chs>
inline constexpr auto cset(Chs&&... chs) -> CharSetRule<sizeof...(Chs)> {
	return {std::array {static_cast<char32_t>(std::forward<Chs>(chs))...}};
}

template<NoEndU32StringLiteral str>
inline constexpr auto operator""_cset() -> CharSetRule<str.size> {
	return {std::move(str.str)};
}

struct CharRangeRule {
	char32_t lo;
	char32_t hi;

	using ValueType	 = char32_t;
	using ErrorType	 = Errors::CharMismatchError;
	using ResultType = tl::expected<ValueType, ErrorType>;

	constexpr auto match(Queryable<QueryParserCursor> auto&& state) const -> ResultType {
		auto& cursor = query_parser_cursor(state);
		return {};
	}
};

template<typename... RuleTs>
struct SequentialRule {
	std::tuple<std::remove_cvref_t<RuleTs>...> rules;

	struct SequentialSingleStageRuleError {
		size_t																 pos;
		unique_variant_t<typename std::remove_cvref_t<RuleTs>::ErrorType...> error;

		[[nodiscard]] auto tag_invoke(to_string_t) const -> std::string {
			return std::format(
				"SequentialSingleStageRuleError: encounterd error at stage {}, which: `{}`",
				pos,
				error | overload {[]<typename E>(E&& e) constexpr -> std::string {
					return pars::to_string(std::forward<E>(e));
				}}
			);
		}
	};

	template<typename... Rs>
	constexpr SequentialRule(Rs&&... rules) : rules(std::forward<Rs>(rules)...) {}

	using RequiredQuery = RequiredQueryOfRules<RuleTs...>;
	using ValueType		= std::tuple<typename std::remove_cvref_t<RuleTs>::ValueType...>;
	using ErrorType		= SequentialSingleStageRuleError;
	using ResultType	= tl::expected<ValueType, ErrorType>;

	template<typename PolicyT = ResultPolicies::Normal>
	constexpr auto match(auto&& state, PolicyT policy = ResultPolicies::normal) const
		-> PoliciedResult<ValueType, ErrorType, PolicyT> {
		// TODO:

		// const auto snapshot =
		// 	aggregate_snapshot_of_rules<RuleTs...>(std::forward<decltype(state)>(state));
		const auto snapshot =
			apply_signature_t<AggregateSnapshot, RequiredQueryOfRules<RuleTs...>> {
				std::forward<decltype(state)>(state)
			};

		return rules | apply([&](auto&&... rs) -> PoliciedResult<ValueType, ErrorType, PolicyT> {
				   PoliciedValue<ValueType, PolicyT> value;
				   PoliciedError<ErrorType, PolicyT> error;
				   if ([&]<size_t... Is>(std::index_sequence<Is...>) {
						   return ([&]() {
							   // auto res = state | pars::match(std::get<Is>(rules));
							   auto res = std::get<Is>(rules).match(state);
							   if (res) {
								   std::get<Is>(value) = std::move(res.value());
								   return true;
							   } else {
								   // error = tl::make_unexpected(std::move(res.error()));
								   error = SequentialSingleStageRuleError {
									   .pos	  = Is,
									   .error = {std::move(res.error())},
								   };
								   return false;
							   }
						   }() && ...);
					   }(std::make_index_sequence<sizeof...(RuleTs)>()))
					   return value;
				   else {
					   // tag_invoke(rollback_t {}, state, snapshot);
					   snapshot.rollback(state);
					   // state | rollback(snapshot);
					   return tl::make_unexpected(error);
				   }
			   });
	}

	template<typename PolicyT = ResultPolicies::Normal>
	inline friend constexpr auto tag_invoke(
		match_t, auto&& state, const SequentialRule& rule, PolicyT policy = ResultPolicies::normal
	) -> decltype(auto) {
		return rule.match(std::forward<decltype(state)>(state), policy);
	}
};

template<typename... RuleTs>
SequentialRule(RuleTs&&...) -> SequentialRule<std::remove_cvref_t<RuleTs>...>;

template<typename ParserRuleT>
struct PeekIsRule {};

}  // namespace pars::v2::pars
