// bench/bench_baselines.cpp — domain SOTA baselines with per-iteration stats.
//
// No SIMD/CUDA SOTA exists for TOML/XML/YAML/INI; we use the best optimized CPU
// parsers (toml++, pugixml, inih) plus simdjson for JSON.  CSV/simdcsv is a
// separate binary, run by the `paper-data` xmake task.
//
// Output: format,impl,variant,scale_mb,GBps_p50,GBps_p95,GBps_min,GBps_max,tokens,bytes_moved
#include <simdjson.h>
#include <toml++/toml.h>
#include "pugixml.hpp"
#include "ini.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace std::chrono;

static double gbps(std::size_t bytes, double s) {
	return (double)bytes / s / 1e9;
}

template<class F>
static std::vector<double> sample(std::size_t bytes, int iters, F&& f) {
	std::vector<double> g;
	g.reserve(iters);
	for (int i = 0; i < iters; ++i) {
		auto a = steady_clock::now();
		f();
		auto b = steady_clock::now();
		g.push_back(gbps(bytes, duration<double>(b - a).count()));
	}
	return g;
}

static void emit(const char* fmt, const char* impl, std::size_t mb, std::vector<double> g) {
	std::sort(g.begin(), g.end());
	double sum = 0;
	for (double v : g) sum += v;
	const double mean = g.empty() ? 0.0 : sum / g.size();
	auto q = [&](double p) { return g[std::min(g.size() - 1, (std::size_t)(p * (g.size() - 1)))]; };
	std::printf(
		"%s,%s,native-ram,%zu,%.3f,%.3f,%.3f,%.3f,%.3f,0,0\n",
		fmt,
		impl,
		mb,
		mean,
		q(0.50),
		q(0.95),
		g.front(),
		g.back()
	);
	std::fflush(stdout);
}

static std::string rep(const std::string& rec, std::size_t target) {
	std::string out;
	out.reserve(target);
	while (out.size() < target) out += rec;
	return out;
}

static int ini_cb(void*, const char*, const char*, const char*) {
	return 1;
}

int main(int argc, char** argv) {
	const std::size_t mb	 = argc > 1 ? (std::size_t)std::atol(argv[1]) : 64;
	const int		  iters	 = argc > 2 ? std::atoi(argv[2]) : 10;
	const std::size_t target = mb * 1024 * 1024;

	// Optional real Standard-JSON dataset (argv[3]).  cuJSON's Fig.9 harness
	// parses real *_large_record.json files, so simdjson must see the SAME bytes.
	const char* json_file = argc > 3 ? argv[3] : nullptr;
	std::string json;
	if (json_file) {
		std::ifstream f(json_file, std::ios::binary);
		if (!f) {
			std::fprintf(stderr, "cannot open json file: %s\n", json_file);
			return 1;
		}
		json.assign(
			(std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>()
		);
	} else {
		const std::string rec =
			"{\"id\":123,\"name\":\"x \\\"y\\\"\",\"a\":[1,2,3],\"n\":{\"z\":null}}";
		json.push_back('[');
		while (json.size() < target) {
			if (json.size() > 1)
				json.push_back(',');
			json += rec;
		}
		json.push_back(']');
	}
	const std::size_t json_mb = (json.size() + (1u << 20) - 1) >> 20;
	std::string toml;
	{
		std::size_t i = 0;
		while (toml.size() < target)
			toml += "[t" + std::to_string(i++)
				  + "]\nhost = \"localhost\"\nport = 8080\nname = 'a # b'\n"
					"# comment { } , =\narr = [1, 2, 3]\n";
	}
	std::string xml;
	{
		xml = "<root>";
		while (xml.size() < target)
			xml +=
				"<item id=\"1\" name=\"x &amp; y\"><name>alpha</name><vals><v>1</v><v>2</v></vals></item>";
		xml += "</root>";
	}
	std::string ini = rep("[section]\nkey = value\nnum = 123\n; comment\nother = x\n", target);

	std::printf(
		"# format,impl,variant,scale_mb,GBps_mean,GBps_p50,GBps_p95,GBps_min,GBps_max,tokens,bytes_moved\n"
	);

	{
		// Match cuJSON's own simdjson baseline (ondemand quickstart `iterate`),
		// but on a properly padded buffer: ondemand reads SIMDJSON_PADDING bytes
		// past the end, so passing a bare std::string is undefined behaviour.
		simdjson::padded_string ps(json.data(), json.size());
		simdjson::ondemand::parser p;
		emit("json", "simdjson", json_mb, sample(json.size(), iters, [&] {
				 auto d = p.iterate(ps);
				 for (auto v : d) (void)v;
			 }));
	}
	emit("toml", "toml++", mb, sample(toml.size(), iters, [&] {
			 auto r = toml::parse(toml);
			 (void)r;
		 }));
	emit("xml", "pugixml", mb, sample(xml.size(), iters, [&] {
			 pugi::xml_document doc;
			 doc.load_buffer(xml.data(), xml.size());
		 }));
	emit("ini", "inih", mb, sample(ini.size(), iters, [&] {
			 ini_parse_string(ini.c_str(), ini_cb, nullptr);
		 }));
	return 0;
}
