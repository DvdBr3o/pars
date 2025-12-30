#include "pars.hpp"

#include <catch2/catch_test_macros.hpp>
#include <fmt/format.h>
#include <fmt/color.h>

#include <Windows.h>

#include <string>
#include <variant>
#include <iostream>

using namespace pars;

namespace pars::test::arithmetic {
	struct Number {
		std::u8string num;
	};

	struct AdditiveNumber {
		enum Op {
			Add,
			Minus,
		};

		Op													  op;
		std::variant<std::unique_ptr<AdditiveNumber>, Number> lhs;
		std::variant<std::unique_ptr<AdditiveNumber>, Number> rhs;
	};

	struct MultiveNumber {
		enum Op {
			Mult,
			Div,
		};

		Op															 op;
		std::variant<std::unique_ptr<MultiveNumber>, AdditiveNumber> lhs;
		std::variant<std::unique_ptr<MultiveNumber>, AdditiveNumber> rhs;
	};

	struct AdditiveExpr {};

	static constexpr auto number	  = cran('0', '9');
	static constexpr auto additive_op = cset(U'+', U'-', U'屮');

	static constexpr auto seq		  = c('+') >> c('-');
	static constexpr auto choice	  = c('-') | c('+');
	static constexpr auto opt		  = -c('+');
	static constexpr auto oom		  = +c('+');
	static constexpr auto rpt		  = *c('+');
	static constexpr auto pis		  = ~c('+');
	static constexpr auto vt		  = ~c('+') % value_to([](auto c) {});
	static constexpr auto vt2		  = ~c('+') % [](auto c) {};
	static constexpr auto seqrt		  = seq % [](auto c1, auto c2) {};

	TEST_CASE("parser can handle precedence.", "[arithmetic.precedence]") {
		static constexpr auto script = u8R"(+9)";

		//
		ParserState st1 = { { u8R"(++++)" } };
		ParserState st2 = { { u8R"(a-)" } };

		REQUIRE((~c('+')).match(st1));
		REQUIRE(!(~c('-')).match(st1));
		const auto r1 = rpt.match(st1);
		REQUIRE(r1);
		REQUIRE(r1->size() == 4);

		const auto r2 = rpt.match(st2);
		REQUIRE(r2);
	}
}  // namespace pars::test::arithmetic