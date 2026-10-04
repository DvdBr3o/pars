// bench/bench_pointquery.cu — single-point query latency, pars vs SOTA.
//
// The document is parsed once (excluded from the timer); we then measure the
// cost of *one point lookup* (find a key and decode its value) on the resulting
// structure: pars navigates its structural index; each baseline looks up in its
// own DOM / map.  This mirrors cuJSON's querying-cost measurement (Fig. 15).
//
// All formats use the SAME payload for pars and the baseline, and the payload
// is valid for the baseline parser.
//
// Output: format,impl,scale_mb,query_ns,ok
#include "pars/pars.hpp"
#include "pars/backend/cuda/pipeline.cuh"

#include "grammars/json.hpp"
#include "grammars/toml.hpp"
#include "grammars/ini.hpp"
#include "grammars/xml.hpp"

#include <simdjson.h>
#include "toml.hpp"
#include "pugixml.hpp"
#include "ini.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

using namespace std::chrono;
using namespace pars;

namespace {

volatile std::uint64_t g_sink = 0;	// defeats hoisting/dead-code elimination

bool				   is_ws(std::uint8_t c) {
	return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

std::string_view trim(std::string_view s) {
	while (!s.empty() && is_ws((std::uint8_t)s.front())) s.remove_prefix(1);
	while (!s.empty() && is_ws((std::uint8_t)s.back())) s.remove_suffix(1);
	return s;
}

// Last whitespace-separated (quote-aware) token of a gap.
std::string_view last_token(std::string_view s) {
	std::size_t end = s.size();
	while (end > 0 && is_ws((std::uint8_t)s[end - 1])) --end;
	if (end == 0)
		return {};
	std::size_t start = end;
	// walk back, respecting a closing quote
	if (s[end - 1] == '"' || s[end - 1] == '\'') {
		const char	q = s[end - 1];
		std::size_t i = end - 1;
		while (i > 0 && s[i - 1] != q) --i;
		if (i > 0)
			--i;
		start = i;
	} else {
		while (start > 0 && !is_ws((std::uint8_t)s[start - 1])) --start;
	}
	return s.substr(start, end - start);
}

// First whitespace-separated (quote-aware) token of a gap.
std::string_view first_token(std::string_view s) {
	s = trim(s);
	if (s.empty())
		return {};
	if (s.front() == '"' || s.front() == '\'') {
		const char	q = s.front();
		std::size_t i = 1;
		while (i < s.size() && s[i] != q) {
			if (s[i] == '\\' && i + 1 < s.size())
				++i;
			++i;
		}
		if (i < s.size())
			++i;
		return s.substr(0, i);
	}
	std::size_t i = 0;
	while (i < s.size() && !is_ws((std::uint8_t)s[i])) ++i;
	return s.substr(0, i);
}

std::string_view unquote(std::string_view s) {
	s = trim(s);
	if (s.size() >= 2 && (s.front() == '"' || s.front() == '\'') && s.back() == s.front())
		return s.substr(1, s.size() - 2);
	return s;
}

// pars point query (navigation only): first `<key><sep>value` in the index.
std::string_view pars_nav(
	const model::structural_cursor& c, char sep, std::string_view key, bool& ok
) {
	for (std::size_t i = 1; i < c.n; ++i) {
		if (c.ch(i) != sep)
			continue;
		if (unquote(last_token(c.gap(i))) == key) {
			ok = true;
			return unquote(first_token(c.gap(i + 1)));
		}
	}
	return {};
}

template<class F>
double time_queries(int reps, F&& f) {
	// warmup
	for (int i = 0; i < 3; ++i) f();
	auto a = steady_clock::now();
	for (int i = 0; i < reps; ++i) f();
	auto b = steady_clock::now();
	return duration<double, std::nano>(b - a).count() / reps;
}

void emit_row(const char* fmt, const char* impl, std::size_t mb, double ns, bool ok) {
	std::printf("%s,%s,%zu,%.1f,%d\n", fmt, impl, mb, ns, (int)ok);
	std::fflush(stdout);
}

std::string repeat(const std::string& rec, std::size_t target) {
	std::string out;
	out.reserve(target);
	while (out.size() < target) out += rec;
	return out;
}

int ini_count_cb(void* user, const char* section, const char* name, const char* value) {
	auto* m = static_cast<std::unordered_map<std::string, std::string>*>(user);
	m->emplace(std::string(section) + "." + name, value);
	return 1;
}

}  // namespace

int main(int argc, char** argv) {
	const std::size_t mb	 = argc > 1 ? (std::size_t)std::atol(argv[1]) : 64;
	const int		  reps	 = argc > 2 ? std::atoi(argv[2]) : 2000;
	const std::size_t target = mb * 1024 * 1024;

	constexpr auto	  kJ	 = compile::make_scan_plan<bench::json_grammar>();
	constexpr auto	  kT	 = compile::make_scan_plan<bench::toml_grammar>();
	constexpr auto	  kI	 = compile::make_scan_plan<bench::ini_grammar>();
	constexpr auto	  kX	 = compile::make_scan_plan<bench::xml_grammar>();

	// ---------------- JSON: first record's "id" ----------------
	{
		const std::string rec =
			"{\"id\":123,\"name\":\"x \\\"y\\\"\",\"a\":[1,2,3],\"n\":{\"z\":null}}";
		std::string doc;
		doc.push_back('[');
		while (doc.size() < target) {
			if (doc.size() > 1)
				doc.push_back(',');
			doc += rec;
		}
		doc.push_back(']');

		gpu::scanner<kJ>		 g;
		const std::uint8_t*		 base = reinterpret_cast<const std::uint8_t*>(doc.data());
		auto					 hr	  = g.scan_host(base, doc.size(), 0, false);
		model::structural_cursor c {hr.structural.data(), nullptr, base, hr.structural.size()};
		bool					 ok = false;
		std::string_view		 v	= pars_nav(c, ':', "id", ok);
		std::fprintf(
			stderr,
			"[pointquery] json pars id=%s ok=%d\n",
			std::string(v).c_str(),
			(int)ok
		);
		emit_row(
			"json",
			"pars",
			mb,
			time_queries(
				reps,
				[&] {
					bool o;
					g_sink += (std::uint64_t)pars_nav(c, ':', "id", o).size() + o;
				}
			),
			ok
		);

		simdjson::dom::parser  p;
		simdjson::dom::element root;
		if (!p.parse(doc).get(root)) {
			simdjson::dom::array   arr	 = root.get_array();
			simdjson::dom::element first = arr.at(0);
			int64_t				   idv	 = 0;
			(void)first["id"].get(idv);
			emit_row(
				"json",
				"simdjson",
				mb,
				time_queries(
					reps,
					[&] {
						int64_t x = 0;
						(void)arr.at(0)["id"].get(x);
						g_sink += (std::uint64_t)x;
					}
				),
				idv == 123
			);
		}
	}

	// ---------------- INI: first "[section]"'s "num" ----------------
	{
		std::string doc =
			repeat("[section]\nkey = value\nnum = 123\n; comment\nother = x\n", target);
		gpu::scanner<kI>		 g;
		const std::uint8_t*		 base = reinterpret_cast<const std::uint8_t*>(doc.data());
		auto					 hr	  = g.scan_host(base, doc.size(), 0, false);
		model::structural_cursor c {hr.structural.data(), nullptr, base, hr.structural.size()};
		bool					 ok = false;
		std::string_view		 v	= pars_nav(c, '=', "num", ok);
		std::fprintf(
			stderr,
			"[pointquery] ini pars num=%s ok=%d\n",
			std::string(v).c_str(),
			(int)ok
		);
		emit_row(
			"ini",
			"pars",
			mb,
			time_queries(
				reps,
				[&] {
					bool o;
					g_sink += (std::uint64_t)pars_nav(c, '=', "num", o).size() + o;
				}
			),
			ok
		);

		std::unordered_map<std::string, std::string> m;
		ini_parse_string(doc.c_str(), ini_count_cb, &m);
		auto it = m.find("section.num");
		emit_row(
			"ini",
			"inih",
			mb,
			time_queries(reps, [&] { g_sink += (m.find("section.num") != m.end()); }),
			it != m.end()
		);
	}

	// ---------------- TOML: first "[t0]"'s "port" ----------------
	{
		std::string doc;
		for (std::size_t i = 0; doc.size() < target; ++i)
			doc += "[t" + std::to_string(i)
				 + "]\nhost = \"localhost\"\nport = 8080\n"
				   "name = 'a # b'\narr = [1, 2, 3]\n";
		gpu::scanner<kT>		 g;
		const std::uint8_t*		 base = reinterpret_cast<const std::uint8_t*>(doc.data());
		auto					 hr	  = g.scan_host(base, doc.size(), 0, false);
		model::structural_cursor c {hr.structural.data(), nullptr, base, hr.structural.size()};
		bool					 ok = false;
		std::string_view		 v	= pars_nav(c, '=', "port", ok);
		std::fprintf(
			stderr,
			"[pointquery] toml pars port=%s ok=%d\n",
			std::string(v).c_str(),
			(int)ok
		);
		emit_row(
			"toml",
			"pars",
			mb,
			time_queries(
				reps,
				[&] {
					bool o;
					g_sink += (std::uint64_t)pars_nav(c, '=', "port", o).size() + o;
				}
			),
			ok
		);

		auto	tbl	 = toml::parse(doc);
		int64_t port = 0;
		if (auto node = tbl["t0"]["port"].value<int64_t>())
			port = *node;
		emit_row(
			"toml",
			"toml++",
			mb,
			time_queries(
				reps,
				[&] { g_sink += (std::uint64_t)tbl["t0"]["port"].value<int64_t>().value_or(0); }
			),
			port == 8080
		);
	}

	// ---------------- XML: first <item>'s "id" attribute ----------------
	{
		const std::string item = "<item id=\"1\" name=\"x &amp; y\"><name>alpha</name></item>";
		std::string		  doc  = "<root>";
		while (doc.size() < target) doc += item;
		doc += "</root>";
		gpu::scanner<kX>		 g;
		const std::uint8_t*		 base = reinterpret_cast<const std::uint8_t*>(doc.data());
		auto					 hr	  = g.scan_host(base, doc.size(), 0, false);
		model::structural_cursor c {hr.structural.data(), nullptr, base, hr.structural.size()};
		bool					 ok = false;
		std::string_view		 v	= pars_nav(c, '=', "id", ok);
		std::fprintf(
			stderr,
			"[pointquery] xml pars id=%s ok=%d\n",
			std::string(v).c_str(),
			(int)ok
		);
		emit_row(
			"xml",
			"pars",
			mb,
			time_queries(
				reps,
				[&] {
					bool o;
					g_sink += (std::uint64_t)pars_nav(c, '=', "id", o).size() + o;
				}
			),
			ok
		);

		pugi::xml_document pdoc;
		pdoc.load_buffer(doc.data(), doc.size());
		auto item_node = pdoc.child("root").child("item");
		emit_row(
			"xml",
			"pugixml",
			mb,
			time_queries(
				reps,
				[&] { g_sink += (std::uint64_t)item_node.attribute("id").value()[0]; }
			),
			!item_node.attribute("id").empty()
		);
	}
	return 0;
}
