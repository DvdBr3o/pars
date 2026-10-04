// tests/test_model.cpp — the scan algebra end-to-end: grammar -> plan -> scan -> pair.
#include "pars/pars.hpp"

#include "../bench/grammars/json.hpp"
#include "../bench/grammars/csv.hpp"
#include "../bench/grammars/toml.hpp"
#include "../bench/grammars/clike.hpp"

#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

using namespace pars;

static int g_failures = 0;
#define CHECK(cond)                                                                                \
	do {                                                                                           \
		if (!(cond)) {                                                                             \
			std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                            \
			++g_failures;                                                                          \
		}                                                                                          \
	} while (0)

// ---------------------------------------------------------------------------
// Derived plans (compile-time)
// ---------------------------------------------------------------------------
constexpr auto kJsonPlan = compile::make_scan_plan<bench::json_grammar>();
constexpr auto kCsvPlan	 = compile::make_scan_plan<bench::csv_grammar>();

static_assert(kJsonPlan.n_brackets == 2);
static_assert(kJsonPlan.n_modes == 1);
static_assert(kJsonPlan.modes[0].delim == '"' && kJsonPlan.modes[0].escape == '\\');
static_assert(kJsonPlan.n_punctuation == 2);

static_assert(kCsvPlan.n_brackets == 0);
static_assert(kCsvPlan.n_modes == 1);
static_assert(kCsvPlan.modes[0].delim == '"' && kCsvPlan.modes[0].escape == '"');
static_assert(kCsvPlan.n_punctuation == 2);

// TOML: derived purely from automata + FIRST/LAST (multi-mode + comment).
constexpr auto kTomlPlan = compile::make_scan_plan<bench::toml_grammar>();
static_assert(kTomlPlan.n_brackets == 2);
static_assert(kTomlPlan.n_modes == 2);
static_assert(kTomlPlan.n_comments == 1);
static_assert(kTomlPlan.n_punctuation == 3);

// Decisive coverage evidence: a C-like grammar whose block comment is NOT one of
// the legacy templates must land on the general Table path.
constexpr auto kClikePlan = compile::make_scan_plan<bench::clike_grammar>();
static_assert(kClikePlan.n_brackets == 1);
static_assert(kClikePlan.n_punctuation == 1);

constexpr bool clike_has_table() {
	for (std::uint8_t d = 0; d < kClikePlan.n_descs; ++d)
		if (kClikePlan.descs[d].kind == scan_plan::lex_kind::table)
			return true;
	return false;
}

static_assert(clike_has_table());

// report every lexical descriptor kind (compile-time)
template<class Plan>
constexpr int count_kind(Plan P, scan_plan::lex_kind k) {
	int c = 0;
	for (std::uint8_t d = 0; d < P.n_descs; ++d)
		if (P.descs[d].kind == k)
			++c;
	return c;
}

static_assert(count_kind(kJsonPlan, scan_plan::lex_kind::affine) == 1);
static_assert(count_kind(kCsvPlan, scan_plan::lex_kind::doubled) == 1);
static_assert(count_kind(kTomlPlan, scan_plan::lex_kind::comment) == 1);

// every recursive rule must have a primitive
static_assert(compile::is_supported_rule<bench::json_grammar, bench::json_grammar::object>());
static_assert(compile::is_supported_rule<bench::json_grammar, bench::json_grammar::array>());

// ---------------------------------------------------------------------------
// P0 soundness gates
// ---------------------------------------------------------------------------
static_assert(compile::is_supported<bench::json_grammar>);
static_assert(compile::is_supported<bench::csv_grammar>);
static_assert(compile::is_supported<bench::toml_grammar>);

// A recursive rule whose "closing" literal is not the matching bracket must be
// rejected (previously FIRST/LAST mis-derived a bracket pair).
struct g_bad_bracket {
	struct a {};

	using rules = type_list<a>;
	using start = a;
	template<class>
	struct def;
};

template<>
struct g_bad_bracket::def<g_bad_bracket::a> {
	using type = seq<lit<'('>, rule<g_bad_bracket::a>, lit<')'>, lit<';'>>;
};

static_assert(!compile::is_supported<g_bad_bracket>);

// A general (non one-byte) notp is not regular and must be rejected.
struct g_bad_notp {
	struct a {};

	struct b {};

	using rules = type_list<a, b>;
	using start = a;
	template<class>
	struct def;
};

template<>
struct g_bad_notp::def<g_bad_notp::b> {
	using type = lit<'x'>;
};

template<>
struct g_bad_notp::def<g_bad_notp::a> {
	using type = notp<rule<g_bad_notp::b>>;
};

static_assert(!compile::is_supported<g_bad_notp>);

// An explicit fallback whitelist makes an otherwise unsupported rule supported.
struct g_fallback {
	struct a {};

	using rules			 = type_list<a>;
	using start			 = a;
	using fallback_rules = type_list<a>;
	template<class>
	struct def;
};

template<>
struct g_fallback::def<g_fallback::a> {
	using type = seq<rule<g_fallback::a>, lit<'x'>, rule<g_fallback::a>>;
};

static_assert(compile::is_supported<g_fallback>);

// ---------------------------------------------------------------------------
// Scan correctness vs naive reference
// ---------------------------------------------------------------------------
static void test_json_scan() {
	model::scanner<kJsonPlan>  sc;
	const std::string_view	   in = R"({"name":"a\"b","n":[1,2.5e-3,true,null],"x":{}})";
	auto					   r  = sc.scan(in);

	std::vector<std::uint32_t> pos;
	std::vector<std::uint8_t>  ch;
	bool					   in_str = false;
	for (std::size_t i = 0; i < in.size(); ++i) {
		const char c = in[i];
		if (c == '"') {
			bool esc = false;
			for (std::size_t j = i; j > 0 && in[j - 1] == '\\'; --j) esc = !esc;
			if (!esc)
				in_str = !in_str;
		}
		if (!in_str && (c == '{' || c == '}' || c == '[' || c == ']' || c == ':' || c == ',')) {
			pos.push_back((std::uint32_t)i);
			ch.push_back((std::uint8_t)c);
		}
	}
	CHECK(r.positions == pos);
	CHECK(r.chars == ch);
}

static void test_csv_scan() {
	model::scanner<kCsvPlan>   sc;
	const std::string_view	   in = "a,\"b, c\",\"d\"\"e\",f\n1,2,3,4\n";
	auto					   r  = sc.scan(in);
	std::vector<std::uint32_t> pos;
	std::vector<std::uint8_t>  ch;
	bool					   in_q = false;
	for (std::size_t i = 0; i < in.size(); ++i) {
		const char c = in[i];
		if (c == '"') {
			if (in_q && i + 1 < in.size() && in[i + 1] == '"') {
				++i;
				continue;
			}
			in_q = !in_q;
			continue;
		}
		if (!in_q && (c == ',' || c == '\n')) {
			pos.push_back((std::uint32_t)i);
			ch.push_back((std::uint8_t)c);
		}
	}
	CHECK(r.positions == pos);
	CHECK(r.chars == ch);
}

static void test_pair() {
	model::scanner<kJsonPlan> sc;
	const std::string_view	  in = R"({"a":[1,{"b":2}],"c":[]})";
	auto					  r	 = sc.scan(in);
	auto					  t	 = model::pair_brackets<kJsonPlan>(r.bracket_char, r.bracket_pos);
	CHECK(t.balanced);
	CHECK(t.well_formed);
	CHECK(t.max_depth == 3);
	CHECK(t.pair.size() == r.bracket_char.size());
	for (std::size_t i = 0; i < t.pair.size(); ++i) {
		const auto j = t.pair[i];
		CHECK(j >= 0);
		CHECK(t.pair[(std::size_t)j] == (std::int32_t)i);
	}
}

static void test_clike_scan() {
	const std::string inputs[] = {
		"a;b/* { ; } */c;// x { } ;\nd;{e;f}",
		"/**/;",
		"/***/;",
		"/* * */x;",
		"/*/**/;",			// nested-looking star run
		"a/*a**b*/;c",		// star run in the middle
		"// /* ; { */\n;",	// line comment containing a block opener
	};
	for (const std::string& in : inputs) {
		model::scanner<kClikePlan> sc;
		auto					   r = sc.scan(in);
		std::vector<std::uint32_t> pos;
		std::vector<std::uint8_t>  ch;
		std::size_t				   i = 0;
		while (i < in.size()) {
			if (in[i] == '/' && i + 1 < in.size() && in[i + 1] == '*') {
				i += 2;
				while (i + 1 < in.size() && !(in[i] == '*' && in[i + 1] == '/')) ++i;
				i = (i + 2 <= in.size()) ? i + 2 : in.size();
				continue;
			}
			if (in[i] == '/' && i + 1 < in.size() && in[i + 1] == '/') {
				i += 2;
				while (i < in.size() && in[i] != '\n') ++i;
				continue;
			}
			const char c = in[i];
			if (c == ';' || c == '{' || c == '}') {
				pos.push_back((std::uint32_t)i);
				ch.push_back((std::uint8_t)c);
			}
			++i;
		}
		CHECK(r.positions == pos);
		CHECK(r.chars == ch);
	}
}

int main() {
	// hard support gate must hold for the benchmark grammars
	compile::require_supported<bench::json_grammar>();
	compile::require_supported<bench::csv_grammar>();
	compile::require_supported<bench::toml_grammar>();

	test_json_scan();
	test_csv_scan();
	test_clike_scan();
	test_pair();

	std::printf(
		"json plan: brackets=%u modes=%u punct=%u\n",
		kJsonPlan.n_brackets,
		kJsonPlan.n_modes,
		kJsonPlan.n_punctuation
	);
	std::printf(
		"csv  plan: brackets=%u modes=%u punct=%u\n",
		kCsvPlan.n_brackets,
		kCsvPlan.n_modes,
		kCsvPlan.n_punctuation
	);
	if (g_failures == 0)
		std::printf("all model-M tests passed\n");
	return g_failures == 0 ? 0 : 1;
}
