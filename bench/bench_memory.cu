// bench/bench_memory.cu — pars device working set, per language.
//
// For each grammar, measure how much device memory the scanner holds vs input.
//
// usage: pars.bench.memory <scale_mb> [payload_dir]
// output: format,impl,scale_mb,input_mib,used_mib,ratio
#include "payloads.hpp"

#include "pars/pars.hpp"
#include "pars/scan.hpp"

#include "grammars/clike.hpp"
#include "grammars/csv.hpp"
#include "grammars/ini.hpp"
#include "grammars/json.hpp"
#include "grammars/toml.hpp"
#include "grammars/xml.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

#include <cuda_runtime.h>

using namespace pars;
namespace bp = bench_payloads;

template<class Grammar>
static void one(const char* fmt, const std::string& payload, std::size_t mb) {
	if (payload.empty())
		return;
	std::size_t fb = 0, tot = 0;
	cudaMemGetInfo(&fb, &tot);
	auto r = scan<Grammar>(payload);
	cudaDeviceSynchronize();
	std::size_t fa = 0;
	cudaMemGetInfo(&fa, &tot);
	const double input_mib = static_cast<double>(payload.size()) / 1048576.0;
	const double used_mib  = static_cast<double>(fb - fa) / 1048576.0;
	std::printf("%s,pars,%zu,%.1f,%.1f,%.2f\n", fmt, mb, input_mib, used_mib, used_mib / input_mib);
	std::fflush(stdout);
	(void)r;
}

int main(int argc, char** argv) {
	const std::size_t mb	= argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 256;
	const std::string pdir	= argc > 2 ? argv[2] : "";
	const std::size_t target = mb * 1024 * 1024;

	cudaFree(nullptr);

	std::string json = pdir.empty() ? "" : bp::read_file(pdir + "/json_" + std::to_string(mb) + ".json");
	if (json.empty())
		json = bp::gen_json(target);

	std::printf("# format,impl,scale_mb,input_mib,used_mib,ratio\n");
	one<bench::json_grammar>("json", json, mb);
	one<bench::csv_grammar>("csv", bp::gen_csv(target), mb);
	one<bench::toml_grammar>("toml", bp::gen_toml(target), mb);
	one<bench::ini_grammar>("ini", bp::gen_ini(target), mb);
	one<bench::xml_grammar>("xml", bp::gen_xml(target), mb);
	one<bench::clike_grammar>("clike", bp::gen_clike(target), mb);
	return 0;
}
