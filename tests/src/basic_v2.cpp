
#include "pars_v2.hpp"

#include <catch2/catch_test_macros.hpp>

#include <iostream>

using namespace pars::v2::pars;

TEST_CASE("utf8 facilities works properly", "[utf]") {
	static constexpr auto script = u8"AABBCC";

	//
	auto state	  = DefaultParserState {u8"hello"};
	auto composed = aggregate(	//
		QueryableMixin<QueryParserCursor> {{script}}
	);

	//
	static constexpr auto r1 = c('h');
	static constexpr auto r2 = cset('A', 'B');
	static constexpr auto r3 = cset({'A', 'B'});
	static constexpr auto r4 = U"ABC"_cset;
	REQUIRE(r1.match(state));
	REQUIRE(r2.match(composed));
	REQUIRE(r3.match(composed));
	REQUIRE(r4.match(composed));
}

TEST_CASE("snapshot & rollback works properly", "[basics.snapshot]") {
	static constexpr auto script = u8"ABCD";
	static constexpr auto a		 = c('A');

	//
	auto	   state  = aggregate(QueryableMixin<QueryParserCursor> {{script}});
	const auto cursor = state | snapshot(query_parser_cursor);
	REQUIRE(a.match(state));
	REQUIRE(!a.match(state));
	state | rollback(query_parser_cursor, cursor);
	REQUIRE(a.match(state));
}

struct PythonicIndent {
	int indent;
};

struct QueryPythonicIndent {
	using queryable_type = PythonicIndent;

	inline friend constexpr auto tag_invoke(
		snapshot_t, Queryable<QueryPythonicIndent> auto&& queryable, QueryPythonicIndent
	) noexcept {
		return queryable.query(QueryPythonicIndent {});
	}

	inline friend constexpr auto tag_invoke(
		rollback_t, Queryable<QueryPythonicIndent> auto&& queryable, const queryable_type& snapshot
	) noexcept {
		queryable.query(QueryPythonicIndent {}) = snapshot;
	}
};

TEST_CASE("aggregate snashot works properly", "[basics.aggregate_snapshot]") {
	static constexpr auto script = u8"aafoobar";
	static constexpr auto a		 = c('a');
	static constexpr auto seq	 = SequentialRule {a, a, a};

	//
	auto state = aggregate(
		QueryableMixin<QueryParserCursor> {{script}},  //
		QueryableMixin<QueryPythonicIndent> {}		   //
	);

	// REQUIRE(state | match(seq));
	static_assert(
		std::same_as<RequiredQueryOfRule<decltype(seq)>, required_query_t(QueryParserCursor)>
	);
	static_assert(QueryableAllSigC<decltype(state), RequiredQueryOfRule<decltype(seq)>>);
	// const auto res = seq.match(state);
	const auto res = seq.match(state);
	CHECK(res);
	if (!res) {
		INFO("error!");
		INFO(to_string(res.error()));
		std::cout << to_string(res.error()) << '\n';
	} else {
		INFO("yep!");
	}
}

TEST_CASE("sequetial rule matches properly", "[rules.sequential]") {
	static constexpr auto script = u8"hello";

	//
	auto state = DefaultParserState {script};
}
