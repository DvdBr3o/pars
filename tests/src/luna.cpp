#include <pars.hpp>

#include <catch2/catch_test_macros.hpp>
#include <tl/expected.hpp>
#include <utf8.h>

#include <iterator>
#include <stack>
#include <string_view>
#include <variant>
#include <iostream>

namespace pars::test::luna {
struct Ident {
	std::string ident;
};

struct OpIdent {
	std::string op;
};

struct NumLit {
	std::uint32_t num;
};

struct StrLit {
	std::string str;
};

struct FnCallList {
	//
};

struct FnCall {
	//
};

struct Table {
	// TODO:
};

struct Indent {
	uint32_t depth = 0;
};

struct IndentState : public std::stack<Indent> {
	IndentState() : std::stack<Indent> {{Indent {0}}} {}
};

struct QueryIndentState :
	QueryTagBase<QueryIndentState>,
	CopyPasteSnapshot<QueryIndentState, IndentState> {
	using QueryableType = IndentState;
};

constexpr QueryIndentState query_indent_state;

struct LunaParserState : AggregateParserState<QueryParserCursor, QueryIndentState> {
	LunaParserState(std::u8string_view s) :
		AggregateParserState<QueryParserCursor, QueryIndentState> {{s}, {}} {}
};

constexpr auto line_start =	 //
	(c('\n') >> *(c(' ') | c('\t'))) % value_to([](auto&&, auto&& indents) {
		Indent new_indent;
		for (const auto indent : indents)
			if (indent == ' ')
				new_indent.depth += 1;
			else if (indent == '\t')
				new_indent.depth += 4;
		return new_indent;
	});

struct IndentFailureError {
	uint32_t	current_depth;
	uint32_t	actual_depth;

	friend auto tag_invoke(to_string_t, const IndentFailureError& error) {
		return std::format(
			"unexpected indent depth, current_indent = {} whereas actual_depth = {}!",
			error.current_depth,
			error.actual_depth
		);
	}
};

struct DedentFailureError {
	friend auto tag_invoke(to_string_t, const DedentFailureError&) {
		return std::format("dedent failed!");
	}
};

constexpr auto indent =
	line_start
	% with_effect<required_query_t(QueryIndentState)>(
		[](auto&&		 st,
		   const Indent& new_indent) -> tl::expected<std::monostate, IndentFailureError> {
			auto&	   indent_state	  = query_indent_state(st);
			const auto current_indent = indent_state.top();
			if (new_indent.depth > current_indent.depth) {
				indent_state.push(new_indent);
				return std::monostate {};
			}
			return tl::make_unexpected(
				IndentFailureError {
					.current_depth = current_indent.depth,
					.actual_depth  = new_indent.depth,
				}
			);
		}
	);

constexpr auto dedent =
	line_start
	% with_effect<required_query_t(QueryIndentState)>(
		[](auto&&		 st,
		   const Indent& new_indent) -> tl::expected<std::monostate, DedentFailureError> {
			auto& indent_state = query_indent_state(st);
			while (!indent_state.empty() && new_indent.depth <= indent_state.top().depth) {
				if (new_indent.depth == indent_state.top().depth)
					return std::monostate {};
				indent_state.pop();
			}
			return tl::make_unexpected(DedentFailureError {});
		}
	);

struct BlockLineStartRule {
	using RequiredQuery = required_query_t(QueryParserCursor, QueryIndentState);
	using ValueType		= std::monostate;

	using ErrorType		= struct BlockLineStartError {
		uint32_t	expected_depth;
		uint32_t	actual_depth;

		friend auto tag_invoke(to_string_t, const BlockLineStartError& error) {
			return std::format(
				"unexpected indent depth, expected_depth = {} whereas actual_depth = {}!",
				error.expected_depth,
				error.actual_depth
			);
		}
	};

	using ResultType = tl::expected<ValueType, ErrorType>;

	constexpr auto match(auto&& st) const -> ResultType {
		const auto current_indent = query_indent_state(st).top().depth;
		Indent	   new_indent;
		if (auto res = line_start.match(st)) {
			new_indent = res.value();
			if (new_indent.depth == current_indent)
				return std::monostate {};
		}
		return tl::make_unexpected(
			BlockLineStartError {
				.expected_depth = current_indent,
				.actual_depth	= new_indent.depth,
			}
		);
	}
};

constexpr BlockLineStartRule block_line_start;

constexpr auto				 block = [](auto&& rule) constexpr {
	return (indent >> std::forward<decltype(rule)>(rule) >> (dedent | ceof))
		 % value_to([](auto&&, auto&& res, auto&&) { return std::forward<decltype(res)>(res); });
};

constexpr auto block_of_lines = []<typename RuleT>(RuleT&& rule) constexpr {
	return block(+(block_line_start >> std::forward<RuleT>(rule)));
};

constexpr auto empty_line = c('\n') >> *cset(' ', '\t') >> ~c('\n');

template<size_t Nl, size_t Nr>
constexpr auto packed_of(const char32_t (&lpack)[Nl], const char32_t (&rpack)[Nr]) {
	return [lpack, rpack]<typename RuleT>(RuleT&& rule) constexpr {
		return cstr(lpack) >> std::forward<RuleT>(rule) >> cstr(rpack);
	};
}

constexpr auto packed_of(char32_t lpack, char32_t rpack) {
	return [lpack, rpack]<typename RuleT>(RuleT&& rule) constexpr {
		return c(lpack) >> std::forward<RuleT>(rule) >> c(rpack);
	};
}

constexpr auto parenthesised = packed_of('(', ')');
constexpr auto bracketed	 = packed_of('[', ']');
constexpr auto braced		 = packed_of('{', '}');

constexpr auto sp			 = c(' ') | c('\t');
constexpr auto space		 = *sp;

/* identifier */
constexpr auto ident_start	  = cran('a', 'z') | cran('A', 'Z') | c('_');
constexpr auto ident_continue = ident_start | cran('0', '9');
constexpr auto ident		  =	 //
	(ident_start >> *ident_continue) % value_to([](auto start, auto&& cont) {
		std::u8string ident;
		ident.reserve(cont.size() + 2);
		utf8::append(start, std::back_inserter(ident));
		utf8::utf32to8(cont.begin(), cont.end(), std::back_inserter(ident));
		return ident;
	});

/* number literal */
constexpr auto numlit =	 //
	+cran('0', '9') % value_to([](auto&& num) {
		std::uint32_t numlit = 0;
		for (const auto dig : num) numlit = (numlit * 10) + dig - '0';
		return numlit;
	});

/* string literal */
constexpr auto strlit_escape =	//
	(c('\\') >> (c('\'') | c('\"') | c('\\') | c('t') | c('n')))
	% value_to([](auto, auto esc) -> char32_t {
		  switch (esc) {
			  case '\'': return '\'';
			  case '\"': return '\"';
			  case '\\': return '\\';
			  case 't': return '\t';
			  case 'n': return '\n';
			  default: break;
		  }
		  return U'\0';
	  });

constexpr auto strlit_chars_to_u8(auto&& cs) {
	std::u8string strlit;
	strlit.reserve(cs.size());
	utf8::utf32to8(cs.begin(), cs.end(), std::back_inserter(strlit));
	return strlit;
};

constexpr auto single_quote_strlit =  //
	(c('\'') >> *(strlit_escape | cnot('\n', '\'')) >> c('\''))
	% value_to([](auto, auto&& cs, auto) { return strlit_chars_to_u8(cs); });

constexpr auto double_quote_strlit =  //
	(c('\"') >> *(strlit_escape | cnot('\n', '\"')) >> c('\"'))
	% value_to([](auto, auto&& cs, auto) { return strlit_chars_to_u8(cs); });

constexpr auto long_bracket_strlit_char =
	cnot(']') | ((c(']') >> !c(']')) % value_to([](auto ch, auto) -> char32_t { return ch; }));

constexpr auto long_bracket_strlit =  //
	(cstr(U"[[") >> *long_bracket_strlit_char >> cstr(U"]]"))
	% value_to([](auto&&, auto&& cs, auto&&) { return strlit_chars_to_u8(cs); });

constexpr auto strlit = single_quote_strlit | double_quote_strlit | long_bracket_strlit_char;

template<typename InfixT, typename OperandT>
constexpr auto infixed_expr(InfixT&& infix, OperandT&& operand) -> decltype(auto) {
	return std::forward<OperandT>(operand)
		>> *((space >> std::forward<InfixT>(infix) >> space >> std::forward<OperandT>(operand))
			 % value_to([](auto&&, auto&& infix, auto&&, auto&& operand) {
				   return std::tuple {
					   std::forward<decltype(infix)>(infix),
					   std::forward<decltype(operand)>(operand),
				   };
			   }));
}

/* arithmetic expression */
constexpr auto atom_expr		= ident | numlit | strlit;
constexpr auto arithemetic_expr = atom_expr;

/* functional expression / call syntax */
constexpr auto call_parameter_list	 = infixed_expr(atom_expr, c(','));
constexpr auto nested_call			 = ident >> c('.') >> +sp;			// foo. a,b,c
constexpr auto currying_call		 = ident >> +sp;					// foo a b c
constexpr auto infixed_nested_call	 = +sp >> c('.') >> nested_call;	// a .foo b
constexpr auto infixed_currying_call = +sp >> c('.') >> currying_call;	// a .foo. b
constexpr auto functional_expr		 = cstr(U"TODO");

constexpr auto expr					 = arithemetic_expr | functional_expr;

/* pair */
constexpr auto inline_pair_key = ident | bracketed(expr);
constexpr auto inline_pair	   = inline_pair_key >> space >> c(':') >> space >> expr;

/* table */
constexpr auto table = fix<Table>([](auto&& table) constexpr {
	auto table_pair	   = inline_pair_key >> space >> c(':') >> space >> (expr | table);
	auto inline_table  = table_pair >> *(space >> c(',') >> space >> table_pair);
	auto blocked_table = block_of_lines(inline_table >> -c(','));
	return table_pair % value_to([](auto&&...) { return Table {}; });
});

/* lambda */
constexpr auto tuple			  = infixed_expr(c(','), expr);
constexpr auto parameter_list	  = parenthesised(tuple);
constexpr auto inline_lambda_body = expr;
constexpr auto block_lambda_body  = block(+(block_line_start >> expr));
constexpr auto lambda_body		  = inline_lambda_body | block_lambda_body;
constexpr auto thin_lambda		  = parameter_list >> space >> cstr(U"->") >> space >> lambda_body;
constexpr auto fat_lambda		  = cstr(U"=>") >> space >> lambda_body;
constexpr auto lambda			  = thin_lambda | fat_lambda;

TEST_CASE("can parse luna identifier", "[luna.primitive.ident]") {
	using std::operator""sv;
	auto st = LunaParserState {u8R"(id123)"};
	REQUIRE(ident.match(st).value() == u8"id123");
}

TEST_CASE("can parse luna number literal", "[luna.primitive.numlit]") {
	auto st = LunaParserState {u8R"(1245)"};
	REQUIRE(numlit.match(st).value() == 1245);
}

TEST_CASE("can parse luna single quote strling literal", "[luna.primitive.strlit.single_quote]") {
	auto st	 = LunaParserState {u8R"('\"\'\thello!\n')"};
	auto res = single_quote_strlit.match(st);
	REQUIRE(res.value() == u8"\"\'\thello!\n");
}

TEST_CASE("can parse luna double quote string literal", "[luna.primitive.strlit.double_quote]") {
	auto st	 = LunaParserState {u8R"("\"'\thello!\n")"};
	auto res = double_quote_strlit.match(st);
	REQUIRE(res.value() == u8"\"'\thello!\n");
}

TEST_CASE("can parse luna long bracket string literal", "[luna.primitive.strlit.long_bracket]") {
	auto st	 = LunaParserState {u8"[[hello\nworld]\"]]"};
	auto res = long_bracket_strlit.match(st);
	REQUIRE(res.value() == u8"hello\nworld]\"");
}

TEST_CASE("can parse indented block", "[luna.indent]") {
	auto st = LunaParserState {
		u8R"(if
    a
else
    b)"
	};

	constexpr auto rule =  //
		cstr(U"if") >> block(c('a')) >> cstr(U"else") >> block(c('b'));

	auto res = rule.match(st);
	CHECK(res);
	if (!res)
		std::cout << to_string(res.error()) << '\n';
}

TEST_CASE("can parse table literal", "[luna.table]") {
	SECTION("recursive table") {
		auto st = LunaParserState {
			u8R"(foo:
    bar: hello: 1, world: 'what'
    recur:
        sive: 2)"
		};
		REQUIRE(table.match(st));
	}
}

TEST_CASE("can parse thin arrow lambda", "[luna.lambda.thin]") {
	SECTION("inline lambda body") {
		auto st	 = LunaParserState {u8R"((a, b) -> a)"};
		auto res = thin_lambda.match(st);
		REQUIRE(res);
	}

	SECTION("optimized inline lambda body") {
		auto st	 = LunaParserState {u8R"((a, b) -> a)"};
		auto res = normalize(thin_lambda).match(st);
		REQUIRE(res);
	}

	SECTION("block lambda body") {
		auto st = LunaParserState {
			u8R"((a, b) -> 
	a + b
	1 * 2
)"
		};
		auto res = thin_lambda.match(st);
		REQUIRE(res);
	}
}

TEST_CASE("can parse call syntax.", "[luna.call]") {
	SECTION("nesting call") {
		auto st = LunaParserState {{u8R"(a b c)"}};
	}

	SECTION("infixing/chaining call") {}

	SECTION("currying call") {}
}

}  // namespace pars::test::luna
