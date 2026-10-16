// bench/bench_variants.cu — the two consumer variants for every grammar.
//
//   variant = ondemand : pars stage-1 index + a *lazy* iterator (walks the
//                        structural index, decodes only selected fields; no DOM)
//   variant = dom      : pars stage-1 index + a *DOM builder* (materialises a
//                        node tape over the whole index)
//
// Baselines (host TU) matched per language:
//   json -> simdjson ondemand / dom        toml -> toml++ (dom)
//   xml  -> pugixml (dom)                  ini  -> inih (dom)
//   csv / clike -> no in-process baseline (simdcsv / flex are separate binaries)
//
// Output: format,impl,variant,scale_mb,GBps,tokens
#include "pars/parse.cuh"
#include "pars/pars.hpp"

#include "grammars/clike.hpp"
#include "grammars/csv.hpp"
#include "grammars/ini.hpp"
#include "grammars/json.hpp"
#include "grammars/toml.hpp"
#include "grammars/xml.hpp"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace std::chrono;
using namespace pars;

extern "C" {
void bv_simdjson_ondemand(const char* data, std::size_t n, int iters, std::size_t mb);
void bv_simdjson_dom(const char* data, std::size_t n, int iters, std::size_t mb);
void bv_toml_dom(const char* data, std::size_t n, int iters, std::size_t mb);
void bv_xml_dom(const char* data, std::size_t n, int iters, std::size_t mb);
void bv_ini_dom(const char* data, std::size_t n, int iters, std::size_t mb);
}

namespace {

volatile std::uint64_t g_sink = 0;

bool ws(std::uint8_t c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

// ondemand: a minimal lazy consumer — stream the index once, touching each
// structural token (no DOM allocation), like `for (auto v : doc)`.
std::uint64_t lazy_consume(const char* data, std::size_t n, const parse_result& r) {
	const auto*		  base = reinterpret_cast<const std::uint8_t*>(data);
	const auto*		  pos  = r.structural.data();
	const std::size_t N	   = r.structural.size();
	std::uint64_t	  h	   = 1469598103934665603ull;
	for (std::size_t i = 0; i < N; ++i) {
		h ^= base[pos[i]];
		h *= 1099511628211ull;
	}
	(void)n;
	return h;
}

// dom: replay the index and materialise a node per structural token and per
// non-structural value run.
struct dom_sink {
	const std::uint8_t* base  = nullptr;
	std::uint64_t		nodes = 0;
	std::uint64_t		h	  = 1469598103934665603ull;
	void mix(std::uint8_t b) {
		h ^= b;
		h *= 1099511628211ull;
	}
	void scalar(std::size_t b, std::size_t e) {
		std::size_t i = b;
		while (i < e && ws(base[i])) ++i;
		if (i >= e)
			return;
		++nodes;
		for (; i < e; ++i) mix(base[i]);
	}
	void symbol(std::size_t p, char c, std::int32_t) {
		++nodes;
		mix(static_cast<std::uint8_t>(c));
	}
};

template<class F>
double sample_mean(std::size_t n, int iters, F&& f) {
	f();
	f();
	double s = 0;
	for (int i = 0; i < iters; ++i) {
		auto a = steady_clock::now();
		f();
		auto b = steady_clock::now();
		s += duration<double>(b - a).count();
	}
	return static_cast<double>(n) / (s / iters) / 1e9;
}

void emit(const char* fmt, const char* impl, const char* variant, std::size_t mb, double g,
		  std::size_t tokens) {
	std::printf("%s,%s,%s,%zu,%.3f,%zu\n", fmt, impl, variant, mb, g, tokens);
	std::fflush(stdout);
}

template<class Grammar>
void run_lang(const char* fmt, const std::string& payload, std::size_t mb, int iters) {
	const std::size_t n	   = payload.size();
	const auto*		  base = reinterpret_cast<const std::uint8_t*>(payload.data());
	std::uint64_t	  tok  = 0;
	const double	  g_od = sample_mean(n, iters, [&] {
		 auto r = parser<Grammar> {}(payload);
		 tok	= r.structural.size();
		 g_sink = g_sink + lazy_consume(payload.data(), n, r);
	 });
	emit(fmt, "pars", "ondemand", mb, g_od, tok);

	std::size_t	 nodes = 0;
	const double g_dom = sample_mean(n, iters, [&] {
		auto r = parser<Grammar> {}(payload);
		dom_sink s;
		s.base = base;
		r.visit(payload, s);
		nodes  = s.nodes;
		g_sink = g_sink + s.h;
	});
	emit(fmt, "pars", "dom", mb, g_dom, nodes);
}

std::string repeat_rec(const std::string& rec, std::size_t target) {
	std::string out;
	out.reserve(target);
	while (out.size() < target) out += rec;
	return out;
}

}  // namespace

int main(int argc, char** argv) {
	const std::size_t mb	 = argc > 1 ? (std::size_t)std::atol(argv[1]) : 64;
	const int		  iters	 = argc > 2 ? std::atoi(argv[2]) : 5;
	const std::size_t target = mb * 1024 * 1024;

	std::printf("# format,impl,variant,scale_mb,GBps,tokens\n");

	// --- json ---
	std::string json;
	{
		const std::string rec =
			"{\"id\":123,\"name\":\"x \\\"y\\\"\",\"a\":[1,2,3],\"n\":{\"z\":null}}";
		json.push_back('[');
		while (json.size() < target) {
			if (json.size() > 1) json.push_back(',');
			json += rec;
		}
		json.push_back(']');
	}
	run_lang<bench::json_grammar>("json", json, mb, iters);
	bv_simdjson_ondemand(json.data(), json.size(), iters, mb);
	bv_simdjson_dom(json.data(), json.size(), iters, mb);

	// --- csv ---
	std::string csv =
		repeat_rec("alpha,beta,\"quoted, field\",\"has \"\"q\"\"\",42,true\n", target);
	run_lang<bench::csv_grammar>("csv", csv, mb, iters);

	// --- toml ---
	std::string toml;
	{
		std::size_t i = 0;
		while (toml.size() < target)
			toml += "[t" + std::to_string(i++)
				  + "]\nhost = \"localhost\"\nport = 8080\nname = 'a # b'\n"
					"# comment { } , = with \"quotes\"\narr = [1, 2, 3]\n";
	}
	run_lang<bench::toml_grammar>("toml", toml, mb, iters);
	bv_toml_dom(toml.data(), toml.size(), iters, mb);

	// --- ini ---
	std::string ini = repeat_rec("[section]\nkey = value\nnum = 123\n; comment\nother = x\n", target);
	run_lang<bench::ini_grammar>("ini", ini, mb, iters);
	bv_ini_dom(ini.data(), ini.size(), iters, mb);

	// --- xml ---
	std::string xml = repeat_rec("<item id=\"1\" name=\"x &amp; y\">alpha</item>", target);
	run_lang<bench::xml_grammar>("xml", xml, mb, iters);
	bv_xml_dom(xml.data(), xml.size(), iters, mb);

	// --- clike ---
	std::string clike = repeat_rec("int x; /* c { ; } */ y = 1; // { ; }\n", target);
	run_lang<bench::clike_grammar>("clike", clike, mb, iters);

	std::fprintf(stderr, "[variants] sink=%llu\n", (unsigned long long)g_sink);
	return 0;
}
