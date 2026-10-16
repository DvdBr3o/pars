// tests/test_algebra.cpp — the unified core: ONE primitive, THREE instances.
//
// Proves the claim of include/pars/algebra/summary.hpp operationally:
//   * every instance's `compose` is associative (fuzzed, not asserted);
//   * the SAME `prefix_reduce` computes all three parser fragments;
//   * each result matches a naive scalar reference.
// If this passes, "R1..R6" are not separate rules — they are one prefix
// product over three different summary monoids.
#include "pars/algebra/summary.hpp"

#include <cstdint>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

using namespace pars::algebra;

static int g_fail = 0;
#define CHECK(c)                                                                                   \
	do {                                                                                           \
		if (!(c)) {                                                                                \
			std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                               \
			++g_fail;                                                                              \
		}                                                                                          \
	} while (0)

// ---------------------------------------------------------------------------
// associativity fuzz: compose(compose(a,b),c) == compose(a,compose(b,c))
// ---------------------------------------------------------------------------
template<Summary S, class Gen>
static void fuzz_assoc(Gen&& gen, int iters) {
	for (int i = 0; i < iters; ++i) {
		const S a = gen(), b = gen(), c = gen();
		const S l = S::compose(S::compose(a, b), c);
		const S r = S::compose(a, S::compose(b, c));
		// equality is instance-specific (each instance defines operator==)
		CHECK(l == r);
	}
}

// ---------------------------------------------------------------------------
// instance 1: finite monoid — JSON string state (out / in / esc)
// ---------------------------------------------------------------------------
static state_map<3> byte_map(char c) {
	// f[state] = next state
	state_map<3> m;
	// state 0 out, 1 in, 2 esc
	for (int s = 0; s < 3; ++s) {
		int n;
		if (s == 2)
			n = 1;  // escaped char is consumed, back inside
		else if (s == 1)
			n = (c == '\\') ? 2 : (c == '"' ? 0 : 1);
		else
			n = (c == '"') ? 1 : 0;
		m.f[s] = (std::uint8_t)n;
	}
	return m;
}

static void test_finite() {
	const std::string in = "a\"b\\\"c\"d";
	std::vector<state_map<3>> p;
	for (char c : in) p.push_back(byte_map(c));
	prefix_reduce(p.data(), p.size());

	// naive reference: state before each byte
	int		st = 0;
	for (std::size_t i = 0; i < in.size(); ++i) {
		// state before byte i from the scan
		int viaScan = (i == 0) ? 0 : p[i - 1].f[0];
		CHECK(viaScan == st);
		// advance naive
		st = byte_map(in[i]).f[st];
	}
}

// ---------------------------------------------------------------------------
// instance 2: stack monoid — bracket balance over "(())()"
// ---------------------------------------------------------------------------
static void test_stack() {
	const std::string			  in = "(())()";
	std::vector<bracket_balance>  p;
	for (char c : in)
		p.push_back(c == '('	  ? bracket_balance {0, 1}
					 : c == ')' ? bracket_balance {1, 0}
								: bracket_balance {0, 0});
	prefix_reduce(p.data(), p.size());

	int depth = 0;
	for (std::size_t i = 0; i < in.size(); ++i) {
		depth += (in[i] == '(') ? 1 : (in[i] == ')') ? -1 : 0;
		CHECK(p[i].depth_after() == depth);
		CHECK(!p[i].balanced() || depth == 0);
	}
	CHECK(p.back().balanced());
}

// ---------------------------------------------------------------------------
// instance 3: tree/associative monoid — prefix sums
// ---------------------------------------------------------------------------
struct plus {
	int operator()(int a, int b) const { return a + b; }
};

static void test_tree() {
	const std::vector<int>			 vals = {3, 1, 4, 1, 5, 9, 2, 6};
	std::vector<reducer<int, plus>>	 p;
	for (int v : vals) p.push_back({true, v});
	prefix_reduce(p.data(), p.size());

	int run = 0;
	for (std::size_t i = 0; i < vals.size(); ++i) {
		run += vals[i];
		CHECK(p[i].v == run);
	}
}

// ---------------------------------------------------------------------------
// instance 4: ordered choice (PEG `/`) — first-success is a monoid
// ---------------------------------------------------------------------------
static void test_choice() {
	using RC = ranked_choice<int>;
	// both matched -> lower rank (earlier alternative) wins, regardless of fold order
	CHECK(RC::compose(RC::of(1, 10), RC::of(0, 20)).v == 20);
	CHECK(RC::compose(RC::of(0, 7), RC::of(2, 9)).v == 7);
	// identity is failure
	CHECK(RC::compose(RC::identity(), RC::of(1, 5)).v == 5);
	CHECK(!RC::compose(RC::identity(), RC::identity()).matched);
}

int main() {
	std::mt19937 rng(12345);

	fuzz_assoc<state_map<3>>(
		[&] {
			state_map<3> m;
			for (auto& x : m.f) x = (std::uint8_t)(rng() % 3);
			return m;
		},
		20000
	);
	fuzz_assoc<bracket_balance>(
		[&] { return bracket_balance {(std::int64_t)(rng() % 5), (std::int64_t)(rng() % 5)}; },
		20000
	);
	fuzz_assoc<reducer<int, plus>>(
		[&] { return reducer<int, plus> {true, (int)(rng() % 100)}; },
		20000
	);

	test_finite();
	test_stack();
	test_tree();
	test_choice();

	fuzz_assoc<ranked_choice<int>>(
		[&] {
			if (rng() % 4 == 0) return ranked_choice<int>::identity();
			return ranked_choice<int>::of((std::uint8_t)(rng() % 3), (int)(rng() % 100));
		},
		20000
	);

	if (g_fail == 0) std::printf("all algebra-core tests passed\n");
	return g_fail == 0 ? 0 : 1;
}
