// tests/test_core.cpp — the new unified pipeline: grammar -> core::plan ->
// host executor via prefix_reduce.  Cross-validated against the existing
// reference scanner on the JSON fast fragment.
#include "pars/pars.hpp"

#include "pars/core/host.hpp"
#include "pars/core/lower.hpp"
#include "pars/core/tree.hpp"

#include "../bench/grammars/json.hpp"

#include <cstdint>
#include <cstdio>
#include <string>

using namespace pars;

static int g_fail = 0;
#define CHECK(c)                                                                                   \
	do {                                                                                           \
		if (!(c)) {                                                                                \
			std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                               \
			++g_fail;                                                                              \
		}                                                                                          \
	} while (0)

constexpr auto kCore = core::lower<bench::json_grammar>();
constexpr auto kOld	 = compile::make_scan_plan<bench::json_grammar>();

// the new IR agrees with what the analysis says
static_assert(kCore.n_atoms == 2);		 // ':' ','
static_assert(kCore.n_brackets == 2);	 // {} []
static_assert(kCore.n_cells >= 1);		 // >= the string transducer

// --- tree schema (R7): associative reduction as a parallel op --------------
struct plus63 {
	std::int64_t operator()(std::int64_t a, std::int64_t b) const { return a + b; }
};

static void test_tree_schema() {
	// "1+2+3+4+5" -> 15, computed by prefix_reduce over the reducer monoid
	const char* s = "1+2+3+4+5";
	CHECK(core::reduce_ints<plus63>(reinterpret_cast<const std::uint8_t*>(s), 9) == 15);

	// brackets/punctuation are leaves-adjacent; only digit runs are tokens
	const char* t = "[10,20,30]";
	CHECK(core::reduce_ints<plus63>(reinterpret_cast<const std::uint8_t*>(t), 10) == 60);

	// random vs a serial fold
	std::uint64_t seed = 88172645463325252ull;
	std::int64_t  serial = 0;
	std::string	  buf;
	for (int i = 0; i < 50000; ++i) {
		seed ^= seed << 13;
		seed ^= seed >> 7;
		seed ^= seed << 17;
		const std::int64_t v = static_cast<std::int64_t>(seed % 1000);
		serial += v;
		if (i)
			buf.push_back(',');
		buf += std::to_string(v);
	}
	CHECK(
		core::reduce_ints<plus63>(reinterpret_cast<const std::uint8_t*>(buf.data()), buf.size())
		== serial
	);
}

// --- S4-a: PEG expression grammar -> tree cell (front-end widening) ---------
// expr ::= num ('+' expr)*   -- single right self-recursion, associative '+'
struct expr_grammar {
	struct expr {};
	struct num {};
	using rules = type_list<expr, num>;
	using start = expr;
	template<class>
	struct def;
};
template<>
struct expr_grammar::def<expr_grammar::num> {
	using type = plus<cls<'0', '1', '2', '3', '4', '5', '6', '7', '8', '9'>>;
};
template<>
struct expr_grammar::def<expr_grammar::expr> {
	using type = seq<rule<expr_grammar::num>, star<seq<lit<'+'>, rule<expr_grammar::expr>>>>;
};

constexpr auto kExpr = core::lower<expr_grammar>();

// WIDENING PROOF: the OLD front-end rejects this grammar (recursive and not
// bracket-guarded -> not a sound data-parallel lowering); the NEW core lowers
// it to a `tree` cell.
static_assert(!compile::is_supported<expr_grammar>, "old gate should reject");
static_assert(kExpr.n_cells >= 1, "new core should accept");

static void test_tree_lowering() {
	// the front end must have recognized the monoid pattern and emitted a tree cell
	char op = 0;
	for (std::uint8_t d = 0; d < kExpr.n_cells; ++d)
		if (kExpr.cells[d].sch == core::schema::tree)
			op = kExpr.cells[d].op;
	CHECK(op == '+');

	const char* s = "1+2+3+40";
	CHECK(core::reduce<kExpr>(reinterpret_cast<const std::uint8_t*>(s), 8) == 46);
}

int main() {
	const std::string cases[] = {
		R"({"name":"a\"b","n":[1,2.5e-3,true,null],"x":{}})",
		R"([1,2,3])",
		R"({"k":"\\\"x"})",
		R"({"a":{"b":[true,false,null]},"c":""})",
	};

	for (const std::string& in : cases) {
		const auto* p = reinterpret_cast<const std::uint8_t*>(in.data());
		auto		c = core::scan<kCore>(p, in.size());

		model::scanner<kOld> ref;
		auto				 r = ref.scan(in);

		CHECK(c.pos.size() == r.positions.size());
		CHECK(c.ch.size() == r.chars.size());
		for (std::size_t i = 0; i < c.pos.size() && i < r.positions.size(); ++i) {
			CHECK(c.pos[i] == r.positions[i]);
			CHECK(static_cast<std::uint8_t>(c.ch[i]) == r.chars[i]);
		}
	}

	test_tree_schema();
	test_tree_lowering();

	if (g_fail == 0)
		std::printf("all core-pipeline tests passed\n");
	return g_fail == 0 ? 0 : 1;
}
