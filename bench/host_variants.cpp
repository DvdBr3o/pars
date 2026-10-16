// bench/host_variants.cpp — host-only SOTA baselines for the two consumer
// variants (ondemand = lazy index consume; dom = materialise).
//
// simdjson (JSON, both variants), toml++ (TOML, dom), pugixml (XML, dom),
// inih (INI, dom).  Kept in a host TU because nvcc cannot parse these headers.
// CSV (simdcsv, token index) and C-like (flex, tokens) are separate binaries,
// so they are not callable from here.
#include <simdjson.h>
#include <toml++/toml.h>

#include "ini.h"
#include "pugixml.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace std::chrono;

double gbps(std::size_t bytes, double s) { return static_cast<double>(bytes) / s / 1e9; }

template<class F>
static void emit(const char* fmt, const char* impl, const char* variant, std::size_t mb,
				 std::size_t n, int iters, F&& f) {
	std::vector<double> g;
	g.reserve(iters);
	f();  // warm up
	for (int i = 0; i < iters; ++i) {
		auto a = steady_clock::now();
		f();
		auto b = steady_clock::now();
		g.push_back(gbps(n, duration<double>(b - a).count()));
	}
	std::sort(g.begin(), g.end());
	double sum = 0;
	for (double v : g) sum += v;
	std::printf("%s,%s,%s,%zu,%.3f,0\n", fmt, impl, variant, mb, sum / g.size());
	std::fflush(stdout);
}

int ini_cb(void* user, const char*, const char*, const char*) {
	++*static_cast<std::uint64_t*>(user);
	return 1;
}
}  // namespace

namespace {
// Shape-agnostic recursive traversal: touch every key and value once.  Works on
// the real corpus (GeoJSON features with nested geometry), not just a fixed
// record shape.
void walk_dom(simdjson::dom::element e, std::uint64_t& sink) {
	switch (e.type()) {
		case simdjson::dom::element_type::OBJECT:
			for (auto [k, v] : e.get_object()) {
				sink += k.size();
				walk_dom(v, sink);
			}
			break;
		case simdjson::dom::element_type::ARRAY:
			for (auto v : e.get_array()) walk_dom(v, sink);
			break;
		case simdjson::dom::element_type::STRING:
			{
				std::string_view s;
				if (!e.get_string().get(s)) sink += s.size();
			}
			break;
		case simdjson::dom::element_type::INT64:
			{
				int64_t x;
				if (!e.get_int64().get(x)) sink += static_cast<std::uint64_t>(x);
			}
			break;
		case simdjson::dom::element_type::DOUBLE:
			{
				double x;
				if (!e.get_double().get(x)) sink += static_cast<std::uint64_t>(x);
			}
			break;
		default: break;
	}
}

template<class V>
void walk_od(V&& v, std::uint64_t& sink) {
	switch (v.type()) {
		case simdjson::ondemand::json_type::object:
			{
				simdjson::ondemand::object o;
				if (v.get_object().get(o)) break;
				for (auto f : o) {
					std::string_view k = f.unescaped_key();
					sink += k.size();
					walk_od(f.value(), sink);
				}
			}
			break;
		case simdjson::ondemand::json_type::array:
			{
				simdjson::ondemand::array a;
				if (v.get_array().get(a)) break;
				for (auto e : a) walk_od(e, sink);
			}
			break;
		case simdjson::ondemand::json_type::string:
			{
				std::string_view s;
				if (!v.get_string().get(s)) sink += s.size();
			}
			break;
		case simdjson::ondemand::json_type::number:
			{
				double d;
				if (!v.get_double().get(d)) sink += static_cast<std::uint64_t>(d);
			}
			break;
		default: break;
	}
}
}  // namespace

extern "C" void bv_simdjson_ondemand(const char* data, std::size_t n, int iters, std::size_t mb) {
	simdjson::padded_string	   ps(data, n);
	simdjson::ondemand::parser p;
	std::uint64_t			   sink = 0;
	auto					   run	= [&] {
		 simdjson::ondemand::document doc = p.iterate(ps);
		 simdjson::ondemand::array	  arr;
		 if (doc.get_array().get(arr))
			 return;
		 for (auto v : arr) walk_od(v, sink);
	 };
	emit("json", "simdjson", "ondemand", mb, n, iters, run);
	(void)sink;
}

extern "C" void bv_simdjson_dom(const char* data, std::size_t n, int iters, std::size_t mb) {
	simdjson::padded_string ps(data, n);
	simdjson::dom::parser	p;
	std::uint64_t			sink = 0;
	auto					run	 = [&] {
		   simdjson::dom::element root;
		   if (p.parse(ps).get(root))
			   return;
		   walk_dom(root, sink);
	   };
	emit("json", "simdjson", "dom", mb, n, iters, run);
	(void)sink;
}

extern "C" void bv_toml_dom(const char* data, std::size_t n, int iters, std::size_t mb) {
	std::uint64_t sink = 0;
	auto		  run  = [&] {
		 try {
			 toml::table t = toml::parse(std::string_view(data, n));
			 sink += t.size();
		 } catch (...) {
		 }
	};
	emit("toml", "toml++", "dom", mb, n, iters, run);
	(void)sink;
}

extern "C" void bv_xml_dom(const char* data, std::size_t n, int iters, std::size_t mb) {
	std::uint64_t sink = 0;
	auto		  run  = [&] {
		 pugi::xml_document doc;
		 if (doc.load_buffer(data, n)) {
			 std::uint64_t c = 0;
			 for (auto child : doc.children()) {
				 (void)child;
				 ++c;
			 }
			 sink += c;
		 }
	 };
	emit("xml", "pugixml", "dom", mb, n, iters, run);
	(void)sink;
}

extern "C" void bv_ini_dom(const char* data, std::size_t n, int iters, std::size_t mb) {
	std::uint64_t sink = 0;
	std::string	  doc(data, n);	 // inih needs a NUL-terminated buffer; hoist out of the loop
	auto		  run = [&] {
		 std::uint64_t cnt = 0;
		 ini_parse_string(doc.c_str(), ini_cb, &cnt);
		 sink += cnt;
	};
	emit("ini", "inih", "dom", mb, n, iters, run);
	(void)sink;
}
