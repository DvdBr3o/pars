// bench/bench_mem_sota.cpp — host-RAM footprint of the SOTA parsers.
//
// Runs ONE language per process (fresh peak RSS), parses a payload with the
// SAME parser work the parser is meant to do, and reports the DOM/tape
// footprint as a ratio of the input.
//
// FAIRNESS RULES (see docs/model/bench_memory.md):
//   * The reported number is the parser's RETIRED working set, not the first
//     allocation's bump.  We warm up, snapshot RSS, then run `iters` parses,
//     snapshot RSS again, and report the STEADY-STATE growth.  Fresh
//     alloc/free inside the loop no longer counts as footprint.
//   * Every baseline builds the object model it is known for:
//       simdjson  -> dom::element (full DOM)
//       toml++    -> toml::table
//       pugixml   -> xml_document (reused across iterations, like pars reuses
//                               its device buffers)
//       inih      -> std::unordered_map<string,string> (the real consumer)
//   * Results are consumed through a volatile sink so the optimiser cannot
//     delete the parse.
//
// usage: pars.bench.mem_sota <lang(json|toml|xml|ini)> <scale_mb>
// output: format,impl,placement,scale_mb,input_mib,used_mib,ratio
#include "payloads.hpp"

#include <simdjson.h>
#include <toml++/toml.h>

#include "ini.h"
#include "pugixml.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_map>

#include <sys/resource.h>
#include <unistd.h>

namespace bp = bench_payloads;

static volatile std::size_t g_sink = 0;

static long peak_kb() {
	rusage r {};
	getrusage(RUSAGE_SELF, &r);
	return r.ru_maxrss;	 // KiB on Linux
}

// Current resident set size (VmLck-free, cheap): /proc/self/statm is in pages.
static long resident_kb() {
	FILE* f = std::fopen("/proc/self/statm", "r");
	if (!f) return peak_kb();
	long total = 0, res = 0;
	if (std::fscanf(f, "%ld %ld", &total, &res) != 2) {
		std::fclose(f);
		return peak_kb();
	}
	std::fclose(f);
	return res * (sysconf(_SC_PAGESIZE) / 1024);
}

template<class Run>
static void measure(const char* fmt, const char* impl, std::size_t n, Run&& run, int iters) {
	for (int i = 0; i < 2; ++i) run();	 // warm up
	const long before = resident_kb();
	for (int i = 0; i < iters; ++i) run();
	const long after = resident_kb();

	const double input_mib = static_cast<double>(n) / 1048576.0;
	const double used_mib  = static_cast<double>(after - before) / 1024.0;
	std::printf(
		"%s,%s,host,%zu,%.1f,%.1f,%.2f\n",
		fmt,
		impl,
		n / 1048576,
		input_mib,
		used_mib,
		used_mib / input_mib
	);
	std::fflush(stdout);
}

int main(int argc, char** argv) {
	if (argc < 3) return 1;
	const std::string lang = argv[1];
	const std::size_t mb   = std::strtoull(argv[2], nullptr, 10);
	const std::size_t tgt  = mb * 1024 * 1024;
	const int		  iters = 5;

	std::printf("# format,impl,placement,scale_mb,input_mib,used_mib,ratio\n");

	if (lang == "json") {
		std::string			   doc = bp::gen_json(tgt);
		simdjson::dom::parser  p;
		measure("json", "simdjson", doc.size(), [&] {
			simdjson::dom::element root;
			if (!p.parse(doc).get(root)) g_sink = g_sink + static_cast<std::size_t>(root.type());
		}, iters);
	} else if (lang == "toml") {
		std::string	   doc = bp::gen_toml(tgt);
		measure("toml", "toml++", doc.size(), [&] {
			toml::table t = toml::parse(doc);
			g_sink = g_sink + t.size();
		}, iters);
	} else if (lang == "xml") {
		std::string		   doc = bp::gen_xml(tgt);
		pugi::xml_document pd;	 // reused across iterations, like pars' buffers
		measure("xml", "pugixml", doc.size(), [&] {
			pd.reset();
			pugi::xml_parse_result r = pd.load_buffer(doc.data(), doc.size());
			g_sink = g_sink + static_cast<std::size_t>(r.status) + pd.child("item").attribute("id").as_uint();
		}, iters);
	} else if (lang == "ini") {
		std::string									   doc = bp::gen_ini(tgt);
		std::unordered_map<std::string, std::string>   m;	 // the real consumer
		measure("ini", "inih", doc.size(), [&] {
			m.clear();
			ini_parse_string(doc.c_str(), [](void* u, const char* s, const char* n, const char* v) {
				auto* mm = static_cast<std::unordered_map<std::string, std::string>*>(u);
				(*mm)[std::string(s) + "." + n] = v;
				return 1;
			}, &m);
			g_sink = g_sink + m.size();
		}, iters);
	}
	return 0;
}
