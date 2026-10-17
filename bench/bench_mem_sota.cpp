// bench/bench_mem_sota.cpp — host-RAM footprint of the SOTA parsers.
//
// Runs ONE language per process (fresh peak RSS), parses a payload with the
// SAME parser work the parser is meant to do, and reports the DOM/tape
// footprint as a ratio of the input.
//
// FAIRNESS RULES (see docs/model/bench_memory.md):
//   * The reported number is the REsident growth of a LIVE parse: build the
//     object model once, then sample /proc/self/statm.  A short-lived DOM that
//     is freed before the sample would report ~0.
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
#include <vector>

#include <sys/resource.h>
#include <unistd.h>

namespace bp = bench_payloads;

// flex-generated C-like lexer (same one as pars.baseline.clike), declared here
// so this probe can measure the index it builds.  Linux-only (the generated
// lexer uses unistd.h), like the other flex/simdcsv targets.
#ifndef _WIN32
// The generated lexer declares these as extern; the owning TU must define them.
std::vector<std::uint32_t> g_pos;
const char*				   g_base = nullptr;
struct yy_buffer_state;
extern int				yylex(void);
extern yy_buffer_state* yy_scan_bytes(const char*, int);
extern void				yy_delete_buffer(yy_buffer_state*);
#endif

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
static void measure(const char* fmt, const char* impl, std::size_t n, std::size_t mb, Run&& run) {
	// `run()` must keep the object model it builds alive (e.g. store it in a
	// captured variable), so we sample RSS while the DOM is resident.  The
	// footprint is the RSS growth of a live parse, not the bump of a
	// short-lived allocation that is freed before we look.  We build exactly
	// once: a second build would accumulate a second DOM in RSS.
	const long before = resident_kb();
	run();
	const long during = resident_kb();

	const double input_mib = static_cast<double>(n) / 1048576.0;
	const double used_mib  = static_cast<double>(during - before) / 1024.0;
	std::printf(
		"%s,%s,host,%zu,%.1f,%.1f,%.2f\n",
		fmt,
		impl,
		mb,
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

	std::printf("# format,impl,placement,scale_mb,input_mib,used_mib,ratio\n");

	if (lang == "json") {
		std::string				doc = bp::gen_json(tgt);
		simdjson::dom::parser	p;
		simdjson::dom::element	root;	 // keep the DOM resident during sampling
		measure("json", "simdjson", doc.size(), mb, [&] {
			if (!p.parse(doc).get(root)) g_sink = g_sink + static_cast<std::size_t>(root.type());
		});
	} else if (lang == "toml") {
		std::string	 doc = bp::gen_toml(tgt);
		toml::table	 t;	 // keep the table resident during sampling
		measure("toml", "toml++", doc.size(), mb, [&] {
			t = toml::parse(doc);
			g_sink = g_sink + t.size();
		});
	} else if (lang == "xml") {
		std::string			doc = bp::gen_xml(tgt);
		pugi::xml_document	pd;	 // one DOM, kept alive (pars reuses its buffers)
		measure("xml", "pugixml", doc.size(), mb, [&] {
			pd.reset();
			pugi::xml_parse_result r = pd.load_buffer(doc.data(), doc.size());
			g_sink = g_sink + static_cast<std::size_t>(r.status) + pd.child("item").attribute("id").as_uint();
		});
	} else if (lang == "ini") {
		std::string										doc = bp::gen_ini(tgt);
		std::unordered_map<std::string, std::string>	m;	 // the real consumer
		measure("ini", "inih", doc.size(), mb, [&] {
			m.clear();
			ini_parse_string(doc.c_str(), [](void* u, const char* s, const char* n, const char* v) {
				auto* mm = static_cast<std::unordered_map<std::string, std::string>*>(u);
				(*mm)[std::string(s) + "." + n] = v;
				return 1;
			}, &m);
			g_sink = g_sink + m.size();
		});
	} else if (lang == "clike") {
#ifndef _WIN32
		std::string doc = bp::gen_clike(tgt);
		g_base			= doc.data();
		measure("clike", "flex", doc.size(), mb, [&] {
			g_pos.clear();
			auto* b = yy_scan_bytes(doc.data(), (int)doc.size());
			yylex();
			yy_delete_buffer(b);
			g_sink = g_sink + g_pos.size();
		});
#endif
	}
	return 0;
}
