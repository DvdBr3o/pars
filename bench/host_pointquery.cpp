// bench/host_pointquery.cpp — host-only SOTA point-query baselines.
//
// toml++ / pugixml / inih / simdjson are host-only, template-heavy headers that
// nvcc's front-end (cudafe++) cannot reliably parse inside a .cu (it emits
// spurious "‘__T###’ was not declared in this scope" from toml++).  They are
// therefore kept here, compiled by the host compiler only, and called from
// bench_pointquery.cu through C linkage.
//
// Each function parses the document once (excluded from the timed region) and
// returns the average nanoseconds of one point lookup over `reps` repetitions.
#include <simdjson.h>
#include <toml++/toml.h>
#include "pugixml.hpp"
#include "ini.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

namespace {

volatile std::uint64_t g_host_sink = 0;	 // defeats DCE

template<class F>
double time_queries(int reps, F&& f) {
	for (int i = 0; i < 3; ++i) f();	 // warmup
	auto a = std::chrono::steady_clock::now();
	for (int i = 0; i < reps; ++i) f();
	auto b = std::chrono::steady_clock::now();
	return std::chrono::duration<double, std::nano>(b - a).count() / reps;
}

int ini_count_cb(void* user, const char* section, const char* name, const char* value) {
	auto* m = static_cast<std::unordered_map<std::string, std::string>*>(user);
	(*m)[std::string(section) + "." + name] = value;
	return 1;
}

}  // namespace

extern "C" double pq_json_simdjson(const char* data, std::size_t n, int reps, int* ok) {
	std::string			   doc(data, n);
	simdjson::dom::parser  p;
	simdjson::dom::element root;
	if (p.parse(doc).get(root)) {
		*ok = 0;
		return 0;
	}
	simdjson::dom::array   arr	 = root.get_array();
	simdjson::dom::element first = arr.at(0);
	int64_t				   idv	 = 0;
	simdjson::error_code   err	 = first["id"].get(idv);
	(void)err;
	*ok = (idv == 123);
	return time_queries(reps, [&] {
		int64_t				 x	 = 0;
		simdjson::error_code e	 = arr.at(0)["id"].get(x);
		(void)e;
		g_host_sink = g_host_sink + (std::uint64_t)x;
	});
}

extern "C" double pq_ini_inih(const char* data, std::size_t n, int reps, int* ok) {
	std::string									 doc(data, n);
	std::unordered_map<std::string, std::string> m;
	ini_parse_string(doc.c_str(), ini_count_cb, &m);
	*ok = (m.find("section.num") != m.end());
	return time_queries(reps, [&] { g_host_sink = g_host_sink + (m.find("section.num") != m.end()); });
}

extern "C" double pq_toml_tomlpp(const char* data, std::size_t n, int reps, int* ok) {
	toml::table tbl = toml::parse(std::string_view(data, n));
	int64_t		port = 0;
	if (auto node = tbl["t0"]["port"].value<int64_t>())
		port = *node;
	*ok = (port == 8080);
	return time_queries(reps, [&] {
		g_host_sink =
			g_host_sink + (std::uint64_t)tbl["t0"]["port"].value<int64_t>().value_or(0);
	});
}

extern "C" double pq_xml_pugixml(const char* data, std::size_t n, int reps, int* ok) {
	pugi::xml_document pdoc;
	pdoc.load_buffer(data, n);
	auto item_node = pdoc.child("root").child("item");
	*ok = !item_node.attribute("id").empty();
	return time_queries(reps, [&] {
		g_host_sink = g_host_sink + (std::uint64_t)item_node.attribute("id").value()[0];
	});
}
