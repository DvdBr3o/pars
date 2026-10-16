// bench/probe_scan.cu — isolation probe for pars::scan<Grammar>.
#include "pars/pars.hpp"
#include "pars/scan.hpp"

#include "grammars/json.hpp"

#include <cstdio>
#include <string>

using namespace pars;

int main() {
	std::string rec	 = "{\"id\":123,\"name\":\"x\"}";
	std::string json = "[";
	for (int i = 0; i < 200000; ++i) {
		if (i) json += ',';
		json += rec;
	}
	json += "]";
	std::fprintf(stderr, "probe: n=%zu\n", json.size());
	auto r = scan<bench::json_grammar>(json);
	std::fprintf(stderr, "probe: tokens=%zu\n", r.tokens());
	return 0;
}
