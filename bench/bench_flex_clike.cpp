// bench/bench_flex_clike.cpp — flex-generated C-like lexer baseline.
//
// Emits the same structural index (`;`, `{`, `}` outside comments) as the pars
// C-like grammar, so the two are comparable.  Flex is a standard DFA lexer
// generator; there is no SIMD/GPU parser for a generic C-like token grammar.
//
// Output: format,impl,variant,scale_mb,GBps_p50,GBps_p95,GBps_min,GBps_max,tokens,bytes_moved
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

std::vector<std::uint32_t> g_pos;
const char*				   g_base = nullptr;

struct yy_buffer_state;
extern int				yylex(void);
extern yy_buffer_state* yy_scan_bytes(const char*, int);
extern void				yy_delete_buffer(yy_buffer_state*);

using namespace std::chrono;

static double gbps(std::size_t bytes, double s) {
	return (double)bytes / s / 1e9;
}

static std::string rep(const std::string& rec, std::size_t target) {
	std::string out;
	out.reserve(target);
	while (out.size() < target) out += rec;
	return out;
}

int main(int argc, char** argv) {
	const std::size_t mb	= argc > 1 ? (std::size_t)std::atol(argv[1]) : 64;
	const int		  iters = argc > 2 ? std::atoi(argv[2]) : 10;

	std::string		  clike = rep("int x; /* c { ; } */ y = 1; // { ; }\n", mb * 1024 * 1024);
	g_base					= clike.data();

	{
		auto* b = yy_scan_bytes(clike.data(), (int)clike.size());
		yylex();
		yy_delete_buffer(b);
	}

	std::vector<double> g;
	g.reserve(iters);
	for (int i = 0; i < iters; ++i) {
		g_pos.clear();
		auto  a = steady_clock::now();
		auto* b = yy_scan_bytes(clike.data(), (int)clike.size());
		yylex();
		yy_delete_buffer(b);
		auto c = steady_clock::now();
		g.push_back(gbps(clike.size(), duration<double>(c - a).count()));
	}
	std::sort(g.begin(), g.end());
	double sum = 0;
	for (double v : g) sum += v;
	const double mean = g.empty() ? 0.0 : sum / g.size();
	auto q = [&](double p) { return g[std::min(g.size() - 1, (std::size_t)(p * (g.size() - 1)))]; };
	std::printf(
		"# format,impl,variant,scale_mb,GBps_mean,GBps_p50,GBps_p95,GBps_min,GBps_max,tokens,bytes_moved\n"
	);
	std::printf(
		"clike,flex,native-ram,%zu,%.3f,%.3f,%.3f,%.3f,%.3f,%zu,0\n",
		mb,
		mean,
		q(0.50),
		q(0.95),
		g.front(),
		g.back(),
		g_pos.size()
	);
	return 0;
}
