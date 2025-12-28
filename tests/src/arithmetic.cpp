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

	static constexpr auto foo		  = c('+') >> c('-') >> c('-') >> c('-');

	TEST_CASE("parser can handle precedence.", "[arithmetic.precedence]") {
		static constexpr auto script = u8R"(屮)";

		// +c('a');
		//
		ParserState st = { { script } };

		SetConsoleOutputCP(CP_UTF8);

		{
			const auto res = additive_op.match(st);
			if (res) {
				fmt::print(fmt::emphasis::bold | fg(fmt::color::green), "[Info] ");
				fmt::println("Received expected char: `{}`", to_utf8(*res));
			} else {
				const auto err = res.error();
				fmt::print(fmt::emphasis::bold | fg(fmt::color::red), "[Error] ");
				fmt::println("{}", err | overload { [](auto&& e) -> std::string {
									   return e.to_string();
								   } });
			}
		}
	}
}  // namespace pars::test::arithmetic