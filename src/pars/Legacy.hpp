#pragma once

#include "pars/Utils.hpp"
#include "pars/Arena.hpp"

#include <tl/expected.hpp>
#include <utf8cpp/utf8/cpp20.h>

#include <algorithm>
#include <concepts>
#include <functional>
#include <memory>
#include <optional>
#include <ranges>
#include <tuple>
#include <type_traits>
#include <utility>
#include <format>
#include <string>
#include <variant>
#include <vector>

namespace pars::Legacy {
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

inline constexpr auto tag_invoke(to_string_t, std::monostate) -> std::string {
	return "monostate";
}

inline constexpr auto tag_invoke(to_string_t, Ignore) -> std::string {
	return "ignore";
}

struct DynamicError {
	std::string message;

	DynamicError() = default;

	template<typename ErrorT>
		requires requires(const std::remove_cvref_t<ErrorT>& error) {
			{ to_string(error) } -> std::convertible_to<std::string>;
		}
	constexpr DynamicError(ErrorT&& error) : message(to_string(std::forward<ErrorT>(error))) {}

	template<typename ErrorT>
		requires(!requires(const std::remove_cvref_t<ErrorT>& error) {
			{ to_string(error) } -> std::convertible_to<std::string>;
		})
	constexpr DynamicError(ErrorT&&) : message("<dynamic error>") {}

	friend auto tag_invoke(to_string_t, const DynamicError& error) -> std::string {
		return error.message;
	}
};

template<typename... Es>
inline constexpr auto tag_invoke(to_string_t, const std::tuple<Es...>& es) {
	std::string res = "(";
	std::apply(
		[&](auto&&... e) {
			(
				[&]() {
					res += to_string(e);
					res += ", ";
				},
				...);
		},
		es
	);
	res += ')';
	return res;
}

template<typename... Es>
inline constexpr auto tag_invoke(to_string_t, const std::variant<Es...>& es) {
	return std::visit([]<typename E>(const E& e) -> std::string { return to_string(e); }, es);
}

template<typename TagT>
struct queryable_type {
	using type = typename TagT::QueryableType;
};

template<typename TagT>
using queryable_type_t = typename queryable_type<TagT>::type;

template<typename TagT>
concept QueryTag = requires() { typename queryable_type_t<TagT>; };
template<typename T, typename TagT>
concept Queryable = requires(T t, TagT tag) {
	{ t.query(tag) } -> std::convertible_to<queryable_type_t<TagT>>;
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

template<typename QueryableT>
struct QueryTagOf : QueryTagBase<QueryTagOf<QueryableT>> {
	using QueryableType = QueryableT;
};

template<typename QueryTagT, typename QueryableT>
struct CopyPasteSnapshot {
	inline friend constexpr auto tag_invoke(
		snapshot_t, const Queryable<QueryTagT> auto& queryable, QueryTagT
	) noexcept {
		return queryable.query(QueryTagT {});
	}

	inline friend constexpr auto tag_invoke(
		rollback_t, Queryable<QueryTagT> auto&& queryable, QueryTagT, const QueryableT& snapshot
	) noexcept {
		queryable.query(QueryTagT {}) = snapshot;
	}
};

template<typename TagT, typename QueryableT>
struct SnapshotableQueryTagBase : QueryTagBase<TagT> {
	inline friend constexpr auto tag_invoke(
		snapshot_t, const Queryable<TagT> auto& queryable, TagT
	) noexcept -> QueryableT
		requires std::copy_constructible<QueryableT>
	{
		return queryable.query(TagT {});
	}

	inline friend constexpr auto tag_invoke(
		rollback_t, Queryable<TagT> auto&& queryable, TagT, const QueryableT& snapshot
	) noexcept
		requires std::assignable_from<QueryableT&, const QueryableT&>
	{
		queryable.query(TagT {}) = snapshot;
	}
};

template<typename TagT>
inline constexpr decltype(auto) query(Queryable<TagT> auto&& st) {
	return st.query(TagT());
}

template<typename TagT>
using QueryTagSnapshotType = decltype(TagT::snapshot(std::declval<queryable_type_t<TagT>>()));

using ParserCursor		   = u8::Cursor;
template<typename TokenT>
using TokenCursor = seq::Cursor<TokenT>;

struct QueryParserCursor : QueryTagBase<QueryParserCursor> {
	using QueryableType = ParserCursor;

	inline static constexpr auto snapshot(const QueryableType& queryable) { return queryable; }

	inline static constexpr auto rollback(QueryableType& queryable, const QueryableType& snapshot) {
		queryable = snapshot;
	}

	[[nodiscard]] inline static constexpr auto tag_invoke(
		snapshot_t, const QueryableType& queryable
	) noexcept {
		return queryable;
	}

	[[nodiscard]] inline friend constexpr auto tag_invoke(
		snapshot_t, const Queryable<QueryParserCursor> auto& queryable, QueryParserCursor
	) noexcept {
		return queryable.query(QueryParserCursor {});
	}

	constexpr auto tag_invoke(
		rollback_t, QueryableType& queryable, const QueryableType& snapshot
	) const noexcept {
		queryable = snapshot;
	}

	inline friend constexpr auto tag_invoke(
		rollback_t, Queryable<QueryParserCursor> auto&& queryable, QueryParserCursor,
		const QueryableType& snapshot
	) noexcept {
		queryable.query(QueryParserCursor {}) = snapshot;
	}
};

inline constexpr QueryParserCursor query_parser_cursor;

template<typename TokenT>
struct QueryTokenCursor : QueryTagBase<QueryTokenCursor<TokenT>> {
	using QueryableType = TokenCursor<TokenT>;

	inline static constexpr auto snapshot(const QueryableType& queryable) { return queryable; }

	inline static constexpr auto rollback(QueryableType& queryable, const QueryableType& snapshot) {
		queryable = snapshot;
	}

	[[nodiscard]] constexpr auto tag_invoke(
		snapshot_t, const QueryableType& queryable
	) const noexcept {
		return queryable;
	}

	[[nodiscard]] inline friend constexpr auto tag_invoke(
		snapshot_t, const Queryable<QueryTokenCursor<TokenT>> auto& queryable, QueryTokenCursor
	) noexcept {
		return queryable.query(QueryTokenCursor<TokenT> {});
	}

	constexpr auto tag_invoke(
		rollback_t, QueryableType& queryable, const QueryableType& snapshot
	) const noexcept {
		queryable = snapshot;
	}

	inline friend constexpr auto tag_invoke(
		rollback_t, Queryable<QueryTokenCursor<TokenT>> auto&& queryable, QueryTokenCursor,
		const QueryableType& snapshot
	) noexcept {
		queryable.query(QueryTokenCursor<TokenT> {}) = snapshot;
	}
};

template<typename TokenT>
inline constexpr QueryTokenCursor<TokenT> query_token_cursor;

template<typename TagT>
struct QueryableMixin {
	queryable_type_t<TagT> queryable;

	constexpr const auto&  query(TagT) const { return queryable; }

	constexpr auto&		   query(TagT) { return queryable; }

	constexpr const auto&  tag_invoke(query_t, TagT) const { return queryable; }

	constexpr auto&		   tag_invoke(query_t, TagT) { return queryable; }
};

struct TextParserState : QueryableMixin<QueryParserCursor> {
	TextParserState(std::u8string_view script) : QueryableMixin<QueryParserCursor>({script}) {}

	TextParserState(const TextParserState&)				   = default;
	TextParserState(TextParserState&&) noexcept			   = default;
	TextParserState& operator=(const TextParserState&)	   = default;
	TextParserState& operator=(TextParserState&&) noexcept = default;
};

template<typename TokenT>
struct TokenParserState : QueryableMixin<QueryTokenCursor<TokenT>> {
	TokenParserState(std::span<const TokenT> stream) :
		QueryableMixin<QueryTokenCursor<TokenT>>({stream}) {}

	template<size_t N>
	TokenParserState(const std::array<TokenT, N>& stream) : TokenParserState(std::span {stream}) {}

	TokenParserState(const std::vector<TokenT>& stream) : TokenParserState(std::span {stream}) {}

	TokenParserState(const TokenParserState&)				 = default;
	TokenParserState(TokenParserState&&) noexcept			 = default;
	TokenParserState& operator=(const TokenParserState&)	 = default;
	TokenParserState& operator=(TokenParserState&&) noexcept = default;
};

template<typename... QueryableMixinTs>
class AggregateParserStateOfMixin {
public:
	AggregateParserStateOfMixin(QueryableMixinTs&&... queryables) :
		_queryables(std::forward<QueryableMixinTs>(queryables)...) {}

public:
	template<typename TagTs>
		requires((Queryable<QueryableMixinTs, std::remove_cvref_t<TagTs>> || ...))
	constexpr auto& query(TagTs&& tag) {
		using TagT = std::remove_cvref_t<TagTs>;
		return std::apply(
			[&](auto&&... queryables) -> auto& {
				queryable_type_t<TagT>* queryable;

				([&]() {
					if constexpr (
						Queryable<
							std::remove_cvref_t<decltype(queryables)>,
							std::remove_cvref_t<decltype(tag)>>
					) {
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

	template<typename TagTs>
		requires((Queryable<QueryableMixinTs, std::remove_cvref_t<TagTs>> || ...))
	constexpr auto& query(TagTs&& tag) const {
		using TagT = std::remove_cvref_t<TagTs>;
		return std::apply(
			[&](auto&&... queryables) -> auto& {
				const queryable_type_t<TagT>* queryable = nullptr;

				([&]() {
					if constexpr (
						Queryable<
							std::remove_cvref_t<decltype(queryables)>,
							std::remove_cvref_t<decltype(tag)>>
					) {
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
	std::tuple<QueryableMixinTs...> _queryables;
};

template<typename... QueryableMixinTs>
AggregateParserStateOfMixin(QueryableMixinTs&&...)
	-> AggregateParserStateOfMixin<QueryableMixinTs...>;

template<typename... QueryableMixinTs>
inline constexpr auto aggregate(QueryableMixinTs&&... queryables)
	-> AggregateParserStateOfMixin<QueryableMixinTs...> {
	return {std::forward<QueryableMixinTs>(queryables)...};
}

template<typename... QueryableTs>
using AggregateParserState = AggregateParserStateOfMixin<QueryableMixin<QueryableTs>...>;

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
struct AggregateSnapshot : std::tuple<queryable_type_t<QueryTagTs>...> {
	using Tuple = std::tuple<queryable_type_t<QueryTagTs>...>;

	template<QueryableAll<QueryTagTs...> QueryableT>
	constexpr AggregateSnapshot(QueryableT&& queryable) :
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
				  | rollback(QueryTagTs(), std::forward<decltype(snapshots)>(snapshots))),
				 ...);
			},
			as_tuple()
		);
	}
};

template<typename... RuleTs>
using AggregateSnapshotOfRules =
	apply_signature_t<AggregateSnapshot, RequiredQueryOfRules<RuleTs...>>;

template<typename... RuleTs, typename QueryableT>
	requires QueryableAllSigC<std::remove_cvref_t<QueryableT>, RequiredQueryOfRules<RuleTs...>>
inline constexpr auto aggregate_snapshot_of_rules(QueryableT&& queryable)
	-> AggregateSnapshotOfRules<RuleTs...> {
	return {std::forward<QueryableT>(queryable)};
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
	std::conditional_t<!std::same_as<PolicyT, ResultPolicies::DiscardError>, T, Ignore>;

template<typename T, typename E, typename PolicyT>
using PoliciedResult = tl::expected<PoliciedValue<T, PolicyT>, PoliciedError<E, PolicyT>>;

namespace Errors {
struct EarlyEofError {
	[[nodiscard]] auto tag_invoke(to_string_t) const -> std::string { return "Early EOF"; }
};

template<typename T>
inline auto token_debug_string(const T& token) -> std::string {
	if constexpr (std::same_as<T, char32_t>)
		return to_utf8(token);
	else if constexpr (requires { to_string(token); })
		return to_string(token);
	else if constexpr (std::integral<T> || std::floating_point<T>)
		return std::format("{}", token);
	else
		return "<token>";
}

template<typename TokenT>
struct TokenMismatchError {
	TokenT			   mismatched;
	TokenT			   expect;

	[[nodiscard]] auto tag_invoke(to_string_t) const -> std::string {
		return std::format(
			"TokenMismatch: expected `{}`, received `{}`",
			token_debug_string(expect),
			token_debug_string(mismatched)
		);
	}
};

template<typename TokenT, size_t N>
struct TokenSetMismatchError {
	TokenT				  mismatched;
	std::array<TokenT, N> expects;

	[[nodiscard]] auto	  tag_invoke(to_string_t) const -> std::string {
		std::string expects_str;
		for (size_t i = 0; i < N; ++i) {
			if (i != 0)
				expects_str += ", ";
			expects_str += '`';
			expects_str += token_debug_string(expects[i]);
			expects_str += '`';
		}
		return std::format(
			"TokenSetMismatch: received `{}`, expected one of [{}]",
			token_debug_string(mismatched),
			expects_str
		);
	}
};

template<typename TokenT>
struct TokenRangeMismatchError {
	TokenT			   mismatched;
	TokenT			   lo;
	TokenT			   hi;

	[[nodiscard]] auto tag_invoke(to_string_t) const -> std::string {
		return std::format(
			"TokenRangeMismatch: received `{}`, expected in range [`{}`, `{}`]",
			token_debug_string(mismatched),
			token_debug_string(lo),
			token_debug_string(hi)
		);
	}
};

struct CharMismatchError {
	char32_t		   mismatched;
	char32_t		   expect;

	[[nodiscard]] auto tag_invoke(to_string_t) const -> std::string {
		return std::format(
			"CharMismatch: expected `{}`, received `{}`",
			to_utf8(expect),
			to_utf8(mismatched)
		);
	}
};

template<size_t N>
struct CharSetMismatchError {
	char32_t				mismatched;
	std::array<char32_t, N> expects;

	[[nodiscard]] auto		tag_invoke(to_string_t) const -> std::string {
		std::string expects_str;
		for (size_t i = 0; i < N; ++i) {
			if (i != 0)
				expects_str += ", ";
			expects_str += '`';
			expects_str += to_utf8(expects[i]);
			expects_str += '`';
		}
		return std::format(
			"CharSetMismatch: received `{}`, expected one of [{}]",
			to_utf8(mismatched),
			expects_str
		);
	}
};

struct CharRangeMismatchError {
	char32_t		   mismatched;
	char32_t		   lo;
	char32_t		   hi;

	[[nodiscard]] auto tag_invoke(to_string_t) const -> std::string {
		return std::format(
			"CharRangeMismatch: received `{}`, expected in range [`{}`, `{}`]",
			to_utf8(mismatched),
			to_utf8(lo),
			to_utf8(hi)
		);
	}
};

struct RuleMatchedError {
	[[nodiscard]] auto tag_invoke(to_string_t) const -> std::string {
		return "PeekNot failed because the inner rule matched";
	}
};
}  // namespace Errors

/* Rules */

template<typename QueryTagT, typename EofErrorT = Errors::EarlyEofError>
struct AnyTokenRule {
	using TokenType		= typename queryable_type_t<QueryTagT>::value_type;
	using RequiredQuery = QueryTagT;
	using ValueType		= TokenType;
	using ErrorType		= EofErrorT;
	using ResultType	= tl::expected<ValueType, ErrorType>;

	constexpr auto match(Queryable<QueryTagT> auto&& state) const -> ResultType {
		auto& cursor = query<QueryTagT>(state);
		if (const auto token = cursor.bump(); token != queryable_type_t<QueryTagT>::eof)
			return token;
		else
			return tl::make_unexpected<ErrorType>({});
	}
};

template<
	typename QueryTagT,
	typename ErrorT = Errors::TokenMismatchError<typename queryable_type_t<QueryTagT>::value_type>>
struct TokenRule {
	using TokenType = typename queryable_type_t<QueryTagT>::value_type;
	TokenType token;

	using RequiredQuery = QueryTagT;
	using ValueType		= TokenType;
	using ErrorType		= ErrorT;
	using ResultType	= tl::expected<ValueType, ErrorType>;

	constexpr auto match(Queryable<QueryTagT> auto&& state) const -> ResultType {
		auto& cursor = query<QueryTagT>(state);
		if (const auto nxt = cursor.peek(); nxt == token) {
			cursor.bump();
			return token;
		} else
			return tl::make_unexpected<ErrorType>({
				.mismatched = nxt,
				.expect		= token,
			});
	}
};

template<
	typename QueryTagT, size_t N,
	typename SetErrorT =
		Errors::TokenSetMismatchError<typename queryable_type_t<QueryTagT>::value_type, N>,
	typename EofErrorT = Errors::EarlyEofError>
struct TokenSetRule {
	using TokenType = typename queryable_type_t<QueryTagT>::value_type;
	std::array<TokenType, N> tokens;

	constexpr TokenSetRule(const TokenType (&arr)[N]) : tokens {std::to_array(arr)} {
		std::ranges::sort(tokens);
	}

	constexpr TokenSetRule(std::array<TokenType, N>&& arr) : tokens(std::move(arr)) {
		std::ranges::sort(tokens);
	}

	constexpr TokenSetRule(const std::array<TokenType, N>& arr) : tokens(arr) {
		std::ranges::sort(tokens);
	}

	using RequiredQuery = QueryTagT;
	using ValueType		= TokenType;
	using ErrorType		= unique_variant_t<SetErrorT, EofErrorT>;
	using ResultType	= tl::expected<ValueType, ErrorType>;

	constexpr auto match(Queryable<QueryTagT> auto&& state) const -> ResultType {
		auto& cursor = query<QueryTagT>(state);
		if (const auto token = cursor.peek(); token != queryable_type_t<QueryTagT>::eof) {
			size_t lo = 0;
			size_t hi = N;
			while (lo < hi) {
				const auto mid	= (lo + hi) >> 1;
				const auto cand = tokens[mid];
				if (token < cand)
					hi = mid;
				else if (token > cand)
					lo = mid + 1;
				else {
					cursor.bump();
					return token;
				}
			};
			return tl::make_unexpected<ErrorType>(SetErrorT {
				.mismatched = token,
				.expects	= tokens,
			});
		} else
			return tl::make_unexpected<ErrorType>(EofErrorT {});
	}
};

template<
	typename QueryTagT,
	typename RangeErrorT =
		Errors::TokenRangeMismatchError<typename queryable_type_t<QueryTagT>::value_type>,
	typename EofErrorT = Errors::EarlyEofError>
struct TokenRangeRule {
	using TokenType = typename queryable_type_t<QueryTagT>::value_type;
	TokenType lo;
	TokenType hi;

	using RequiredQuery = QueryTagT;
	using ValueType		= TokenType;
	using ErrorType		= unique_variant_t<RangeErrorT, EofErrorT>;
	using ResultType	= tl::expected<ValueType, ErrorType>;

	constexpr auto match(Queryable<QueryTagT> auto&& state) const -> ResultType {
		auto& cursor = query<QueryTagT>(state);
		if (const auto token = cursor.peek(); token != queryable_type_t<QueryTagT>::eof) {
			if (lo <= token && token <= hi) {
				cursor.bump();
				return token;
			}
			return tl::make_unexpected<ErrorType>(RangeErrorT {
				.mismatched = token,
				.lo			= lo,
				.hi			= hi,
			});
		}
		return tl::make_unexpected<ErrorType>(EofErrorT {});
	}
};

using AnyCharRule		   = AnyTokenRule<QueryParserCursor>;
inline constexpr auto cany = AnyCharRule {};

using CharRule			   = TokenRule<QueryParserCursor, Errors::CharMismatchError>;
using c					   = CharRule;

template<size_t N>
using CharSetRule = TokenSetRule<QueryParserCursor, N, Errors::CharSetMismatchError<N>>;

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

using CharRangeRule = TokenRangeRule<QueryParserCursor, Errors::CharRangeMismatchError>;
using cran			= CharRangeRule;

template<size_t N>
inline constexpr auto cstr(const char32_t (&str)[N]) {
	return [&]<size_t... Is>(std::index_sequence<Is...>) {
		return (c(str[Is]) >> ...)
			 % value_to([&](auto... cs) { return utf8::utf32to8(std::u32string_view {str}); });
	}(std::make_index_sequence<N - 1>());
}

template<typename TokenT>
inline constexpr auto tok(TokenT&& token)
	-> TokenRule<QueryTokenCursor<std::remove_cvref_t<TokenT>>> {
	return {std::forward<TokenT>(token)};
}

template<typename TokenT>
inline constexpr AnyTokenRule<QueryTokenCursor<TokenT>> anytok {};

template<typename TokenT, size_t N>
inline constexpr auto tokset(const TokenT (&arr)[N]) {
	return TokenSetRule<QueryTokenCursor<TokenT>, N> {arr};
}

template<typename TokenT0, typename... TokenTs>
inline constexpr auto tokset(TokenT0&& token0, TokenTs&&... tokens)
	-> TokenSetRule<QueryTokenCursor<std::remove_cvref_t<TokenT0>>, sizeof...(TokenTs) + 1> {
	using TokenT = std::remove_cvref_t<TokenT0>;
	static_assert((std::same_as<TokenT, std::remove_cvref_t<TokenTs>> && ...));
	return {
		std::array<TokenT, sizeof...(TokenTs) + 1> {
													std::forward<TokenT0>(token0),
													std::forward<TokenTs>(tokens)...,
													}
	};
}

template<typename TokenT>
inline constexpr auto tokran(TokenT&& lo, TokenT&& hi)
	-> TokenRangeRule<QueryTokenCursor<std::remove_cvref_t<TokenT>>> {
	return {
		std::forward<TokenT>(lo),
		std::forward<TokenT>(hi),
	};
}

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
					return to_string(std::forward<E>(e));
				}}
			);
		}
	};

	template<typename... Rs>
		requires(
			sizeof...(Rs) == sizeof...(RuleTs)
			&& (std::constructible_from<std::remove_cvref_t<RuleTs>, Rs &&> && ...)
		)
	constexpr SequentialRule(Rs&&... rules) : rules(std::forward<Rs>(rules)...) {}

	using RequiredQuery = RequiredQueryOfRules<RuleTs...>;
	using ValueType		= std::tuple<typename std::remove_cvref_t<RuleTs>::ValueType...>;
	using ErrorType		= SequentialSingleStageRuleError;
	using ResultType	= tl::expected<ValueType, ErrorType>;

	template<typename PolicyT = ResultPolicies::Normal>
	constexpr auto match(auto&& state, PolicyT policy = ResultPolicies::normal) const
		-> PoliciedResult<ValueType, ErrorType, PolicyT> {
		const auto snapshot =
			aggregate_snapshot_of_rules<RuleTs...>(std::forward<decltype(state)>(state));

		return rules |	//
			   apply([&](auto&&... rs) -> PoliciedResult<ValueType, ErrorType, PolicyT> {
				   [[maybe_unused]] PoliciedValue<ValueType, PolicyT> value;
				   [[maybe_unused]] PoliciedError<ErrorType, PolicyT> error;
				   if ([&]<size_t... Is>(std::index_sequence<Is...>) {
						   return ([&]() {
							   auto res = std::get<Is>(rules).match(state);
							   if (res) {
								   if constexpr (!std::
													 same_as<PolicyT, ResultPolicies::DiscardValue>)
									   std::get<Is>(value) = pars::value_of(std::move(res));
								   return true;
							   } else {
								   if constexpr (!std::
													 same_as<PolicyT, ResultPolicies::DiscardError>)
									   error = SequentialSingleStageRuleError {
										   .pos	  = Is,
										   .error = {pars::error_of(std::move(res))},
									   };
								   return false;
							   }
						   }() && ...);
					   }(std::make_index_sequence<sizeof...(RuleTs)>()))
					   return value;
				   else {
					   snapshot.rollback(state);
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

template<typename... RuleTs>
struct ChoiceRule;

template<typename RuleT>
struct OptionalRule;

template<typename RuleT>
struct RepeatableRule;

template<typename RuleT>
struct OnceOrMoreRule;

template<typename RuleT, typename FnT>
struct ValueTransformRule;

template<typename... RequiredQueryTs>
struct PolymorphicParserStateView;

template<typename ExtraRequiredQuerySigT, typename RuleT, typename FnT>
struct StateEffectRule;

template<typename RuleT>
struct PeekIsRule;

template<typename RuleT>
struct PeekNotRule;

template<typename ValueT, typename F>
struct FixRule;

template<typename RuleT>
using result_type_of_rule_t = typename std::remove_cvref_t<RuleT>::ResultType;

template<typename RuleT>
using value_type_of_rule_t = value_type_of_t<result_type_of_rule_t<RuleT>>;

template<typename RuleT>
using error_type_of_rule_t = error_type_of_t<result_type_of_rule_t<RuleT>>;

template<typename RuleT>
concept ParserRuleLike = requires {
	typename result_type_of_rule_t<RuleT>;
	typename value_type_of_rule_t<RuleT>;
	typename error_type_of_rule_t<RuleT>;
};

template<typename RuleT>
constexpr auto normalize_rule(RuleT&& rule);

template<bool Enabled = true, typename RuleT>
constexpr auto normalize(RuleT&& rule);

template<typename RuleT>
struct SemanticBarrier : std::true_type {};

template<typename QueryTagT, typename EofErrorT>
struct SemanticBarrier<AnyTokenRule<QueryTagT, EofErrorT>> : std::false_type {};

template<typename QueryTagT, typename ErrorT>
struct SemanticBarrier<TokenRule<QueryTagT, ErrorT>> : std::false_type {};

template<typename QueryTagT, size_t N, typename SetErrorT, typename EofErrorT>
struct SemanticBarrier<TokenSetRule<QueryTagT, N, SetErrorT, EofErrorT>> : std::false_type {};

template<typename QueryTagT, typename RangeErrorT, typename EofErrorT>
struct SemanticBarrier<TokenRangeRule<QueryTagT, RangeErrorT, EofErrorT>> : std::false_type {};

template<typename... RuleTs>
struct SemanticBarrier<SequentialRule<RuleTs...>> :
	std::bool_constant<(SemanticBarrier<std::remove_cvref_t<RuleTs>>::value || ...)> {};

template<typename... RuleTs>
struct SemanticBarrier<ChoiceRule<RuleTs...>> :
	std::bool_constant<(SemanticBarrier<std::remove_cvref_t<RuleTs>>::value || ...)> {};

template<typename RuleT>
struct SemanticBarrier<OptionalRule<RuleT>> : SemanticBarrier<std::remove_cvref_t<RuleT>> {};

template<typename RuleT>
struct SemanticBarrier<RepeatableRule<RuleT>> : SemanticBarrier<std::remove_cvref_t<RuleT>> {};

template<typename RuleT>
struct SemanticBarrier<OnceOrMoreRule<RuleT>> : SemanticBarrier<std::remove_cvref_t<RuleT>> {};

template<typename RuleT>
struct SemanticBarrier<PeekIsRule<RuleT>> : SemanticBarrier<std::remove_cvref_t<RuleT>> {};

template<typename RuleT>
struct SemanticBarrier<PeekNotRule<RuleT>> : SemanticBarrier<std::remove_cvref_t<RuleT>> {};

template<typename RuleT, typename FnT>
struct SemanticBarrier<ValueTransformRule<RuleT, FnT>> :
	SemanticBarrier<std::remove_cvref_t<RuleT>> {};

template<typename ExtraRequiredQuerySigT, typename RuleT, typename FnT>
struct SemanticBarrier<StateEffectRule<ExtraRequiredQuerySigT, RuleT, FnT>> : std::true_type {};

template<typename ValueT, typename F>
struct SemanticBarrier<FixRule<ValueT, F>> : std::true_type {};

template<typename RuleT>
inline constexpr bool semantic_barrier_v = SemanticBarrier<std::remove_cvref_t<RuleT>>::value;

template<typename RuleT>
struct IsSequentialRule : std::false_type {};

template<typename... RuleTs>
struct IsSequentialRule<SequentialRule<RuleTs...>> : std::true_type {};

template<typename RuleT>
inline constexpr bool is_sequential_rule_v = IsSequentialRule<std::remove_cvref_t<RuleT>>::value;

template<typename RuleT>
struct IsChoiceRule : std::false_type {};

template<typename... RuleTs>
struct IsChoiceRule<ChoiceRule<RuleTs...>> : std::true_type {};

template<typename RuleT>
inline constexpr bool is_choice_rule_v = IsChoiceRule<std::remove_cvref_t<RuleT>>::value;

template<typename RuleT>
struct IsValueTransformRule : std::false_type {};

template<typename RuleT, typename FnT>
struct IsValueTransformRule<ValueTransformRule<RuleT, FnT>> : std::true_type {};

template<typename RuleT>
inline constexpr bool is_value_transform_rule_v =
	IsValueTransformRule<std::remove_cvref_t<RuleT>>::value;

template<typename FnT, typename ValueT>
constexpr decltype(auto) invoke_transform_output(FnT&& fn, ValueT&& value) {
	if constexpr (tuple_like_v<std::remove_cvref_t<ValueT>>)
		return std::apply(std::forward<FnT>(fn), std::forward<ValueT>(value));
	else
		return std::invoke(std::forward<FnT>(fn), std::forward<ValueT>(value));
}

template<typename InnerFnT, typename OuterFnT>
struct ComposedValueTransform {
	InnerFnT inner;
	OuterFnT outer;

	template<typename... Args>
	constexpr decltype(auto) operator()(Args&&... args) const {
		return invoke_transform_output(outer, std::invoke(inner, std::forward<Args>(args)...));
	}
};

template<typename InnerFnT, typename OuterFnT>
ComposedValueTransform(InnerFnT&&, OuterFnT&&)
	-> ComposedValueTransform<std::remove_cvref_t<InnerFnT>, std::remove_cvref_t<OuterFnT>>;

template<typename TupleT>
constexpr auto make_sequential_from_tuple(TupleT&& rules) {
	constexpr auto N = std::tuple_size_v<std::remove_cvref_t<TupleT>>;
	if constexpr (N == 1)
		return std::get<0>(std::forward<TupleT>(rules));
	else
		return std::apply(
			[]<typename... RuleTs>(RuleTs&&... elems)
				-> SequentialRule<std::remove_cvref_t<RuleTs>...> {
				return {std::forward<RuleTs>(elems)...};
			},
			std::forward<TupleT>(rules)
		);
}

template<typename TupleT>
constexpr auto make_choice_from_tuple(TupleT&& rules) {
	constexpr auto N = std::tuple_size_v<std::remove_cvref_t<TupleT>>;
	if constexpr (N == 1)
		return std::get<0>(std::forward<TupleT>(rules));
	else
		return std::apply(
			[]<typename... RuleTs>(RuleTs&&... elems)
				-> ChoiceRule<std::remove_cvref_t<RuleTs>...> {
				return {std::forward<RuleTs>(elems)...};
			},
			std::forward<TupleT>(rules)
		);
}

template<typename RuleT>
constexpr auto as_sequential_tuple(RuleT&& rule) {
	auto normalized = normalize_rule(std::forward<RuleT>(rule));
	if constexpr (is_sequential_rule_v<decltype(normalized)>)
		return std::move(normalized.rules);
	else
		return std::tuple {std::move(normalized)};
}

template<typename RuleT>
constexpr auto as_choice_tuple(RuleT&& rule) {
	auto normalized = normalize_rule(std::forward<RuleT>(rule));
	if constexpr (is_choice_rule_v<decltype(normalized)>)
		return std::move(normalized.rules);
	else
		return std::tuple {std::move(normalized)};
}

template<typename RuleT>
constexpr auto normalize_once(RuleT&& rule) {
	return std::forward<RuleT>(rule);
}

template<typename... RuleTs>
struct ChoiceRule {
	std::tuple<std::remove_cvref_t<RuleTs>...> rules;

	using RequiredQuery = RequiredQueryOfRules<RuleTs...>;
	using ValueType		= simplify_single_type_variant_t<
		unique_variant_t<typename std::remove_cvref_t<RuleTs>::ValueType...>>;
	using ErrorType	 = std::tuple<typename std::remove_cvref_t<RuleTs>::ErrorType...>;
	using ResultType = tl::expected<ValueType, ErrorType>;

	template<typename... Rs>
		requires(
			sizeof...(Rs) == sizeof...(RuleTs)
			&& (std::constructible_from<std::remove_cvref_t<RuleTs>, Rs &&> && ...)
		)
	constexpr ChoiceRule(Rs&&... rules) : rules(std::forward<Rs>(rules)...) {}

	constexpr auto match(auto&& state) const -> ResultType {
		const auto snapshot =
			aggregate_snapshot_of_rules<RuleTs...>(std::forward<decltype(state)>(state));

		ErrorType errors {};
		if (auto res = [&]<size_t... Is>(std::index_sequence<Is...>) -> std::optional<ResultType> {
				std::optional<ResultType> final;
				(
					[&]() {
						if (final)
							return;
						auto res = std::get<Is>(rules).match(state);
						if (res)
							final = ResultType {ValueType {pars::value_of(std::move(res))}};
						else {
							std::get<Is>(errors) = pars::error_of(std::move(res));
							snapshot.rollback(state);
						}
					}(),
					...);
				return final;
			}(std::make_index_sequence<sizeof...(RuleTs)>()))
			return std::move(*res);
		return tl::make_unexpected<ErrorType>(std::move(errors));
	}
};

template<typename... RuleTs>
ChoiceRule(RuleTs&&...) -> ChoiceRule<std::remove_cvref_t<RuleTs>...>;

template<typename... RuleTs>
constexpr auto normalize_once(SequentialRule<RuleTs...>&& rule) {
	auto flattened = std::apply(
		[]<typename... Rs>(Rs&&... rules) {
			return std::tuple_cat(as_sequential_tuple(std::forward<Rs>(rules))...);
		},
		std::move(rule.rules)
	);
	return make_sequential_from_tuple(std::move(flattened));
}

template<typename... RuleTs>
constexpr auto normalize_once(const SequentialRule<RuleTs...>& rule) {
	auto flattened = std::apply(
		[]<typename... Rs>(Rs&&... rules) {
			return std::tuple_cat(as_sequential_tuple(std::forward<Rs>(rules))...);
		},
		rule.rules
	);
	return make_sequential_from_tuple(std::move(flattened));
}

template<typename... RuleTs>
constexpr auto normalize_once(ChoiceRule<RuleTs...>&& rule) {
	auto flattened = std::apply(
		[]<typename... Rs>(Rs&&... rules) {
			return std::tuple_cat(as_choice_tuple(std::forward<Rs>(rules))...);
		},
		std::move(rule.rules)
	);
	return make_choice_from_tuple(std::move(flattened));
}

template<typename... RuleTs>
constexpr auto normalize_once(const ChoiceRule<RuleTs...>& rule) {
	auto flattened = std::apply(
		[]<typename... Rs>(Rs&&... rules) {
			return std::tuple_cat(as_choice_tuple(std::forward<Rs>(rules))...);
		},
		rule.rules
	);
	return make_choice_from_tuple(std::move(flattened));
}

template<typename RuleT>
struct OptionalRule {
	RuleT rule;

	using RequiredQuery	 = RequiredQueryOfRule<RuleT>;
	using InnerValueType = typename RuleT::ValueType;
	using ValueType		 = std::optional<InnerValueType>;
	using ErrorType		 = Ignore;
	using ResultType	 = tl::expected<ValueType, ErrorType>;

	constexpr auto match(auto&& state) const -> ResultType {
		const auto snapshot = aggregate_snapshot_of_rules<RuleT>(state);
		if (auto res = rule.match(state))
			return ValueType {pars::value_of(std::move(res))};
		snapshot.rollback(state);
		return ValueType {};
	}
};

template<typename RuleT>
OptionalRule(RuleT&&) -> OptionalRule<std::remove_cvref_t<RuleT>>;

template<typename RuleT>
constexpr auto normalize_once(OptionalRule<RuleT>&& rule) {
	auto normalized = normalize_rule(std::move(rule.rule));
	if constexpr (std::same_as<std::remove_cvref_t<decltype(normalized)>, RuleT>)
		return rule;
	else
		return OptionalRule {std::move(normalized)};
}

template<typename RuleT>
constexpr auto normalize_once(const OptionalRule<RuleT>& rule) {
	auto normalized = normalize_rule(rule.rule);
	if constexpr (std::same_as<std::remove_cvref_t<decltype(normalized)>, RuleT>)
		return rule;
	else
		return OptionalRule {std::move(normalized)};
}

template<typename RuleT>
struct RepeatableRule {
	RuleT rule;

	using RequiredQuery = RequiredQueryOfRule<RuleT>;
	using ValueType		= std::vector<typename RuleT::ValueType>;
	using ErrorType		= Ignore;
	using ResultType	= tl::expected<ValueType, ErrorType>;

	constexpr auto match(auto&& state) const -> ResultType {
		ValueType values;
		while (true) {
			const auto snapshot = aggregate_snapshot_of_rules<RuleT>(state);
			auto	   res		= rule.match(state);
			if (!res) {
				snapshot.rollback(state);
				break;
			}
			values.emplace_back(pars::value_of(std::move(res)));
		}
		return values;
	}
};

template<typename RuleT>
RepeatableRule(RuleT&&) -> RepeatableRule<std::remove_cvref_t<RuleT>>;

template<typename RuleT>
constexpr auto normalize_once(RepeatableRule<RuleT>&& rule) {
	auto normalized = normalize_rule(std::move(rule.rule));
	if constexpr (std::same_as<std::remove_cvref_t<decltype(normalized)>, RuleT>)
		return rule;
	else
		return RepeatableRule {std::move(normalized)};
}

template<typename RuleT>
constexpr auto normalize_once(const RepeatableRule<RuleT>& rule) {
	auto normalized = normalize_rule(rule.rule);
	if constexpr (std::same_as<std::remove_cvref_t<decltype(normalized)>, RuleT>)
		return rule;
	else
		return RepeatableRule {std::move(normalized)};
}

template<typename RuleT>
struct OnceOrMoreRule {
	RuleT rule;

	using RequiredQuery = RequiredQueryOfRule<RuleT>;
	using ValueType		= std::vector<typename RuleT::ValueType>;
	using ErrorType		= typename RuleT::ErrorType;
	using ResultType	= tl::expected<ValueType, ErrorType>;

	constexpr auto match(auto&& state) const -> ResultType {
		ValueType values;
		auto	  first = rule.match(state);
		if (!first)
			return tl::make_unexpected<ErrorType>(pars::error_of(std::move(first)));
		values.emplace_back(pars::value_of(std::move(first)));
		while (true) {
			const auto snapshot = aggregate_snapshot_of_rules<RuleT>(state);
			auto	   res		= rule.match(state);
			if (!res) {
				snapshot.rollback(state);
				break;
			}
			values.emplace_back(pars::value_of(std::move(res)));
		}
		return values;
	}
};

template<typename RuleT>
OnceOrMoreRule(RuleT&&) -> OnceOrMoreRule<std::remove_cvref_t<RuleT>>;

template<typename RuleT>
constexpr auto normalize_once(OnceOrMoreRule<RuleT>&& rule) {
	auto normalized = normalize_rule(std::move(rule.rule));
	if constexpr (std::same_as<std::remove_cvref_t<decltype(normalized)>, RuleT>)
		return rule;
	else
		return OnceOrMoreRule {std::move(normalized)};
}

template<typename RuleT>
constexpr auto normalize_once(const OnceOrMoreRule<RuleT>& rule) {
	auto normalized = normalize_rule(rule.rule);
	if constexpr (std::same_as<std::remove_cvref_t<decltype(normalized)>, RuleT>)
		return rule;
	else
		return OnceOrMoreRule {std::move(normalized)};
}

template<typename RuleT, typename FnT>
struct ValueTransformRule {
	RuleT rule;
	FnT	  fn;

	using RequiredQuery = RequiredQueryOfRule<RuleT>;
	using InputValue	= typename RuleT::ValueType;
	using ValueType		= auto_tuple_apply_result_t<FnT, InputValue>;
	using ErrorType		= typename RuleT::ErrorType;
	using ResultType	= tl::expected<ValueType, ErrorType>;

	constexpr auto match(auto&& state) const -> ResultType {
		auto res = rule.match(state);
		if (!res)
			return tl::make_unexpected<ErrorType>(pars::error_of(std::move(res)));
		if constexpr (tuple_like_v<InputValue>)
			return std::apply(fn, pars::value_of(std::move(res)));
		else
			return std::invoke(fn, pars::value_of(std::move(res)));
	}
};

template<typename RuleT, typename FnT>
ValueTransformRule(RuleT&&, FnT&&)
	-> ValueTransformRule<std::remove_cvref_t<RuleT>, std::remove_cvref_t<FnT>>;

template<typename RuleT, typename FnT>
constexpr auto normalize_once(ValueTransformRule<RuleT, FnT>&& rule) {
	auto normalized = normalize_rule(std::move(rule.rule));
	if constexpr (is_value_transform_rule_v<decltype(normalized)>) {
		auto composed = ComposedValueTransform {
			std::move(normalized.fn),
			std::move(rule.fn),
		};
		return normalize_rule(std::move(normalized.rule) % value_to(std::move(composed)));
	} else if constexpr (std::same_as<std::remove_cvref_t<decltype(normalized)>, RuleT>)
		return rule;
	else
		return ValueTransformRule {std::move(normalized), std::move(rule.fn)};
}

template<typename RuleT, typename FnT>
constexpr auto normalize_once(const ValueTransformRule<RuleT, FnT>& rule) {
	auto normalized = normalize_rule(rule.rule);
	if constexpr (is_value_transform_rule_v<decltype(normalized)>) {
		auto composed = ComposedValueTransform {
			std::move(normalized.fn),
			rule.fn,
		};
		return normalize_rule(std::move(normalized.rule) % value_to(std::move(composed)));
	} else if constexpr (std::same_as<std::remove_cvref_t<decltype(normalized)>, RuleT>)
		return rule;
	else
		return ValueTransformRule {std::move(normalized), rule.fn};
}

template<typename FnT>
struct ValueTransformFunctor {
	FnT fn;
};

template<typename FnT>
ValueTransformFunctor(FnT&&) -> ValueTransformFunctor<std::remove_cvref_t<FnT>>;

template<typename FnT>
inline constexpr auto value_to(FnT&& fn) -> ValueTransformFunctor<std::remove_cvref_t<FnT>> {
	return {std::forward<FnT>(fn)};
}

template<typename ExtraRequiredQuerySigT, typename RuleT, typename FnT>
struct StateEffectRule {
	RuleT rule;
	FnT	  fn;

	using RequiredQuery = signature_cup_t<RequiredQueryOfRule<RuleT>, ExtraRequiredQuerySigT>;
	using ValueType		= typename RuleT::ValueType;
	using StateView		= apply_signature_t<PolymorphicParserStateView, RequiredQuery>;

private:
	using EffectResult = std::invoke_result_t<FnT, StateView, ValueType&>;

	template<typename T>
	static constexpr bool effect_returns_error_v = !std::same_as<T, void> && requires(T& value) {
		{ static_cast<bool>(value) } -> std::convertible_to<bool>;
		pars::error_of(value);
	};

	template<typename RuleErrorT, typename EffectResultT, bool HasEffectError>
	struct StateEffectError {
		using type = RuleErrorT;
	};

	template<typename RuleErrorT, typename EffectResultT>
	struct StateEffectError<RuleErrorT, EffectResultT, true> {
		using type = unique_variant_t<RuleErrorT, error_type_of_t<EffectResultT>>;
	};

public:
	using ErrorType = typename StateEffectError<
		typename RuleT::ErrorType, EffectResult, effect_returns_error_v<EffectResult>>::type;
	using ResultType = tl::expected<ValueType, ErrorType>;

	constexpr auto match(auto&& state) const -> ResultType {
		auto res = rule.match(state);
		if (!res)
			return tl::make_unexpected<ErrorType>(pars::error_of(std::move(res)));
		auto value = pars::value_of(std::move(res));
		if constexpr (std::same_as<EffectResult, void>)
			std::invoke(fn, StateView {state}, value);
		else {
			auto effect_res = std::invoke(fn, StateView {state}, value);
			if constexpr (effect_returns_error_v<EffectResult>) {
				if (!effect_res)
					return tl::make_unexpected<ErrorType>(pars::error_of(std::move(effect_res)));
			}
		}
		return value;
	}
};

template<typename ExtraRequiredQuerySigT, typename FnT>
struct StateEffectFunctor {
	FnT fn;
};

template<typename ExtraRequiredQuerySigT, typename FnT>
inline constexpr auto with_effect(FnT&& fn)
	-> StateEffectFunctor<ExtraRequiredQuerySigT, std::remove_cvref_t<FnT>> {
	return {std::forward<FnT>(fn)};
}

template<typename ExtraRequiredQuerySigT, typename RuleT, typename FnT>
constexpr auto normalize_once(StateEffectRule<ExtraRequiredQuerySigT, RuleT, FnT>&& rule) {
	auto normalized = normalize_rule(std::move(rule.rule));
	if constexpr (std::same_as<std::remove_cvref_t<decltype(normalized)>, RuleT>)
		return rule;
	else
		return StateEffectRule<
			ExtraRequiredQuerySigT,
			std::remove_cvref_t<decltype(normalized)>,
			FnT> {
			std::move(normalized),
			std::move(rule.fn),
		};
}

template<typename ExtraRequiredQuerySigT, typename RuleT, typename FnT>
constexpr auto normalize_once(const StateEffectRule<ExtraRequiredQuerySigT, RuleT, FnT>& rule) {
	auto normalized = normalize_rule(rule.rule);
	if constexpr (std::same_as<std::remove_cvref_t<decltype(normalized)>, RuleT>)
		return rule;
	else
		return StateEffectRule<
			ExtraRequiredQuerySigT,
			std::remove_cvref_t<decltype(normalized)>,
			FnT> {
			std::move(normalized),
			rule.fn,
		};
}

// template<typename ValueT, typename ErrorT, typename... RequiredQuerySigT>
// struct LiteralRule {
// 	// using RequiredQueryT = a;
// };

template<typename RuleT>
struct PeekIsRule {
	RuleT rule;

	using RequiredQuery = RequiredQueryOfRule<RuleT>;
	using ValueType		= std::monostate;
	using ErrorType		= typename RuleT::ErrorType;
	using ResultType	= tl::expected<ValueType, ErrorType>;

	constexpr auto match(auto&& state) const -> ResultType {
		const auto snapshot = aggregate_snapshot_of_rules<RuleT>(state);
		auto	   res		= rule.match(state);
		snapshot.rollback(state);
		if (res)
			return std::monostate {};
		return tl::make_unexpected<ErrorType>(pars::error_of(std::move(res)));
	}
};

template<typename RuleT>
PeekIsRule(RuleT&&) -> PeekIsRule<std::remove_cvref_t<RuleT>>;

template<typename RuleT>
constexpr auto normalize_once(PeekIsRule<RuleT>&& rule) {
	auto normalized = normalize_rule(std::move(rule.rule));
	if constexpr (std::same_as<std::remove_cvref_t<decltype(normalized)>, RuleT>)
		return rule;
	else
		return PeekIsRule {std::move(normalized)};
}

template<typename RuleT>
constexpr auto normalize_once(const PeekIsRule<RuleT>& rule) {
	auto normalized = normalize_rule(rule.rule);
	if constexpr (std::same_as<std::remove_cvref_t<decltype(normalized)>, RuleT>)
		return rule;
	else
		return PeekIsRule {std::move(normalized)};
}

template<typename RuleT>
struct PeekNotRule {
	RuleT rule;

	using RequiredQuery = RequiredQueryOfRule<RuleT>;
	using ValueType		= std::monostate;
	using ErrorType		= unique_variant_t<Errors::RuleMatchedError, Errors::EarlyEofError>;
	using ResultType	= tl::expected<ValueType, ErrorType>;

	constexpr auto match(auto&& state) const -> ResultType {
		const auto snapshot = aggregate_snapshot_of_rules<RuleT>(state);
		auto	   res		= rule.match(state);
		snapshot.rollback(state);
		if (res)
			return tl::make_unexpected<ErrorType>(Errors::RuleMatchedError {});
		if (!query_parser_cursor(state).peek())
			return tl::make_unexpected<ErrorType>(Errors::EarlyEofError {});
		return std::monostate {};
	}
};

template<typename RuleT>
PeekNotRule(RuleT&&) -> PeekNotRule<std::remove_cvref_t<RuleT>>;

template<typename RuleT>
constexpr auto normalize_once(PeekNotRule<RuleT>&& rule) {
	auto normalized = normalize_rule(std::move(rule.rule));
	if constexpr (std::same_as<std::remove_cvref_t<decltype(normalized)>, RuleT>)
		return rule;
	else
		return PeekNotRule {std::move(normalized)};
}

template<typename RuleT>
constexpr auto normalize_once(const PeekNotRule<RuleT>& rule) {
	auto normalized = normalize_rule(rule.rule);
	if constexpr (std::same_as<std::remove_cvref_t<decltype(normalized)>, RuleT>)
		return rule;
	else
		return PeekNotRule {std::move(normalized)};
}

template<typename ValueT, typename Fn>
struct FixRule {
	Fn fn;

private:
	struct Self {
		const FixRule* owner;

		using RequiredQuery = required_query_t();
		using ValueType		= std::unique_ptr<ValueT>;
		using ErrorType		= Ignore;
		using ResultType	= tl::expected<ValueType, ErrorType>;

		template<typename StateT>
		constexpr auto match(StateT&& st) const -> ResultType {
			auto res = owner->match(std::forward<StateT>(st));
			if (res)
				return pars::value_of(std::move(res));
			return tl::make_unexpected<ErrorType>(ErrorType {});
		}
	};

	using InnerRule =
		std::remove_cvref_t<decltype(std::declval<const Fn&>()(std::declval<Self>()))>;
	using InnerValueType = value_type_of_rule_t<InnerRule>;

	static_assert(ParserRuleLike<InnerRule>, "fix<T>(fn): fn(self) must return a parser rule");
	static_assert(
		std::same_as<InnerValueType, ValueT>
			|| std::same_as<InnerValueType, std::unique_ptr<ValueT>>,
		"fix<T>(fn): fn(self) must return a rule whose ValueType is either T or std::unique_ptr<T>."
	);

public:
	using RequiredQuery = RequiredQueryOfRule<InnerRule>;
	using ValueType		= std::unique_ptr<ValueT>;
	using ErrorType		= error_type_of_rule_t<InnerRule>;
	using ResultType	= tl::expected<ValueType, ErrorType>;

	constexpr auto match(auto&& st) const -> ResultType {
		auto res = fn(Self {this}).match(std::forward<decltype(st)>(st));
		if (!res)
			return tl::make_unexpected<ErrorType>(pars::error_of(std::move(res)));
		if constexpr (std::same_as<InnerValueType, ValueType>)
			return pars::value_of(std::move(res));
		else
			return std::make_unique<ValueT>(pars::value_of(std::move(res)));
	}
};

template<typename ValueT, typename Fn>
inline constexpr auto fix(Fn&& fn) -> FixRule<ValueT, std::remove_cvref_t<Fn>> {
	return {std::forward<Fn>(fn)};
}

template<typename RequiredQueryT>
struct NormalizeRequiredQuery {
	using type = std::conditional_t<
		signature_based_on<RequiredQueryT, required_query_t>::value, RequiredQueryT,
		required_query_t(RequiredQueryT)>;
};

template<typename RequiredQueryT>
using normalize_required_query_t = typename NormalizeRequiredQuery<RequiredQueryT>::type;

template<typename RequiredQueryT>
struct CanonicalDynamicRuleRequiredQueryParam {
	using type = RequiredQueryT;
};

template<typename QueryTagT>
struct CanonicalDynamicRuleRequiredQueryParam<required_query_t(QueryTagT)> {
	using type = QueryTagT;
};

template<typename RequiredQueryT>
using canonical_dynamic_rule_required_query_param_t =
	typename CanonicalDynamicRuleRequiredQueryParam<RequiredQueryT>::type;

template<typename T>
concept DynamicRuleResultLike = TagInvocable<std::remove_cvref_t<T>&, value_of_t>
							 && TagInvocable<std::remove_cvref_t<T>&, error_of_t>;

template<typename T>
using normalize_dynamic_rule_result_t = std::conditional_t<
	DynamicRuleResultLike<std::remove_cvref_t<T>>, std::remove_cvref_t<T>,
	tl::expected<std::remove_cvref_t<T>, DynamicError>>;

template<typename... RequiredQueryTs>
struct PolymorphicParserStateView {
	template<typename QueryTagT>
	struct QueryableSlot {
		queryable_type_t<QueryTagT>* ptr;
	};

	constexpr explicit PolymorphicParserStateView(QueryableAll<RequiredQueryTs...> auto&& st) :
		vtable {QueryableSlot<RequiredQueryTs> {std::addressof(query<RequiredQueryTs>(st))}...} {}

	template<typename QueryT>
	constexpr auto& query(QueryT) const {
		return *std::get<QueryableSlot<QueryT>>(vtable).ptr;
	}

	std::tuple<QueryableSlot<RequiredQueryTs>...> vtable;
};

template<typename ResultT, typename RequiredQueryT = QueryParserCursor>
struct DynamicRule {
	using ResultType	= normalize_dynamic_rule_result_t<ResultT>;
	using RequiredQuery = normalize_required_query_t<RequiredQueryT>;
	using ValueType		= value_type_of_t<ResultType>;
	using ErrorType		= error_type_of_t<ResultType>;

private:
	using StateView = apply_signature_t<PolymorphicParserStateView, RequiredQuery>;
	using MatchFn	= ResultType (*)(const void*, StateView);

public:
	using StateViewType		= StateView;

	const void* target		= nullptr;
	MatchFn		match_fn	= nullptr;

	constexpr DynamicRule() = default;

	template<typename RuleT>
		requires(ParserRuleLike<RuleT> && std::same_as<result_type_of_rule_t<RuleT>, ResultType>
				 && std::same_as<
					 normalize_required_query_t<RequiredQueryOfRule<std::remove_cvref_t<RuleT>>>,
					 RequiredQuery>)
	constexpr DynamicRule(const RuleT& rule) :
		target(std::addressof(rule)), match_fn([](const void* target, StateView st) -> ResultType {
			return static_cast<const std::remove_cvref_t<RuleT>*>(target)->match(std::move(st));
		}) {}

	template<typename RuleT>
		requires(ParserRuleLike<RuleT> && !std::same_as<result_type_of_rule_t<RuleT>, ResultType>
				 && std::same_as<value_type_of_rule_t<RuleT>, ValueType>
				 && std::same_as<ErrorType, DynamicError>
				 && std::same_as<
					 normalize_required_query_t<RequiredQueryOfRule<std::remove_cvref_t<RuleT>>>,
					 RequiredQuery>)
	constexpr DynamicRule(const RuleT& rule) :
		target(std::addressof(rule)), match_fn([](const void* target, StateView st) -> ResultType {
			auto res = static_cast<const std::remove_cvref_t<RuleT>*>(target)->match(std::move(st));
			if (res)
				return pars::value_of(std::move(res));
			return tl::make_unexpected<DynamicError>(pars::error_of(std::move(res)));
		}) {}

	template<typename OtherResultT, typename OtherRequiredQueryT>
		requires(!std::same_as<DynamicRule<OtherResultT, OtherRequiredQueryT>, DynamicRule>
				 && std::same_as<
					 typename DynamicRule<OtherResultT, OtherRequiredQueryT>::ValueType, ValueType>
				 && std::same_as<ErrorType, DynamicError>
				 && std::same_as<
					 typename DynamicRule<OtherResultT, OtherRequiredQueryT>::RequiredQuery,
					 RequiredQuery>)
	constexpr DynamicRule(const DynamicRule<OtherResultT, OtherRequiredQueryT>& rule) :
		target(std::addressof(rule)), match_fn([](const void* target, StateView st) -> ResultType {
			auto res =
				static_cast<const DynamicRule<OtherResultT, OtherRequiredQueryT>*>(target)->match(
					std::move(st)
				);
			if (res)
				return pars::value_of(std::move(res));
			return tl::make_unexpected<DynamicError>(pars::error_of(std::move(res)));
		}) {}

	template<typename StateT>
		requires QueryableAllSigC<std::remove_cvref_t<StateT>, RequiredQuery>
	constexpr auto match(StateT&& st) const -> ResultType {
		return match_fn(target, StateView {std::forward<decltype(st)>(st)});
	}
};

template<ParserRuleLike RuleT>
DynamicRule(const RuleT&)
	-> DynamicRule<result_type_of_rule_t<RuleT>, RequiredQueryOfRule<std::remove_cvref_t<RuleT>>>;

template<typename ResultT, typename RequiredQueryT = QueryParserCursor>
struct DynamicRuleReference {
	using RequiredQuery = normalize_required_query_t<RequiredQueryT>;
	using ResultType	= normalize_dynamic_rule_result_t<ResultT>;
	using ValueType		= value_type_of_t<ResultType>;
	using ErrorType		= error_type_of_t<ResultType>;

	const DynamicRule<ResultT, RequiredQueryT>* rule;

	constexpr auto								match(auto&& st) const -> ResultType {
		return rule->match(std::forward<decltype(st)>(st));
	}
};

template<typename RuleT>
struct RuleStorage {
	using type = std::remove_cvref_t<RuleT>;

	static constexpr auto store(RuleT&& rule) -> type { return std::forward<RuleT>(rule); }
};

template<typename ResultT, typename RequiredQueryT>
struct RuleStorage<DynamicRule<ResultT, RequiredQueryT>&> {
	using type = DynamicRuleReference<ResultT, RequiredQueryT>;

	static constexpr auto store(const DynamicRule<ResultT, RequiredQueryT>& rule) -> type {
		return {std::addressof(rule)};
	}
};

template<typename ResultT, typename RequiredQueryT>
struct RuleStorage<const DynamicRule<ResultT, RequiredQueryT>&> :
	RuleStorage<DynamicRule<ResultT, RequiredQueryT>&> {};

template<typename RuleT>
using stored_rule_t = typename RuleStorage<RuleT>::type;

template<typename RuleT>
constexpr auto store_rule(RuleT&& rule) -> stored_rule_t<RuleT> {
	return RuleStorage<RuleT>::store(std::forward<RuleT>(rule));
}

template<ParserRuleLike RuleT>
inline constexpr auto dyna(const RuleT& rule) -> DynamicRule<
	result_type_of_rule_t<RuleT>, canonical_dynamic_rule_required_query_param_t<
									  RequiredQueryOfRule<std::remove_cvref_t<RuleT>>>> {
	using DynamicRuleT = DynamicRule<
		result_type_of_rule_t<RuleT>,
		canonical_dynamic_rule_required_query_param_t<
			RequiredQueryOfRule<std::remove_cvref_t<RuleT>>>>;
	return DynamicRuleT(rule);
}

template<typename ResultT, ParserRuleLike RuleT>
inline constexpr auto dyna(const RuleT& rule) -> DynamicRule<
	ResultT, canonical_dynamic_rule_required_query_param_t<
				 RequiredQueryOfRule<std::remove_cvref_t<RuleT>>>> {
	using DynamicRuleT = DynamicRule<
		ResultT,
		canonical_dynamic_rule_required_query_param_t<
			RequiredQueryOfRule<std::remove_cvref_t<RuleT>>>>;
	if constexpr (std::same_as<typename DynamicRuleT::ResultType, result_type_of_rule_t<RuleT>>) {
		return DynamicRuleT(rule);
	} else {
		static_assert(std::same_as<value_type_of_rule_t<RuleT>, typename DynamicRuleT::ValueType>);
		static_assert(std::same_as<typename DynamicRuleT::ErrorType, DynamicError>);

		return DynamicRuleT {
			.target	  = std::addressof(rule),
			.match_fn = [](const void* target, typename DynamicRuleT::StateViewType st) ->
			typename DynamicRuleT::ResultType {
				auto res =
					static_cast<const std::remove_cvref_t<RuleT>*>(target)->match(std::move(st));
				if (res)
					return pars::value_of(std::move(res));
				return tl::make_unexpected<DynamicError>(pars::error_of(std::move(res)));
			},
		};
	}
}

template<typename RuleT>
inline constexpr auto normalize_rule(RuleT&& rule) {
	auto normalized = normalize_once(std::forward<RuleT>(rule));
	if constexpr (
		std::same_as<std::remove_cvref_t<decltype(normalized)>, std::remove_cvref_t<RuleT>>
	)
		return normalized;
	else
		return normalize_rule(std::move(normalized));
}

template<bool Enabled, typename RuleT>
constexpr auto normalize(RuleT&& rule) {
	if constexpr (Enabled)
		return normalize_rule(std::forward<RuleT>(rule));
	else
		return std::forward<RuleT>(rule);
}

template<ParserRuleLike RuleTL, ParserRuleLike RuleTR>
inline constexpr auto operator>>(RuleTL&& l, RuleTR&& r) {
	return SequentialRule<stored_rule_t<RuleTL>, stored_rule_t<RuleTR>> {
		store_rule(std::forward<RuleTL>(l)),
		store_rule(std::forward<RuleTR>(r)),
	};
}

template<typename... RuleTLs, ParserRuleLike RuleTR>
inline constexpr auto operator>>(SequentialRule<RuleTLs...>&& l, RuleTR&& r) {
	return SequentialRule<stored_rule_t<decltype(l)>, stored_rule_t<RuleTR>> {
		store_rule(std::move(l)),
		store_rule(std::forward<RuleTR>(r)),
	};
}

template<ParserRuleLike RuleTL, typename... RuleTRs>
inline constexpr auto operator>>(RuleTL&& l, SequentialRule<RuleTRs...>&& r) {
	return SequentialRule<stored_rule_t<RuleTL>, stored_rule_t<decltype(r)>> {
		store_rule(std::forward<RuleTL>(l)),
		store_rule(std::move(r)),
	};
}

template<typename... RuleTLs, typename... RuleTRs>
inline constexpr auto operator>>(SequentialRule<RuleTLs...>&& l, SequentialRule<RuleTRs...>&& r) {
	return SequentialRule<stored_rule_t<decltype(l)>, stored_rule_t<decltype(r)>> {
		store_rule(std::move(l)),
		store_rule(std::move(r)),
	};
}

template<ParserRuleLike RuleTL, ParserRuleLike RuleTR>
inline constexpr auto operator|(RuleTL&& l, RuleTR&& r) {
	return ChoiceRule<stored_rule_t<RuleTL>, stored_rule_t<RuleTR>> {
		store_rule(std::forward<RuleTL>(l)),
		store_rule(std::forward<RuleTR>(r)),
	};
}

template<typename... RuleTLs, ParserRuleLike RuleTR>
inline constexpr auto operator|(ChoiceRule<RuleTLs...>&& l, RuleTR&& r) {
	return std::apply(
		[&]<typename... Rs>(Rs&&... rules) {
			return ChoiceRule<RuleTLs..., stored_rule_t<RuleTR>> {
				std::forward<Rs>(rules)...,
				store_rule(std::forward<RuleTR>(r)),
			};
		},
		std::move(l.rules)
	);
}

template<ParserRuleLike RuleTL, typename... RuleTRs>
inline constexpr auto operator|(RuleTL&& l, ChoiceRule<RuleTRs...>&& r) {
	return std::apply(
		[&]<typename... Rs>(Rs&&... rules) {
			return ChoiceRule<stored_rule_t<RuleTL>, RuleTRs...> {
				store_rule(std::forward<RuleTL>(l)),
				std::forward<Rs>(rules)...,
			};
		},
		std::move(r.rules)
	);
}

template<typename... RuleTLs, typename... RuleTRs>
inline constexpr auto operator|(ChoiceRule<RuleTLs...>&& l, ChoiceRule<RuleTRs...>&& r) {
	return std::apply(
		[&]<typename... LRs>(LRs&&... lrules) {
			return std::apply(
				[&]<typename... RRs>(RRs&&... rrules) {
					return ChoiceRule<RuleTLs..., RuleTRs...> {
						std::forward<LRs>(lrules)...,
						std::forward<RRs>(rrules)...,
					};
				},
				std::move(r.rules)
			);
		},
		std::move(l.rules)
	);
}

template<ParserRuleLike RuleT>
[[nodiscard]] inline constexpr auto operator-(RuleT&& rule) {
	return OptionalRule<stored_rule_t<RuleT>> {store_rule(std::forward<RuleT>(rule))};
}

template<ParserRuleLike RuleT>
[[nodiscard]] inline constexpr auto operator~(RuleT&& rule) {
	return PeekIsRule<stored_rule_t<RuleT>> {store_rule(std::forward<RuleT>(rule))};
}

template<ParserRuleLike RuleT>
[[nodiscard]] inline constexpr auto operator!(RuleT&& rule) {
	return PeekNotRule<stored_rule_t<RuleT>> {store_rule(std::forward<RuleT>(rule))};
}

template<ParserRuleLike RuleT>
[[nodiscard]] inline constexpr auto operator*(RuleT&& rule) {
	return RepeatableRule<stored_rule_t<RuleT>> {store_rule(std::forward<RuleT>(rule))};
}

template<ParserRuleLike RuleT>
[[nodiscard]] inline constexpr auto operator+(RuleT&& rule) {
	return OnceOrMoreRule<stored_rule_t<RuleT>> {store_rule(std::forward<RuleT>(rule))};
}

template<ParserRuleLike RuleT, typename FnT>
[[nodiscard]] inline constexpr auto operator%(RuleT&& rule, ValueTransformFunctor<FnT>&& fn) {
	return [&]<typename NormalizedRuleT>(NormalizedRuleT&& normalized) constexpr {
		if constexpr (is_value_transform_rule_v<NormalizedRuleT>) {
			auto composed = ComposedValueTransform {
				std::move(normalized.fn),
				std::move(fn.fn),
			};
			return ValueTransformRule<
				stored_rule_t<decltype(normalized.rule)>,
				decltype(composed)> {
				store_rule(std::move(normalized.rule)),
				std::move(composed),
			};
		} else {
			return ValueTransformRule<stored_rule_t<NormalizedRuleT>, FnT> {
				store_rule(std::forward<NormalizedRuleT>(normalized)),
				std::move(fn.fn),
			};
		}
	}(normalize_rule(std::forward<RuleT>(rule)));
}

template<ParserRuleLike RuleT, typename FnT>
[[nodiscard]] inline constexpr auto operator^(RuleT&& rule, ValueTransformFunctor<FnT>&& fn) {
	return std::forward<RuleT>(rule) % std::move(fn);
}

template<ParserRuleLike RuleT, typename ExtraRequiredQuerySigT, typename FnT>
[[nodiscard]] inline constexpr auto operator%(
	RuleT&& rule, StateEffectFunctor<ExtraRequiredQuerySigT, FnT>&& fn
) {
	auto normalized = normalize_rule(std::forward<RuleT>(rule));
	return StateEffectRule<
		std::remove_cvref_t<ExtraRequiredQuerySigT>,
		stored_rule_t<decltype(normalized)>,
		FnT> {
		store_rule(std::move(normalized)),
		std::move(fn.fn),
	};
}

template<ParserRuleLike RuleT, typename ExtraRequiredQuerySigT, typename FnT>
[[nodiscard]] inline constexpr auto operator^(
	RuleT&& rule, StateEffectFunctor<ExtraRequiredQuerySigT, FnT>&& fn
) {
	return std::forward<RuleT>(rule) % std::move(fn);
}

template<size_t N>
inline constexpr auto repeat(ParserRuleLike auto&& rule) {
	static_assert(N > 0, "N must be greater than 0");
	if constexpr (N == 1)
		return std::forward<decltype(rule)>(rule);
	else
		return std::forward<decltype(rule)>(rule)
			>> repeat<N - 1>(std::forward<decltype(rule)>(rule));
}

template<ParserRuleLike RuleT>
inline constexpr auto repeat(size_t n, RuleT&& rule) {
	if (n == 1)
		return std::forward<RuleT>(rule);
	else
		return rule >> repeat(n - 1, std::forward<RuleT>(rule));
}

template<typename... Rules>
inline constexpr auto wrap(Rules&&... rules) -> SequentialRule<std::remove_cvref_t<Rules>...> {
	return {std::forward<Rules>(rules)...};
}

struct skip {
	constexpr skip() noexcept = default;

	template<typename T>
	constexpr skip(T&&) noexcept {}
};

inline constexpr auto cnot = []<std::convertible_to<char32_t>... Cs>(Cs&&... cs) {
	return (!(c(cs) | ...) >> cany) % value_to([](auto, auto ch) -> char32_t { return ch; });
};

inline constexpr auto ceof = c(0);

inline auto			  u32chars_to_u8(const std::vector<char32_t>& s) -> std::string {
	return utf8::utf32to8(std::u32string_view {(const char32_t* const)s.data(), s.size()});
}

inline constexpr auto boxed =
	value_to([]<typename V>(V&& v) { return std::make_unique<std::remove_cvref_t<V>>(v); });

}  // namespace pars::Legacy