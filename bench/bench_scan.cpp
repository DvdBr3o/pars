// bench/bench_scan.cpp — Model-M CPU scanner throughput (correctness backend).
#include "pars/pars.hpp"
#include "grammars/json.hpp"
#include "grammars/csv.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace std::chrono;
using namespace pars;

static double gbps(std::size_t bytes, double s) {
	return (double)bytes / s / 1e9;
}

template<scan_plan P>
static double run_bench(const std::string& d, int iters) {
	model::scanner<P> sc;
	(void)sc.scan(d);
	auto		t0 = steady_clock::now();
	std::size_t n  = 0;
	for (int i = 0; i < iters; ++i) n = sc.scan(d).positions.size();
	auto t1 = steady_clock::now();
	std::fprintf(stderr, "  tokens=%zu\n", n);
	return gbps(d.size() * iters, duration<double>(t1 - t0).count());
}

static std::string repeat(const std::string& rec, std::size_t target) {
	std::string out;
	out.reserve(target);
	while (out.size() < target) out += rec;
	return out;
}

int main(int argc, char** argv) {
	const std::size_t mb	= argc > 1 ? (std::size_t)std::atol(argv[1]) : 64;
	const int		  iters = argc > 2 ? std::atoi(argv[2]) : 3;
	std::string		  json;
	{
		const std::string rec =
			"{\"id\":123,\"name\":\"x \\\"y\\\"\",\"a\":[1,2,3],\"n\":{\"z\":null}}";
		json.push_back('[');
		while (json.size() < mb * 1024 * 1024) {
			if (json.size() > 1)
				json.push_back(',');
			json += rec;
		}
		json.push_back(']');
	}
	std::string csv =
		repeat("alpha,beta,\"quoted, field\",\"has \"\"q\"\"\",42,true\n", mb * 1024 * 1024);

	constexpr auto kJ = compile::make_scan_plan<bench::json_grammar>();
	constexpr auto kC = compile::make_scan_plan<bench::csv_grammar>();
	std::printf("json,pars-m-cpu,%.3f\n", run_bench<kJ>(json, iters));
	std::printf("csv,pars-m-cpu,%.3f\n", run_bench<kC>(csv, iters));
	return 0;
}
