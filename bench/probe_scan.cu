// bench/probe_scan.cu — isolation probe for pars::scan<Grammar>.
#include "pars/pars.hpp"
#include "pars/scan.hpp"

#include "grammars/json.hpp"

#include <thrust/execution_policy.h>
#include <thrust/scan.h>

#include <cstdint>
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
	// warm thrust up on the default stream first
	{
		std::uint32_t* d = nullptr;
		cudaMalloc(&d, 64);
		cudaMemset(d, 0, 64);
		thrust::inclusive_scan(thrust::cuda::par, d, d + 16, d);
		cudaDeviceSynchronize();
		cudaFree(d);
	}
	std::fprintf(stderr, "probe: n=%zu\n", json.size());
	auto r = scan<bench::json_grammar>(json);
	std::fprintf(stderr, "probe: tokens=%zu\n", r.tokens());
	return 0;
}
