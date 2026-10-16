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
#include "pars/scan.hpp"

#include "grammars/clike.hpp"
#include "grammars/csv.hpp"
#include "grammars/ini.hpp"
#include "grammars/json.hpp"
#include "grammars/toml.hpp"
#include "grammars/xml.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
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

std::string read_file(const std::string& path) {
	std::ifstream f(path, std::ios::binary);
	if (!f)
		return {};
	return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

// ---------------------------------------------------------------------------
// Varied, valid payload generators (no single repeated record): the structure
// stays valid but every record differs, so the input is not mechanically
// periodic.  (JSON uses the real corpus instead.)
// ---------------------------------------------------------------------------
std::string gen_csv(std::size_t target) {
	static const char* rows[] = {
		"alpha,beta,\"quoted, field\",\"has \"\"q\"\"\",42,true\n",
		"x,y,z,1,2,3\n",
		"\"a,b\",\"c\"\"d\",e,100,false\n",
		"long_field_name,another,third,4.5,-6,true\n",
		"1,2,3,4,5,6,7,8\n",
		"city,pop,lat,lon\n",
	};
	std::string out;
	out.reserve(target + 256);
	std::uint32_t st = 0x9e3779b9u;
	while (out.size() < target) {
		st = st * 1664525u + 1013904223u;
		out += rows[st % (sizeof(rows) / sizeof(rows[0]))];
	}
	return out;
}

std::string gen_toml(std::size_t target) {
	std::string out;
	out.reserve(target + 256);
	for (std::size_t k = 0; out.size() < target; ++k)
		out += "[t" + std::to_string(k) + "]\nhost = \"h" + std::to_string(k % 97)
			   + "\"\nport = " + std::to_string(1000 + (k * 37) % 9000)
			   + "\nname = 'n" + std::to_string(k % 13)
			   + "'\n# comment { } , = with \"quotes\"\narr = [" + std::to_string(k % 5) + ", "
			   + std::to_string(k % 7) + ", " + std::to_string(k % 11) + "]\n";
	return out;
}

std::string gen_ini(std::size_t target) {
	std::string out;
	out.reserve(target + 256);
	for (std::size_t k = 0; out.size() < target; ++k)
		out += "[section" + std::to_string(k) + "]\nkey = value" + std::to_string(k)
			   + "\nnum = " + std::to_string(k * 3) + "\n; comment\nother = x" + std::to_string(k % 9)
			   + "\n";
	return out;
}

std::string gen_xml(std::size_t target) {
	std::string out;
	out.reserve(target + 256);
	for (std::size_t k = 0; out.size() < target; ++k)
		out += "<item id=\"" + std::to_string(k) + "\" name=\"x &amp; y" + std::to_string(k % 11)
			   + "\"><name>alpha" + std::to_string(k % 7) + "</name></item>";
	return out;
}

std::string gen_clike(std::size_t target) {
	std::string out;
	out.reserve(target + 256);
	for (std::size_t k = 0; out.size() < target; ++k)
		out += "int x" + std::to_string(k) + "; /* c { ; } */ y" + std::to_string(k % 13) + " = "
			   + std::to_string(k % 100) + "; // { ; }\n";
	return out;
}

}  // namespace

int main(int argc, char** argv) {
	const std::size_t mb	 = argc > 1 ? (std::size_t)std::atol(argv[1]) : 64;
	const int		  iters	 = argc > 2 ? std::atoi(argv[2]) : 5;
	const std::string pdir	 = argc > 3 ? argv[3] : "";
	const std::size_t target = mb * 1024 * 1024;

	std::printf("# format,impl,variant,scale_mb,GBps,tokens\n");

	// --- json: real corpus slice if a payload dir is given, else a generated
	//     array of records ---
	std::string json;
	if (!pdir.empty())
		json = read_file(pdir + "/json_" + std::to_string(mb) + ".json");
	if (json.empty()) {
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
	// pars index-only: H2D + GPU R1-R6, no D2H, no consumer -> exactly cuJSON's
	// Fig.9 interval (the apples-to-apples comparison).
	{
		parser<bench::json_grammar> P;
		const std::size_t			jn	   = json.size();
		const std::size_t			padded = (jn + 31) / 32 * 32;
		std::uint8_t*				dp	   = nullptr;
		cudaMalloc(&dp, padded);
		const double g = sample_mean(jn, iters, [&] {
			cudaMemcpyAsync(dp, json.data(), jn, cudaMemcpyHostToDevice, 0);
			(void)P.scan_device(dp, jn, 0, false, true);
		});
		emit("json", "pars", "index", mb, g, 0);
		cudaFree(dp);
	}
	// pars index, chunked overlap: H2D on stream s1 overlaps scan_chunk on s0
	// (no D2H).  This is the N2 tier with pipelining — the fix for the gap above.
	{
		parser<bench::json_grammar> P;
		const std::size_t			jn		= json.size();
		const std::size_t			nchunks = 16;
		const std::size_t			chunk	= ((jn + nchunks - 1) / nchunks + 31) / 32 * 32;
		std::uint8_t*				pin		= nullptr;
		cudaHostAlloc(&pin, jn, cudaHostAllocDefault);
		std::memcpy(pin, json.data(), jn);
		std::uint8_t* dev[2] = {nullptr, nullptr};
		cudaMalloc(&dev[0], chunk);
		cudaMalloc(&dev[1], chunk);
		P.reserve(jn);
		cudaStream_t s0, s1;
		cudaStreamCreate(&s0);
		cudaStreamCreate(&s1);
		cudaEvent_t ev[2];
		cudaEventCreate(&ev[0]);
		cudaEventCreate(&ev[1]);
		auto run = [&] {
			gpu::chunk_carry carry {};
			std::size_t		 off = 0;
			int				 i	 = 0;
			cudaMemcpyAsync(dev[0], pin, std::min(chunk, jn), cudaMemcpyHostToDevice, s1);
			cudaEventRecord(ev[0], s1);
			for (; off < jn; ++i) {
				const int		  cur = i & 1, nxt = (i + 1) & 1;
				const std::size_t len = std::min(chunk, jn - off);
				if (off + len < jn) {
					const std::size_t noff = off + len, nlen = std::min(chunk, jn - noff);
					if (i >= 1)
						cudaStreamWaitEvent(s1, ev[nxt], 0);
					cudaMemcpyAsync(dev[nxt], pin + noff, nlen, cudaMemcpyHostToDevice, s1);
					cudaEventRecord(ev[nxt], s1);
				}
				cudaStreamWaitEvent(s0, ev[cur], 0);
				carry = P.scan_chunk(dev[cur], len, carry, s0, off);
				off += len;
			}
			cudaStreamSynchronize(s0);
		};
		const double g = sample_mean(jn, iters, run);
		emit("json", "pars", "indexc", mb, g, 0);
		cudaFree(dev[0]);
		cudaFree(dev[1]);
		cudaFreeHost(pin);
		cudaStreamDestroy(s0);
		cudaStreamDestroy(s1);
		cudaEventDestroy(ev[0]);
		cudaEventDestroy(ev[1]);
	}
	// pars streaming host boundary via the unified API `pars::scan<Grammar>`
	// (chunked overlap, index stays on device, input pre-staged): the SOTA path.
	{
		std::uint8_t* pin = nullptr;
		cudaHostAlloc(&pin, json.size(), cudaHostAllocDefault);
		std::memcpy(pin, json.data(), json.size());
		std::string_view staged(reinterpret_cast<const char*>(pin), json.size());
		scan_options	 o;
		o.to_host = false;
		o.copy_in = false;
		const double g = sample_mean(json.size(), iters, [&] {
			auto r = scan<bench::json_grammar>(staged, o);
			g_sink = g_sink + r.tokens();
		});
		emit("json", "pars", "stream", mb, g, 0);
		cudaFreeHost(pin);
	}

	// --- csv ---
	std::string csv = gen_csv(target);
	run_lang<bench::csv_grammar>("csv", csv, mb, iters);

	// --- toml ---
	std::string toml = gen_toml(target);
	run_lang<bench::toml_grammar>("toml", toml, mb, iters);
	bv_toml_dom(toml.data(), toml.size(), iters, mb);

	// --- ini ---
	std::string ini = gen_ini(target);
	run_lang<bench::ini_grammar>("ini", ini, mb, iters);
	bv_ini_dom(ini.data(), ini.size(), iters, mb);

	// --- xml ---
	std::string xml = gen_xml(target);
	run_lang<bench::xml_grammar>("xml", xml, mb, iters);
	bv_xml_dom(xml.data(), xml.size(), iters, mb);

	// --- clike ---
	std::string clike = gen_clike(target);
	run_lang<bench::clike_grammar>("clike", clike, mb, iters);

	std::fprintf(stderr, "[variants] sink=%llu\n", (unsigned long long)g_sink);
	return 0;
}
