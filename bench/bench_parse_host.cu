// bench/bench_parse_host.cu — pars-side *full parse* for TOML/XML/INI.
//
// These formats have no like-for-like "structural-only" SOTA (toml++/pugixml/
// inih all build a DOM / key-value map), so to compare fairly we run pars the
// same way: R1-R6 on the GPU, then a user-defined host pass (M+D) that decodes
// values and builds a map/tree.  Everything format specific lives here; the
// framework only provides the structural index + the replay hook.
#include "pars/pars.hpp"
#include "pars/backend/cuda/pipeline.cuh"

#include "grammars/toml.hpp"
#include "grammars/ini.hpp"
#include "grammars/xml.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

using namespace std::chrono;
using namespace pars;

namespace {

bool is_ws(std::uint8_t c) {
	return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

// Split a non-structural gap into tokens, keeping a quoted span as one token.
template<class F>
void each_token(const std::uint8_t* base, std::size_t b, std::size_t e, F&& f) {
	std::size_t i = b;
	while (i < e) {
		while (i < e && is_ws(base[i])) ++i;
		if (i >= e)
			break;
		const std::size_t s = i;
		if (base[i] == '"' || base[i] == '\'') {
			const std::uint8_t q = base[i++];
			while (i < e && base[i] != q) {
				if (base[i] == '\\' && i + 1 < e)
					++i;
				++i;
			}
			if (i < e)
				++i;  // closing quote
		} else {
			while (i < e && !is_ws(base[i])) ++i;
		}
		f(std::string_view(reinterpret_cast<const char*>(base) + s, i - s));
	}
}

std::uint64_t fnv(std::uint64_t h, std::string_view s) {
	for (unsigned char c : s) {
		h ^= c;
		h *= 1099511628211ull;
	}
	return h;
}

// ---------------------------------------------------------------------------
// INI: build a section -> (key,value) map (the inih output shape).
// ---------------------------------------------------------------------------
struct ini_parse {
	const std::uint8_t*								 base = nullptr;
	std::vector<std::string>						 sections;
	std::vector<std::pair<std::size_t, std::size_t>> keys, vals;  // offsets into arena
	std::string										 arena;
	std::size_t										 cur_sec	= 0;
	int												 expect		= 0;  // 0 key, 1 section, 2 value
	bool											 in_comment = false;
	std::uint64_t									 hash		= 1469598103934665603ull;

	void											 add(std::string_view s, bool value) {
		const std::size_t off = arena.size();
		arena.append(s);
		(value ? vals : keys).emplace_back(off, s.size());
		hash = fnv(hash, s);
	}

	void scalar(std::size_t b, std::size_t e) {
		if (in_comment)
			return;
		each_token(base, b, e, [&](std::string_view tok) {
			if (expect == 1) {
				cur_sec = sections.size();
				sections.emplace_back(tok);
				hash   = fnv(hash, tok);
				expect = 0;
			} else if (expect == 2) {
				add(tok, true);
				expect = 0;
			} else {
				add(tok, false);
			}
		});
	}

	void symbol(std::size_t, char c, std::int32_t) {
		if (c == '\n') {
			in_comment = false;
			expect	   = 0;
		} else if (c == ';' || c == '#')
			in_comment = true;
		else if (in_comment)
			return;
		else if (c == '[')
			expect = 1;
		else if (c == ']')
			expect = 0;
		else if (c == '=')
			expect = 2;
	}
};

// ---------------------------------------------------------------------------
// XML: element/attribute/text counts with a tag stack (pugixml-like DOM).
// ---------------------------------------------------------------------------
struct xml_parse {
	const std::uint8_t*			  base = nullptr;
	std::vector<std::string_view> stack;
	std::size_t					  elements = 0, attrs = 0, text_bytes = 0, max_depth = 0;
	bool						  in_tag = false, closing = false, expect_attrval = false;
	bool						  prolog = false, expect_name = false;
	std::string_view			  tag {};
	std::uint64_t				  hash = 1469598103934665603ull;

	void						  scalar(std::size_t b, std::size_t e) {
		if (prolog)
			return;
		each_token(base, b, e, [&](std::string_view tok) {
			if (in_tag) {
				if (expect_name) {
					tag			= tok;
					expect_name = false;
					hash		= fnv(hash, tok);
				} else if (expect_attrval) {
					++attrs;
					expect_attrval = false;
					hash		   = fnv(hash, tok);
				}
				// else: attribute name, ignore
			} else {
				text_bytes += tok.size();
			}
		});
	}

	void symbol(std::size_t, char c, std::int32_t) {
		if (prolog) {
			if (c == '>')
				prolog = false;
			return;
		}
		switch (c) {
			case '<':
				in_tag		= true;
				closing		= false;
				expect_name = true;
				tag			= {};
				break;
			case '/':
				if (in_tag && expect_name)
					closing = true;
				break;
			case '=':
				if (in_tag)
					expect_attrval = true;
				break;
			case '>':
				if (in_tag) {
					if (closing) {
						if (!stack.empty() && stack.back() == tag)
							stack.pop_back();
					} else {
						stack.push_back(tag);
						++elements;
						max_depth = std::max(max_depth, stack.size());
					}
					in_tag = false;
				}
				break;
			case '?':
			case '!':
				prolog = true;
				in_tag = false;
				break;
			default: break;
		}
	}
};

// ---------------------------------------------------------------------------
// TOML: sections and key = value pairs (toml::table-like map).
// ---------------------------------------------------------------------------
struct toml_parse {
	const std::uint8_t*										   base = nullptr;
	std::vector<std::string_view>							   sections;
	std::vector<std::pair<std::string_view, std::string_view>> entries;
	std::string_view										   key {};
	std::size_t												   elems = 0;
	int			  mode		 = 0;  // 0 none, 1 section, 2 value, 3 array
	bool		  in_comment = false;
	std::uint64_t hash		 = 1469598103934665603ull;

	void		  scalar(std::size_t b, std::size_t e) {
		if (in_comment)
			return;
		each_token(base, b, e, [&](std::string_view tok) {
			if (mode == 1) {
				sections.push_back(tok);
				hash = fnv(hash, tok);
				mode = 0;
			} else if (mode == 2) {
				entries.emplace_back(key, tok);
				hash = fnv(fnv(hash, key), tok);
				mode = 0;
			} else if (mode == 3) {
				++elems;
				hash = fnv(hash, tok);
			} else {
				key = tok;
			}  // mode 0: next key (a value consumes exactly one token)
		});
	}

	void symbol(std::size_t, char c, std::int32_t) {
		if (c == '\n') {
			in_comment = false;
			mode	   = 0;
		} else if (c == '#')
			in_comment = true;
		else if (in_comment)
			return;
		else if (c == '[')
			mode = (mode == 2) ? 3 : 1;	 // '[' at value position = array
		else if (c == ']')
			mode = 0;
		else if (c == '=')
			mode = 2;
	}
};

double gbps(std::size_t bytes, double s) {
	return static_cast<double>(bytes) / s / 1e9;
}

template<class F>
std::vector<double> sample(std::size_t bytes, int iters, F&& f) {
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

void emit(
	const char* fmt, std::size_t mb, std::vector<double> g, std::size_t tokens, std::size_t moved
) {
	std::sort(g.begin(), g.end());
	double sum = 0;
	for (double v : g) sum += v;
	auto q = [&](double p) { return g[std::min(g.size() - 1, (std::size_t)(p * (g.size() - 1)))]; };
	std::printf(
		"%s,pars,P3,%zu,%.3f,%.3f,%.3f,%.3f,%.3f,%zu,%zu\n",
		fmt,
		mb,
		sum / g.size(),
		q(0.50),
		q(0.95),
		g.front(),
		g.back(),
		tokens,
		moved
	);
	std::fflush(stdout);
}

static std::size_t count_sub(const std::string& s, std::string_view sub) {
	std::size_t n = 0, p = 0;
	while ((p = s.find(sub, p)) != std::string::npos) {
		++n;
		p += sub.size();
	}
	return n;
}

static std::string repeat_rec(const std::string& rec, std::size_t target) {
	std::string out;
	out.reserve(target);
	while (out.size() < target) out += rec;
	return out;
}

}  // namespace

int main(int argc, char** argv) {
	const std::size_t mb	 = argc > 1 ? (std::size_t)std::atol(argv[1]) : 64;
	const int		  iters	 = argc > 2 ? std::atoi(argv[2]) : 10;
	const std::size_t target = mb * 1024 * 1024;

	const std::string toml	 = repeat_rec(
		"[server]\nhost = \"localhost\"\nport = 8080\nname = 'a # b'\n"
		"# comment { } , = with \"quotes\"\narr = [1, 2, 3]\n",
		target
	);
	const std::string ini =
		repeat_rec("[section]\nkey = value\nnum = 123\n; comment\nother = x\n", target);
	const std::string xml	 = repeat_rec("<item id=\"1\" name=\"x &amp; y\">alpha</item>", target);

	constexpr auto	  kT	 = compile::make_scan_plan<bench::toml_grammar>();
	constexpr auto	  kI	 = compile::make_scan_plan<bench::ini_grammar>();
	constexpr auto	  kX	 = compile::make_scan_plan<bench::xml_grammar>();

	const std::size_t recs_t = count_sub(toml, "[server]");
	const std::size_t recs_i = count_sub(ini, "[section]");
	const std::size_t recs_x = count_sub(xml, "<item ");

	toml_parse		  tp;
	(void)tp;
	{
		gpu::scanner<kT>	   g;
		const std::uint8_t*	   base = reinterpret_cast<const std::uint8_t*>(toml.data());
		auto				   hr	= g.scan_host(base, toml.size(), 0, false);
		model::structural_view sv {hr.structural.data(), nullptr, hr.structural.size()};
		toml_parse			   p0 {};
		p0.base = base;
		model::replay(base, toml.size(), sv, p0);
		std::fprintf(
			stderr,
			"[parse-host] toml entries=%zu (want %zu) sections=%zu\n",
			p0.entries.size(),
			recs_t * 3,
			p0.sections.size()
		);
		emit(
			"toml",
			mb,
			sample(
				toml.size(),
				iters,
				[&] {
					toml_parse p {};
					p.base = base;
					model::replay(base, toml.size(), sv, p);
				}
			),
			sv.n,
			toml.size()
		);
	}
	{
		gpu::scanner<kI>	   g;
		const std::uint8_t*	   base = reinterpret_cast<const std::uint8_t*>(ini.data());
		auto				   hr	= g.scan_host(base, ini.size(), 0, false);
		model::structural_view sv {hr.structural.data(), nullptr, hr.structural.size()};
		ini_parse			   p0 {};
		p0.base = base;
		model::replay(base, ini.size(), sv, p0);
		std::fprintf(
			stderr,
			"[parse-host] ini entries=%zu (want %zu) sections=%zu\n",
			p0.vals.size(),
			recs_i * 3,
			p0.sections.size()
		);
		emit(
			"ini",
			mb,
			sample(
				ini.size(),
				iters,
				[&] {
					ini_parse p {};
					p.base = base;
					model::replay(base, ini.size(), sv, p);
				}
			),
			sv.n,
			ini.size()
		);
	}
	{
		gpu::scanner<kX>	   g;
		const std::uint8_t*	   base = reinterpret_cast<const std::uint8_t*>(xml.data());
		auto				   hr	= g.scan_host(base, xml.size(), 0, false);
		model::structural_view sv {hr.structural.data(), nullptr, hr.structural.size()};
		xml_parse			   p0 {};
		p0.base = base;
		model::replay(base, xml.size(), sv, p0);
		std::fprintf(
			stderr,
			"[parse-host] xml elements=%zu (want %zu) attrs=%zu\n",
			p0.elements,
			recs_x,
			p0.attrs
		);
		emit(
			"xml",
			mb,
			sample(
				xml.size(),
				iters,
				[&] {
					xml_parse p {};
					p.base = base;
					model::replay(base, xml.size(), sv, p);
				}
			),
			sv.n,
			xml.size()
		);
	}
	return 0;
}
