#include "pars.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace pars;

TEST_CASE("utf8 facilities works properly", "[utf]") {
	SECTION("bump") {
		auto cursor = u8::Cursor { u8"你好sのb" };
		REQUIRE(cursor.bump() == U'你');
		REQUIRE(cursor.bump() == U'好');
		REQUIRE(cursor.bump() == U's');
		REQUIRE(cursor.bump() == U'の');
		REQUIRE(cursor.bump() == U'b');
	}

	SECTION("peek") {
		auto cursor = u8::Cursor { u8"你好sのb" };
		REQUIRE(cursor.peek() == U'你');
		cursor.bump();
		REQUIRE(cursor.peek() == U'好');
		cursor.bump();
	}

	SECTION("piror") {
		auto cursor = u8::Cursor { u8"你好sのb" };
		cursor.bump();
		cursor.bump();
		REQUIRE(cursor.prior() == U'好');
		REQUIRE(cursor.prior() == U'你');
	}
}
