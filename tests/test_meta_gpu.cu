// tests/test_meta_gpu.cu — the user-facing API `pars<Grammar>(buffer)`.
//
// Validates the CUDA-accelerated entry point against the CPU reference scanner:
// CSV exercises the fused parity head end-to-end; JSON exercises the delegated
// path with bracket pairing.
#include "pars/parse.cuh"
#include "pars/pars.hpp"

#include "../bench/grammars/csv.hpp"
#include "../bench/grammars/json.hpp"

#include <cstdint>
#include <cstdio>
#include <string>

using namespace pars;

namespace {
// `using pars::pars` shadows the global `pars` namespace in this scope, so the
// call reads exactly `pars<Grammar>(buffer)`.
using pars::pars;

template<class Grammar>
bool check(const std::string& s, const char* name, bool want_pairs) {
	constexpr auto	  S = compile::make_scan_plan<Grammar>();
	model::scanner<S> cpu;
	auto			  c = cpu.scan(s);

	auto r = pars<Grammar>(s, parse_options {want_pairs});

	bool ok = c.positions.size() == r.structural.size();
	if (ok) {
		for (std::size_t i = 0; i < c.positions.size(); ++i)
			if ((std::int32_t)c.positions[i] != r.structural[i]) {
				ok = false;
				break;
			}
	}
	std::printf(
		"[api] %-4s cpu=%zu api=%zu pairs=%zu ok=%d\n",
		name,
		c.positions.size(),
		r.structural.size(),
		r.pair.size(),
		(int)ok
	);
	return ok;
}

}  // namespace

int main() {
	std::string csv;
	const char* rec = "alpha,beta,\"quoted, field\",\"has \"\"q\"\"\",42,true\n";
	for (int i = 0; i < 2000; ++i) csv += rec;

	std::string json = "[";
	const char* jrec = "{\"id\":123,\"name\":\"x \\\"y\\\"\",\"a\":[1,2,3],\"n\":{\"z\":null}}";
	for (int i = 0; i < 2000; ++i) {
		if (i)
			json += ",";
		json += jrec;
	}
	json += "]";

	bool ok = true;
	ok &= check<bench::csv_grammar>(csv, "csv", false);
	ok &= check<bench::json_grammar>(json, "json", true);
	std::printf("[api] %s\n", ok ? "ALL PASS" : "FAIL");
	return ok ? 0 : 1;
}
