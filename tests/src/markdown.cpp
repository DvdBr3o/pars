#include "fmt/base.h"
#include "pars.hpp"

#include <catch2/catch_test_macros.hpp>
#include <utf8cpp/utf8/cpp20.h>

#include <string_view>

namespace pars::test::markdown {
inline constexpr auto space = cset(U' ', U'\t') % [](skip) { return std::monostate(); };
inline constexpr auto cnot	= [](auto... cs) constexpr {
	 return (!(c(cs) | ...) >> cany) % [](skip, auto c) -> char32_t { return c; };
};

template<size_t N>
inline constexpr auto heading = []() constexpr {
	return	// clang-format off
		(wrap(repeat<N>(c('#')))     >> +space       >> +cnot('\n')       >> -c('\n'))	
	% [](skip, 					    	 skip,       	 auto&& heading,      skip
		) -> std::string {
			// clang-format on
			return utf8::utf32to8(
				std::u32string_view { (const char32_t* const)heading.data(), heading.size() }
			);
		};
};

inline constexpr auto h1 = heading<1>();
inline constexpr auto h2 = heading<2>();
inline constexpr auto h3 = heading<3>();
inline constexpr auto h4 = heading<4>();
inline constexpr auto h5 = heading<5>();

struct LatexBlock {
	enum class Kind {
		Inline,
		Display,
	};

	Kind		kind;
	std::string content;
};

inline constexpr auto dollar_escape = (c('\\') >> c('$')) % [](skip, skip) { return '$'; };	 // \$
inline constexpr auto inline_latex_block_char =												 //
	(dollar_escape | cnot('$', '\n'))														 //
	% [](auto c) -> char32_t {
	return std::visit(overload { [](auto&& s) -> char32_t { return s; } }, c);
};
inline constexpr auto inline_latex_block =	//
	(c('$') >> +inline_latex_block_char >> c('$')) % [](skip, auto&& s, skip) -> LatexBlock {
	return {
		.kind	 = LatexBlock::Kind::Inline,
		.content = u32chars_to_u8(s),
	};
};

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

TEST_CASE("markdown rule can parse latex blocks.", "[markdown.latex.block]") {
	{
		constexpr auto script = u8"$a^2 + b^2 \\$ = c^2$";
		auto		   ps1	  = ParserState { { script } };
		REQUIRE(inline_latex_block.match(ps1)->content == "a^2 + b^2 $ = c^2");
	}

	{}
}

TEST_CASE("markdown rule can parse inline latex blocks.", "[markdown.latex.inline]") {}
}  // namespace pars::test::markdown
