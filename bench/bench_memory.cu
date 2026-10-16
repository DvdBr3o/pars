// bench/bench_memory.cu — device working set of the generated scanner.
//
// For each scale, run the canonical unified API once and report how much device
// memory the scanner holds (persistent cache + staging) vs the input size.
//
// Output: format,impl,scale_mb,input_mib,used_mib,ratio
#include "pars/pars.hpp"
#include "pars/scan.hpp"

#include "grammars/json.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include <cuda_runtime.h>

using namespace pars;

static std::string read_file(const std::string& path) {
	std::ifstream f(path, std::ios::binary);
	if (!f) return {};
	return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

int main(int argc, char** argv) {
	const std::string pdir = argc > 1 ? argv[1] : "/tmp/payloads";
	const std::size_t scales[] = {8, 16, 32, 64, 128, 256, 512};

	cudaFree(nullptr);	// init context
	std::size_t total = 0, free0 = 0;
	cudaMemGetInfo(&free0, &total);

	std::printf("# format,impl,scale_mb,input_mib,used_mib,ratio\n");
	for (std::size_t mb : scales) {
		std::string json = read_file(pdir + "/json_" + std::to_string(mb) + ".json");
		if (json.empty()) {
			std::fprintf(stderr, "missing %zu\n", mb);
			continue;
		}
		auto r = scan<bench::json_grammar>(json);
		cudaDeviceSynchronize();
		std::size_t fr = 0, tot = 0;
		cudaMemGetInfo(&fr, &tot);
		const double input_mib = static_cast<double>(json.size()) / 1048576.0;
		const double used_mib  = static_cast<double>(free0 - fr) / 1048576.0;
		std::printf(
			"json,pars,%zu,%.1f,%.1f,%.2f\n", mb, input_mib, used_mib, used_mib / input_mib
		);
		std::fflush(stdout);
		(void)r;
	}
	(void)total;
	return 0;
}
