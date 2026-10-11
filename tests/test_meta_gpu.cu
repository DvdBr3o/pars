// tests/test_meta_gpu.cu — validate the meta backend (rewrite + specialized
// kernels) against the CPU reference scanner.  CSV exercises the fused parity
// head end-to-end; JSON confirms the fallback delegation still matches.
#include "pars/backend/cuda/meta/scan.cuh"
#include "pars/pars.hpp"

#include "../bench/grammars/csv.hpp"
#include "../bench/grammars/json.hpp"

#include <cstdint>
#include <cstdio>
#include <string>

using namespace pars;

template<scan_plan S>
static bool check(const std::string& s, const char* name) {
	model::scanner<S> cpu;
	auto			  c = cpu.scan(s);
	auto			  r = gpu::meta::scan_host_meta<S>(
		 reinterpret_cast<const std::uint8_t*>(s.data()),
		 s.size()
	 );
	bool ok = c.positions.size() == r.structural.size();
	if (ok) {
		for (std::size_t i = 0; i < c.positions.size(); ++i)
			if ((std::int32_t)c.positions[i] != r.structural[i]) {
				ok = false;
				break;
			}
	}
	std::printf(
		"[meta-gpu] %-4s cpu=%zu meta=%zu ok=%d\n",
		name,
		c.positions.size(),
		r.structural.size(),
		(int)ok
	);
	return ok;
}

int main() {
	constexpr auto kJ = compile::make_scan_plan<bench::json_grammar>();
	constexpr auto kC = compile::make_scan_plan<bench::csv_grammar>();

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
	ok &= check<kC>(csv, "csv");
	ok &= check<kJ>(json, "json");
	std::printf("[meta-gpu] %s\n", ok ? "ALL PASS" : "FAIL");
	return ok ? 0 : 1;
}
