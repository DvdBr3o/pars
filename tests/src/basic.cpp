#include "pars.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace pars;

namespace pars::tests::basic {
namespace {
inline constexpr auto passthrough1 = [](auto ch) constexpr { return ch; };
inline constexpr auto passthrough2 = [](auto ch) constexpr { return ch; };

using fused_transform_t = decltype((c('+') % value_to(passthrough1)) % value_to(passthrough2));
static_assert(is_value_transform_rule_v<fused_transform_t>);
static_assert(
	!is_value_transform_rule_v<
		std::remove_cvref_t<decltype(std::declval<fused_transform_t>().rule)>>,
	"normalize_rule should fuse nested value transforms into a single layer"
);

enum class SuiteTok {
	Eof,
	If,
	Name,
	Colon,
	Newline,
	Indent,
	Dedent,
};

inline auto tag_invoke(to_string_t, SuiteTok tok) -> std::string {
	switch (tok) {
		case SuiteTok::Eof: return "eof";
		case SuiteTok::If: return "if";
		case SuiteTok::Name: return "name";
		case SuiteTok::Colon: return ":";
		case SuiteTok::Newline: return "newline";
		case SuiteTok::Indent: return "indent";
		case SuiteTok::Dedent: return "dedent";
	}
	return "unknown";
}

struct CustomError {
	int code;

	friend auto tag_invoke(to_string_t, const CustomError& error) -> std::string {
		return std::format("custom error {}", error.code);
	}
};

struct CustomResult {
	bool		ok;
	int			value;
	CustomError error;

	constexpr explicit operator bool() const { return ok; }

	friend constexpr auto tag_invoke(value_of_t, const CustomResult& result) -> int {
		return result.value;
	}

	friend constexpr auto tag_invoke(error_of_t, const CustomResult& result) -> CustomError {
		return result.error;
	}
};

struct CustomResultRule {
	using RequiredQuery = required_query_t(QueryParserCursor);
	using ResultType	= CustomResult;

	constexpr auto match(auto&& state) const -> ResultType {
		if (auto res = c('x').match(state))
			return {
				.ok		= true,
				.value	= static_cast<int>(pars::value_of(std::move(res))),
				.error	= {.code = 0},
			};
		return {
			.ok		= false,
			.value	= 0,
			.error	= {.code = 7},
		};
	}
};

inline constexpr CustomResultRule custom_result_rule {};
inline constexpr DynamicRule custom_dynamic_rule = custom_result_rule;

static_assert(std::same_as<value_type_of_t<CustomResult>, int>);
static_assert(std::same_as<error_type_of_t<CustomResult>, CustomError>);
static_assert(std::same_as<typename decltype(custom_dynamic_rule)::ErrorType, CustomError>);
}  // namespace

TEST_CASE("utf8 and character rules work properly", "[utf]") {
	auto				  plain = TextParserState {u8"hello"};
	auto				  mixed = aggregate(QueryableMixin<QueryParserCursor> {{u8"AABBCC"}});

	static constexpr auto h		= c('h');
	static constexpr auto set	= cset('A', 'B');
	static constexpr CharSetRule<2> arr {
		std::array<char32_t, 2> {U'A', U'B'}
	};
	static constexpr auto lit = U"ABC"_cset;
	static constexpr auto rng = cran {'A', 'Z'};

	REQUIRE(h.match(plain).value() == U'h');
	REQUIRE(set.match(mixed).value() == U'A');
	REQUIRE(arr.match(mixed).value() == U'A');
	REQUIRE(lit.match(mixed).value() == U'B');
	REQUIRE(rng.match(mixed).value() == U'B');
}

TEST_CASE("snapshot and rollback restore parser cursor", "[snapshot]") {
	auto	   state  = aggregate(QueryableMixin<QueryParserCursor> {{u8"ABCD"}});

	const auto cursor = state | snapshot(query_parser_cursor);
	REQUIRE(c('A').match(state));
	REQUIRE(!c('A').match(state));

	state | rollback(query_parser_cursor, cursor);
	REQUIRE(c('A').match(state));
}

struct PythonicIndent {
	int indent = 0;
};

struct QueryPythonicIndent : QueryTagBase<QueryPythonicIndent> {
	using QueryableType = PythonicIndent;

	inline friend constexpr auto tag_invoke(
		snapshot_t, const Queryable<QueryPythonicIndent> auto& queryable, QueryPythonicIndent
	) noexcept {
		return queryable.query(QueryPythonicIndent {});
	}

	inline friend constexpr auto tag_invoke(
		rollback_t, Queryable<QueryPythonicIndent> auto&& queryable, QueryPythonicIndent,
		const QueryableType& snapshot
	) noexcept {
		queryable.query(QueryPythonicIndent {}) = snapshot;
	}
};

struct ParenDepthError {
	[[nodiscard]] auto tag_invoke(to_string_t) const -> std::string {
		return "paren depth parse failed";
	}
};

struct ParenDepthPrototype {
	using RequiredQuery = required_query_t(QueryParserCursor);
	using ValueType		= int;
	using ErrorType		= ParenDepthError;
	using ResultType	= tl::expected<ValueType, ErrorType>;

	constexpr auto match(auto&&) const -> ResultType { return tl::make_unexpected<ErrorType>({}); }
};

template<typename SelfT>
struct ParenDepthRule {
	SelfT self;

	using RequiredQuery =
		signature_cup_t<required_query_t(QueryParserCursor), RequiredQueryOfRule<SelfT>>;
	using ValueType	 = int;
	using ErrorType	 = ParenDepthError;
	using ResultType = tl::expected<ValueType, ErrorType>;

	constexpr auto match(auto&& state) const -> ResultType {
		if (!c('(').match(state))
			return tl::make_unexpected<ErrorType>({});

		const auto snapshot = aggregate_snapshot_of_rules<SelfT>(state);
		int		   depth	= 1;

		if (auto inner = self.match(state))
			depth += inner.value();
		else
			snapshot.rollback(state);

		if (!c(')').match(state))
			return tl::make_unexpected<ErrorType>({});

		return depth;
	}
};

struct Paren {
	std::unique_ptr<Paren> inner;

	[[nodiscard]] auto	   depth() const -> int { return inner ? 1 + inner->depth() : 1; }
};

TEST_CASE("aggregate snapshot restores every queried state", "[aggregate_snapshot]") {
	auto state = aggregate(
		QueryableMixin<QueryParserCursor> {{u8"abc"}},
		QueryableMixin<QueryPythonicIndent> {{.indent = 2}}
	);

	const auto snap = AggregateSnapshot<QueryParserCursor, QueryPythonicIndent> {state};
	query_parser_cursor(state).bump();
	query<QueryPythonicIndent>(state).indent = 8;

	snap.rollback(state);

	REQUIRE(query_parser_cursor(state).peek() == U'a');
	REQUIRE(query<QueryPythonicIndent>(state).indent == 2);
}

TEST_CASE("sequential rule is sound on failure", "[sequential.soundness]") {
	auto	   state  = TextParserState {u8"ac"};
	const auto seq	  = c('a') >> c('b');

	const auto before = query_parser_cursor(state);
	const auto res	  = seq.match(state);

	REQUIRE(!res);
	REQUIRE(query_parser_cursor(state).peek() == before.peek());
	REQUIRE(query_parser_cursor(state).bump() == U'a');
}

TEST_CASE("choice and repetition keep input stable on mismatch", "[choice.soundness]") {
	SECTION("choice rewinds between alternatives") {
		auto state	 = TextParserState {u8"ac"};
		auto choice	 = (c('a') >> c('b')) | (c('a') >> c('c'));
		auto matched = choice.match(state);

		REQUIRE(matched);
		REQUIRE(query_parser_cursor(state).peek() == pars::u8::Cursor::eof);
	}

	SECTION("repeat stops before the first failing item") {
		auto state = TextParserState {u8"aaab"};
		auto many  = *c('a');
		auto res   = many.match(state);

		REQUIRE(res);
		REQUIRE(res->size() == 3);
		REQUIRE(query_parser_cursor(state).peek() == U'b');
	}
}

TEST_CASE("peek rules do not consume input", "[peek]") {
	SECTION("peek-is succeeds without consumption") {
		auto state = TextParserState {u8"ab"};
		auto res   = (~c('a')).match(state);

		REQUIRE(res);
		REQUIRE(query_parser_cursor(state).peek() == U'a');
	}

	SECTION("peek-not succeeds without consumption") {
		auto state = TextParserState {u8"ab"};
		auto res   = (!c('b')).match(state);

		REQUIRE(res);
		REQUIRE(query_parser_cursor(state).peek() == U'a');
	}
}

TEST_CASE("custom result types work with value_of, error_of, and DynamicRule", "[custom.result]") {
	SECTION("choice extracts custom values") {
		auto state = TextParserState {u8"x"};
		auto rule  = custom_dynamic_rule | c('y');
		auto res   = rule.match(state);

		REQUIRE(res);
		REQUIRE(std::get<0>(pars::value_of(std::move(res))) == static_cast<int>(U'x'));
	}

	SECTION("sequential error uses custom to_string") {
		auto state = TextParserState {u8"y"};
		auto rule  = custom_dynamic_rule >> c('z');
		auto res   = rule.match(state);

		REQUIRE(!res);
		const auto& custom_error = std::get<CustomError>(pars::error_of(res).error);
		REQUIRE(to_string(custom_error) == "custom error 7");
	}
}

TEST_CASE("token rules parse a python-like suite", "[token.rules]") {
	const std::vector<SuiteTok> tokens {
		SuiteTok::If,
		SuiteTok::Name,
		SuiteTok::Colon,
		SuiteTok::Newline,
		SuiteTok::Indent,
		SuiteTok::Name,
		SuiteTok::Newline,
		SuiteTok::Dedent,
	};
	auto	   state = TokenParserState<SuiteTok> {tokens};

	const auto suite = tok(SuiteTok::If) >> tokset(SuiteTok::If, SuiteTok::Name)
					>> tok(SuiteTok::Colon) >> tok(SuiteTok::Newline) >> tok(SuiteTok::Indent)
					>> +tok(SuiteTok::Name) >> tok(SuiteTok::Newline) >> tok(SuiteTok::Dedent);

	auto res = suite.match(state);

	REQUIRE(res);
	REQUIRE(query_token_cursor<SuiteTok>(state).peek() == SuiteTok::Eof);
}

TEST_CASE("token range and set rules are sound on mismatch", "[token.rules.soundness]") {
	const std::array<int, 3> tokens {2, 7, 9};
	auto					 state	= TokenParserState<int> {tokens};
	const auto				 before = query_token_cursor<int>(state);

	auto					 res	= (tokran(3, 5) | tokset(8, 10)).match(state);

	REQUIRE(!res);
	REQUIRE(query_token_cursor<int>(state).peek() == before.peek());
	REQUIRE(query_token_cursor<int>(state).bump() == 2);
}

TEST_CASE("fix rule can work", "[fix]") {
	SECTION("fix infers boxed recursive result type") {
		auto		   st	= TextParserState {u8R"(((())))"};
		constexpr auto rule = fix<Paren>([](auto&& self) constexpr {
			return	//
				(c('(') >> -self >> c(')')) % value_to([](auto&&, auto&& inner, auto&&) {
					return std::make_unique<Paren>(inner ? std::move(*inner) : nullptr);
				});
		});

		using rule_t		= std::remove_cvref_t<decltype(rule)>;
		static_assert(std::same_as<typename rule_t::ValueType, std::unique_ptr<Paren>>);

		auto res = rule.match(st);
		REQUIRE(res);
		REQUIRE((*res)->depth() == 3);
		REQUIRE(query_parser_cursor(st).peek() == ParserCursor::eof);
	}
}

// TEST_CASE("arena facilities can work.", "[basic.arena]") {
// 	SECTION("arena string") {
// 		Arena::Pool pool;
// 		auto		str = pool.string("hello world!");
// 		REQUIRE(str == "hello world!");
// 	}

// 	SECTION("arena vector") {
// 		Arena::Pool pool;
// 		auto		vec = pool.make_vector({1, 2, 3});
// 		REQUIRE(vec[0] == 1);
// 	}
// }
}  // namespace pars::tests::basic
