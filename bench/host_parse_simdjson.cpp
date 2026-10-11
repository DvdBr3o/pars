// bench/host_parse_simdjson.cpp — simdjson DOM baseline for the complete-parser
// workload (bench_parse).  Kept out of the .cu so nvcc never has to parse
// simdjson.h: on Windows/CUDA-13 CCCL needs /Zc:preprocessor while simdjson
// fails under it, so mixing them in one nvcc TU does not compile.
#include <simdjson.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {
using namespace std::chrono;

double gbps(std::size_t bytes, double s) {
	return static_cast<double>(bytes) / s / 1e9;
}
}  // namespace

extern "C" void bp_json_simdjson(const char* data, std::size_t n, int iters, std::size_t mb) {
	const std::string json(data, n);
	simdjson::dom::parser p;
	uint64_t			  sink = 0;

	std::vector<double> g;
	g.reserve(iters);
	auto run = [&] {
		simdjson::dom::element root;
		if (p.parse(json).get(root))
			return;
		simdjson::dom::array arr = root.get_array();
		for (auto v : arr) sink += static_cast<std::uint64_t>(v.type());
	};
	run();	// warm up
	for (int i = 0; i < iters; ++i) {
		auto a = steady_clock::now();
		run();
		auto b = steady_clock::now();
		g.push_back(gbps(n, duration<double>(b - a).count()));
	}
	(void)sink;

	std::sort(g.begin(), g.end());
	double sum = 0;
	for (double v : g) sum += v;
	auto q = [&](double pp) { return g[std::min(g.size() - 1, (std::size_t)(pp * (g.size() - 1)))]; };
	std::printf(
		"json,simdjson,P,%zu,%.3f,%.3f,%.3f,%.3f,%.3f,0,%zu\n",
		mb,
		sum / g.size(),
		q(0.50),
		q(0.95),
		g.front(),
		g.back(),
		n
	);
	std::fflush(stdout);
}
