#include "fmt/base.h"
#include "pars.hpp"

#include <catch2/catch_test_macros.hpp>
#include <utf8cpp/utf8/cpp20.h>

#include <string_view>

namespace pars::test::markdown {
static constexpr auto space = cset(U' ', U'\t') % [](auto&&) {
	fmt::println("space!");
	return std::monostate();
};
static constexpr auto cnot = [](char32_t ch) {
	return (!c(ch) >> cany) % [](auto&&, auto c) -> char32_t { return c; };
};

template<size_t N>
static constexpr auto _pre_heading() {
	if constexpr (N == 0)
		return +space;
	else
		return c('#') >> _pre_heading<N - 1>();
};

template<size_t N>
static constexpr auto pre_heading() {
	return _pre_heading<N>() % value_to([](auto&&...) { return std::monostate(); });
}

template<size_t N>
static constexpr auto heading() {
	return (pre_heading<N>() >> +cnot('\n') >> -c('\n')) %
			   [](auto&&, auto&& heading, auto&&) -> std::string {
		return utf8::utf32to8(
			std::u32string_view { (const char32_t* const)heading.data(), heading.size() }
		);
	};
};

static constexpr auto h1 = heading<1>();
static constexpr auto h2 = heading<2>();
static constexpr auto h3 = heading<3>();
static constexpr auto h4 = heading<4>();
static constexpr auto h5 = heading<5>();

TEST_CASE("markdown rule can parse headings.", "[markdown.heading]") {
	auto ps1 = ParserState { {
		u8"# Im Heading1\n"	   //
		"## Im Heading2\n"	   //
		"### Im Heading3\n"	   //
		"#### Im Heading4\n"   //
		"##### Im Heading5\n"  //
	} };

	REQUIRE(h1.match(ps1).value() == "Im Heading1");
	REQUIRE(h2.match(ps1).value() == "Im Heading2");
	REQUIRE(h3.match(ps1).value() == "Im Heading3");
	REQUIRE(h4.match(ps1).value() == "Im Heading4");
	REQUIRE(h5.match(ps1).value() == "Im Heading5");
}

TEST_CASE("markdown rule can parse latex blocks.", "[markdown.latex.block]") {}

TEST_CASE("markdown rule can parse inline latex blocks.", "[markdown.latex.inline]") {}
}  // namespace pars::test::markdown