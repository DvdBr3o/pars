#include "pars.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>
#include <variant>

using namespace pars;

namespace pars::test::markdown {
struct Heading {
	size_t		level;
	std::string text;
};

struct Paragraph {
	std::string text;
};

struct BlockQuote {
	std::string text;
};

struct ListItem {
	std::string text;
};

struct UnorderedList {
	std::vector<ListItem> items;
};

struct OrderedList {
	std::vector<ListItem> items;
};

struct ThematicBreak {};

struct FencedCodeBlock {
	std::string info;
	std::string content;
};

using Block = std::variant<
	Heading, Paragraph, BlockQuote, UnorderedList, OrderedList, ThematicBreak, FencedCodeBlock>;

struct Text {
	std::string content;
};

struct Emphasis {
	std::string content;
};

struct StrongEmphasis {
	std::string content;
};

struct CodeSpan {
	std::string content;
};

struct Link {
	std::string label;
	std::string destination;
};

struct MarkdownBlockContext {
	size_t continuation_indent = 0;
};

struct QueryMarkdownBlockContext :
	SnapshotableQueryTagBase<QueryMarkdownBlockContext, MarkdownBlockContext> {
	using QueryableType = MarkdownBlockContext;
};

struct MarkdownRuleError {
	std::string_view			 message;

	[[nodiscard]] constexpr auto tag_invoke(to_string_t) const -> std::string {
		return std::string(message);
	}
};

inline constexpr auto ignored = []<typename... Ts>(Ts&&...) constexpr { return skip {0}; };
inline constexpr auto as_char = [](auto&& v) constexpr -> char32_t {
	using T = std::remove_cvref_t<decltype(v)>;
	if constexpr (std::same_as<T, char32_t>)
		return v;
	else
		return std::visit(overload {[](char32_t ch) constexpr { return ch; }}, v);
};
inline constexpr auto join_chars = [](const auto& chars) -> std::string {
	return u32chars_to_u8(chars);
};
inline constexpr auto join_lines = [](const auto& lines) -> std::string {
	std::string out;
	for (size_t i = 0; i < lines.size(); ++i) {
		if (i != 0)
			out += "\n";
		out += lines[i];
	}
	return out;
};

inline constexpr auto space = cset(U' ', U'\t') ^ value_to([](auto) constexpr { return skip {0}; });
inline constexpr auto spaces1	  = (+space) ^ value_to(ignored);
inline constexpr auto newline	  = c('\n') ^ value_to([](auto) constexpr { return skip {0}; });
inline constexpr auto opt_newline = -c('\n') ^ value_to(ignored);
inline constexpr auto digit		  = cran(U'0', U'9');

inline constexpr auto cnot		  = []<typename... Chs>(Chs... chs) constexpr {
	   return (!(c(chs) | ...) >> cany)
			^ value_to([](auto, auto ch) constexpr -> char32_t { return ch; });
};

inline constexpr auto line_chars  = +cnot('\n');
inline constexpr auto line_chars0 = *cnot('\n');
inline constexpr auto line_text	  = line_chars ^ value_to(join_chars);
inline constexpr auto line_text0  = line_chars0 ^ value_to(join_chars);
inline constexpr auto quote_text =
	line_text ^ value_to([](const std::string& s) -> BlockQuote { return {.text = s}; });

template<size_t N>
inline constexpr auto heading = []() constexpr {
	return (repeat<N>(c('#')) >> spaces1 >> line_text >> opt_newline)
		 ^ value_to([]<typename... Args>(Args&&... args) -> Heading {
			   auto tup = std::forward_as_tuple(args...);
			   return Heading {
				   .level = N,
				   .text  = std::string(std::get<sizeof...(Args) - 2>(tup)),
			   };
		   });
};

inline constexpr auto h1 = heading<1>();
inline constexpr auto h2 = heading<2>();
inline constexpr auto h3 = heading<3>();
inline constexpr auto h4 = heading<4>();
inline constexpr auto h5 = heading<5>();
inline constexpr auto h6 = heading<6>();

inline constexpr auto paragraph =
	(line_text >> opt_newline) ^ value_to([](const std::string& text, auto&&) -> Paragraph {
		return Paragraph {.text = text};
	});

inline constexpr auto unordered_list_marker =
	(cset(U'-', U'+', U'*') >> +space)
	^ with_effect<required_query_t(QueryMarkdownBlockContext)>([](auto&&	  state,
																  const auto& spaces) constexpr {
		  query<QueryMarkdownBlockContext>(state).continuation_indent =
			  1 + std::get<1>(spaces).size();
	  })
	^ value_to(ignored);

inline constexpr auto ordered_list_marker =
	(+digit >> cset(U'.', U')') >> +space)
	^ with_effect<required_query_t(QueryMarkdownBlockContext)>([](auto&&	  state,
																  const auto& marker) constexpr {
		  query<QueryMarkdownBlockContext>(state).continuation_indent =
			  std::get<0>(marker).size() + 1 + std::get<2>(marker).size();
	  })
	^ value_to(ignored);

struct ContinuationIndentRule {
	using RequiredQuery = required_query_t(QueryParserCursor, QueryMarkdownBlockContext);
	using ValueType		= skip;
	using ErrorType		= MarkdownRuleError;
	using ResultType	= tl::expected<ValueType, ErrorType>;

	constexpr auto match(Queryable<QueryParserCursor> auto&& state) const -> ResultType
		requires Queryable<std::remove_cvref_t<decltype(state)>, QueryMarkdownBlockContext>
	{
		const auto need = query<QueryMarkdownBlockContext>(state).continuation_indent;
		const auto snapshot =
			AggregateSnapshot<QueryParserCursor, QueryMarkdownBlockContext> {state};
		for (size_t i = 0; i < need; ++i) {
			if (query_parser_cursor(state).peek() != U' ') {
				snapshot.rollback(state);
				return tl::make_unexpected<ErrorType>(
					MarkdownRuleError {"insufficient continuation indent"}
				);
			}
			query_parser_cursor(state).bump();
		}
		while (query_parser_cursor(state).peek() == U' ') query_parser_cursor(state).bump();
		return skip {};
	}
};

inline constexpr auto block_quote =
	(c('>') >> -c(' ') >> line_text >> opt_newline)
	^ value_to([](auto, auto&&, const std::string& text, auto&&) -> BlockQuote {
		  return {.text = text};
	  });

inline constexpr auto list_item_body =
	(line_text >> *((newline >> ContinuationIndentRule {} >> line_text)) >> opt_newline)
	^ value_to([](const std::string& first, const auto& tails, auto) -> ListItem {
		  std::string text = first;
		  for (const auto& tail : tails) {
			  text += "\n";
			  text += std::get<2>(tail);
		  }
		  return {.text = std::move(text)};
	  });

inline constexpr auto unordered_list_item =
	(unordered_list_marker >> list_item_body)
	^ value_to([](auto, const ListItem& item) -> ListItem { return item; });

inline constexpr auto ordered_list_item =
	(ordered_list_marker >> list_item_body)
	^ value_to([](auto, const ListItem& item) -> ListItem { return item; });

inline constexpr auto unordered_list =
	(+unordered_list_item) ^ value_to([](const auto& items) -> UnorderedList {
		return {
			.items = {items.begin(), items.end()}
		};
	});

inline constexpr auto ordered_list =
	(+ordered_list_item) ^ value_to([](const auto& items) -> OrderedList {
		return {
			.items = {items.begin(), items.end()}
		};
	});

template<char32_t Marker>
inline constexpr auto thematic_break_of = []() constexpr {
	constexpr auto unit = c(Marker) >> *space;
	return (repeat<3>(unit) >> *unit >> opt_newline)
		 ^ value_to([](auto&&...) -> ThematicBreak { return {}; });
};

inline constexpr auto thematic_break =
	(thematic_break_of<U'*'>() | thematic_break_of<U'-'>() | thematic_break_of<U'_'>())
	^ value_to([](auto&&) -> ThematicBreak { return {}; });

inline constexpr auto fence_info = (*cnot('\n')) ^ value_to([](const auto& chars) -> std::string {
									   return u32chars_to_u8(chars);
								   });
inline constexpr auto fenced_code_open =
	(repeat<3>(c('`')) >> -fence_info >> newline)
	^ value_to([](auto, auto, auto, const auto& info, auto) -> std::string {
		  return info.value_or(std::string {});
	  });
inline constexpr auto fenced_code_close =
	(repeat<3>(c('`')) >> *space >> opt_newline) ^ value_to(ignored);
inline constexpr auto fenced_code_line =
	(line_text0 >> opt_newline)
	^ value_to([](const std::string& line, auto) -> std::string { return line; });
inline constexpr auto fenced_code_block =
	(fenced_code_open >> *((!fenced_code_close >> fenced_code_line)) >> fenced_code_close)
	^ value_to([](const std::string& info, const auto& lines, auto) -> FencedCodeBlock {
		  std::vector<std::string> content_lines;
		  content_lines.reserve(lines.size());
		  for (const auto& line : lines) content_lines.emplace_back(std::get<1>(line));
		  return {
			  .info	   = info,
			  .content = join_lines(content_lines),
		  };
	  });

inline constexpr auto block = h6 | h5 | h4 | h3 | h2 | h1 | thematic_break | fenced_code_block
							| block_quote | unordered_list | ordered_list | paragraph;
inline constexpr auto document = *block;

struct LatexBlock {
	enum class Kind {
		Inline,
	};

	Kind		kind;
	std::string content;
};

using Inline = std::variant<Text, Emphasis, StrongEmphasis, CodeSpan, Link, LatexBlock>;

inline constexpr auto dollar_escape =
	(c('\\') >> c('$')) ^ value_to([](auto, auto) constexpr { return U'$'; });
inline constexpr auto inline_latex_char =
	(dollar_escape | cnot('$', '\n'))
	^ value_to([](auto&& v) constexpr -> char32_t { return as_char(v); });
inline constexpr auto inline_latex_block =
	(c('$') >> +inline_latex_char >> c('$'))
	^ value_to([](auto, const auto& chars, auto) -> LatexBlock {
		  return LatexBlock {
			  .kind	   = LatexBlock::Kind::Inline,
			  .content = u32chars_to_u8(chars),
		  };
	  });

inline constexpr auto inline_text_chars = +cnot('*', '`', '[', '$', '\n');
inline constexpr auto inline_text = inline_text_chars ^ value_to([](const auto& chars) -> Text {
										return {.content = u32chars_to_u8(chars)};
									});

inline constexpr auto emphasis	  = (c('*') >> +cnot('*', '\n') >> c('*'))
							   ^ value_to([](auto, const auto& chars, auto) -> Emphasis {
									 return {.content = u32chars_to_u8(chars)};
								 });

inline constexpr auto strong_emphasis =
	(repeat<2>(c('*')) >> +cnot('*', '\n') >> repeat<2>(c('*')))
	^ value_to([](auto, auto, const auto& chars, auto, auto) -> StrongEmphasis {
		  return {.content = u32chars_to_u8(chars)};
	  });

inline constexpr auto code_span = (c('`') >> *cnot('`', '\n') >> c('`'))
								^ value_to([](auto, const auto& chars, auto) -> CodeSpan {
									  return {.content = u32chars_to_u8(chars)};
								  });

inline constexpr auto link_label	   = (+cnot(']', '\n')) ^ value_to(join_chars);
inline constexpr auto link_destination = (+cnot(')', '\n')) ^ value_to(join_chars);
inline constexpr auto link =
	(c('[') >> link_label >> c(']') >> c('(') >> link_destination >> c(')'))
	^ value_to(
		[](auto, const std::string& label, auto, auto, const std::string& destination,
		   auto) -> Link {
			return {
				.label		 = label,
				.destination = destination,
			};
		}
	);

inline constexpr auto inline_atom =
	strong_emphasis | emphasis | code_span | link | inline_latex_block | inline_text;
inline constexpr auto inline_document = +inline_atom;

TEST_CASE("markdown rules are defined with pure combinators", "[markdown.heading]") {
	auto state = aggregate(
		QueryableMixin<QueryParserCursor> {{u8"# Heading 1\n"
											"### Heading 3\n"
											"plain paragraph\n"}},
		QueryableMixin<QueryMarkdownBlockContext> {{}}
	);

	auto doc = document.match(state);

	REQUIRE(doc);
	REQUIRE(doc->size() == 3);
	REQUIRE(std::get<Heading>((*doc)[0]).text == "Heading 1");
	REQUIRE(std::get<Heading>((*doc)[0]).level == 1);
	REQUIRE(std::get<Heading>((*doc)[1]).text == "Heading 3");
	REQUIRE(std::get<Paragraph>((*doc)[2]).text == "plain paragraph");
}

TEST_CASE("heading rule keeps soundness on malformed heading", "[markdown.heading.soundness]") {
	auto	   state  = TextParserState {u8"##NoSpace"};
	const auto before = query_parser_cursor(state);

	auto	   res	  = h2.match(state);

	REQUIRE(!res);
	REQUIRE(query_parser_cursor(state).peek() == before.peek());
	REQUIRE(query_parser_cursor(state).bump() == U'#');
}

TEST_CASE("inline latex rule parses escaped dollars", "[markdown.latex.inline]") {
	auto state = TextParserState {u8"$a^2 + b^2 \\$ = c^2$"};
	auto res   = inline_latex_block.match(state);

	REQUIRE(res);
	REQUIRE(res->kind == LatexBlock::Kind::Inline);
	REQUIRE(res->content == "a^2 + b^2 $ = c^2");
}

TEST_CASE("inline latex rule is sound on unterminated block", "[markdown.latex.soundness]") {
	auto	   state  = TextParserState {u8"$x + y"};
	const auto before = query_parser_cursor(state);

	auto	   res	  = inline_latex_block.match(state);

	REQUIRE(!res);
	REQUIRE(query_parser_cursor(state).peek() == before.peek());
	REQUIRE(query_parser_cursor(state).bump() == U'$');
}

TEST_CASE(
	"blockquote and unordered list parse commonmark-like blocks", "[markdown.commonmark.blocks]"
) {
	auto state = aggregate(
		QueryableMixin<QueryParserCursor> {{u8"> quoted line\n"
											"- first item\n"
											"  continuation line\n"
											"- second item\n"}},
		QueryableMixin<QueryMarkdownBlockContext> {{}}
	);

	auto doc = document.match(state);

	REQUIRE(doc);
	REQUIRE(doc->size() == 2);
	REQUIRE(std::get<BlockQuote>((*doc)[0]).text == "quoted line");
	const auto& list = std::get<UnorderedList>((*doc)[1]);
	REQUIRE(list.items.size() == 2);
	REQUIRE(list.items[0].text == "first item\ncontinuation line");
	REQUIRE(list.items[1].text == "second item");
}

TEST_CASE(
	"list item rollback restores both cursor and markdown context", "[markdown.list.soundness]"
) {
	auto state = aggregate(
		QueryableMixin<QueryParserCursor> {{u8"-bad item"}},
		QueryableMixin<QueryMarkdownBlockContext> {{.continuation_indent = 7}}
	);
	const auto before_cursor = query_parser_cursor(state);
	const auto before_indent = query<QueryMarkdownBlockContext>(state).continuation_indent;

	auto	   res			 = unordered_list_item.match(state);

	REQUIRE(!res);
	REQUIRE(query_parser_cursor(state).peek() == before_cursor.peek());
	REQUIRE(query<QueryMarkdownBlockContext>(state).continuation_indent == before_indent);
}

TEST_CASE(
	"ordered list, thematic break and fenced code block parse commonmark blocks",
	"[markdown.commonmark.more_blocks]"
) {
	auto state = aggregate(
		QueryableMixin<QueryParserCursor> {{u8"1. first\n"
											"2. second\n"
											"***\n"
											"```cpp\n"
											"int x = 1;\n"
											"return x;\n"
											"```\n"}},
		QueryableMixin<QueryMarkdownBlockContext> {{}}
	);

	auto doc = document.match(state);

	REQUIRE(doc);
	REQUIRE(doc->size() == 3);
	const auto& list = std::get<OrderedList>((*doc)[0]);
	REQUIRE(list.items.size() == 2);
	REQUIRE(list.items[0].text == "first");
	REQUIRE(list.items[1].text == "second");
	REQUIRE(std::holds_alternative<ThematicBreak>((*doc)[1]));
	const auto& code = std::get<FencedCodeBlock>((*doc)[2]);
	REQUIRE(code.info == "cpp");
	REQUIRE(code.content == "int x = 1;\nreturn x;");
}

TEST_CASE(
	"fenced code block is sound on missing closing fence", "[markdown.code_fence.soundness]"
) {
	auto	   state  = TextParserState {u8"```txt\nunterminated\n"};
	const auto before = query_parser_cursor(state);

	auto	   res	  = fenced_code_block.match(state);

	REQUIRE(!res);
	REQUIRE(query_parser_cursor(state).peek() == before.peek());
	REQUIRE(query_parser_cursor(state).bump() == U'`');
}

TEST_CASE("inline parser handles emphasis code links and latex", "[markdown.inline.commonmark]") {
	auto state =
		TextParserState {u8"plain *em* **strong** `code` [site](https://example.com) $x+y$"};

	auto res = inline_document.match(state);

	REQUIRE(res);
	REQUIRE(res->size() == 10);
	REQUIRE(std::get<Text>((*res)[0]).content == "plain ");
	REQUIRE(std::get<Emphasis>((*res)[1]).content == "em");
	REQUIRE(std::get<Text>((*res)[2]).content == " ");
	REQUIRE(std::get<StrongEmphasis>((*res)[3]).content == "strong");
	REQUIRE(std::get<Text>((*res)[4]).content == " ");
	REQUIRE(std::get<CodeSpan>((*res)[5]).content == "code");
	REQUIRE(std::get<Text>((*res)[6]).content == " ");
	REQUIRE(std::get<Link>((*res)[7]).label == "site");
	REQUIRE(std::get<Link>((*res)[7]).destination == "https://example.com");
	REQUIRE(std::get<Text>((*res)[8]).content == " ");
	REQUIRE(std::get<LatexBlock>((*res)[9]).content == "x+y");
}

TEST_CASE(
	"inline link parser is sound on missing destination close", "[markdown.inline.soundness]"
) {
	auto	   state  = TextParserState {u8"[site](https://example.com"};
	const auto before = query_parser_cursor(state);

	auto	   res	  = link.match(state);

	REQUIRE(!res);
	REQUIRE(query_parser_cursor(state).peek() == before.peek());
	REQUIRE(query_parser_cursor(state).bump() == U'[');
}
}  // namespace pars::test::markdown
