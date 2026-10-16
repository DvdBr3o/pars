// bench/bench_mem_sota.cpp — host-RAM footprint of the SOTA parsers.
//
// Runs ONE language per process (fresh peak RSS), parses a payload, and reports
// the DOM/tape footprint as a ratio of the input.  Kept in a host TU (simdjson/
// toml++/pugixml/inih headers).
//
// usage: pars.bench.mem_sota <lang(json|toml|xml|ini)> <scale_mb>
// output: format,impl,scale_mb,input_mib,used_mib,ratio
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

namespace bp = bench_payloads;

static long peak_kb() {
	rusage r {};
	getrusage(RUSAGE_SELF, &r);
	return r.ru_maxrss;	 // KiB on Linux
}

int main(int argc, char** argv) {
	if (argc < 3)
		return 1;
	const std::string lang = argv[1];
	const std::size_t mb   = std::strtoull(argv[2], nullptr, 10);
	const std::size_t tgt  = mb * 1024 * 1024;

	std::printf("# format,impl,scale_mb,input_mib,used_mib,ratio\n");

	auto report = [&](const char* fmt, const char* impl, std::size_t n, long before, long after) {
		const double input_mib = static_cast<double>(n) / 1048576.0;
		const double used_mib  = static_cast<double>(after - before) / 1024.0;
		std::printf("%s,%s,%zu,%.1f,%.1f,%.2f\n", fmt, impl, mb, input_mib, used_mib,
					used_mib / input_mib);
	};

	if (lang == "json") {
		std::string				 doc = bp::gen_json(tgt);
		simdjson::dom::parser	 p;
		long					 b = peak_kb();
		simdjson::dom::element	 root;
		volatile std::size_t	 keep = 0;
		if (!p.parse(doc).get(root)) keep = static_cast<std::size_t>(root.type());
		long a = peak_kb();
		report("json", "simdjson", doc.size(), b, a);
		(void)keep;
	} else if (lang == "toml") {
		std::string	   doc = bp::gen_toml(tgt);
		long		   b   = peak_kb();
		toml::table	   t   = toml::parse(doc);
		long		   a   = peak_kb();
		report("toml", "toml++", doc.size(), b, a);
		(void)t;
	} else if (lang == "xml") {
		std::string		   doc = bp::gen_xml(tgt);
		long			   b   = peak_kb();
		pugi::xml_document pd;
		pd.load_buffer(doc.data(), doc.size());
		long			   a   = peak_kb();
		report("xml", "pugixml", doc.size(), b, a);
	} else if (lang == "ini") {
		std::string									   doc = bp::gen_ini(tgt);
		std::unordered_map<std::string, std::string>   m;
		long										   b = peak_kb();
		ini_parse_string(doc.c_str(), [](void* u, const char* s, const char* n, const char* v) {
			auto* mm = static_cast<std::unordered_map<std::string, std::string>*>(u);
			(*mm)[std::string(s) + "." + n] = v;
			return 1;
		}, &m);
		long a = peak_kb();
		report("ini", "inih", doc.size(), b, a);
	}
	return 0;
}
