// bench/bench_parse.cpp — complete-parser workload built on the user-defined
// point (pars/model/visit.hpp).
//
// Stage-1 (the scan algebra) produces a structural index on the GPU; the user
// hook replays it as an event stream to a *user-supplied* JSON parser, which
// builds a real DOM tape and decodes every value.  Nothing format specific
// lives in include/: this file owns the JSON semantics.
//
// Output rows: format,impl,variant,scale_mb,GBps_mean,...,tokens,bytes_moved
#include "pars/pars.hpp"
#include "pars/backend/cuda/pipeline.cuh"

#include "grammars/json.hpp"

#include <simdjson.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>

using namespace std::chrono;
using namespace pars;

// ---------------------------------------------------------------------------
// The user-defined parser: a JSON DOM builder driven by the replay hook.
// ---------------------------------------------------------------------------
namespace {

enum jkind : std::uint8_t { J_OBJ, J_ARR, J_STR, J_NUM, J_TRUE, J_FALSE, J_NULL };

struct jnode {
	std::uint8_t  kind;
	std::uint32_t begin, end;
};

struct json_parser {
	const std::uint8_t*		 base;
	std::size_t				 n;
	std::vector<jnode>		 tape;
	std::vector<std::size_t> node_stack;  // tape index of each open container

	struct frame {
		char		 open;
		std::uint8_t phase;	 // object: 0 key/} 1 colon 2 value 3 comma/}
							 // array : 0 value/] 1 comma/]
	};

	std::vector<frame> stack;

	bool			   ok		= true;
	bool			   top_done = false;
	uint64_t		   hash		= 1469598103934665603ull;
	uint64_t		   nodes	= 0;

	static bool ws(std::uint8_t c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

	void		mix(std::uint8_t b) {
		hash ^= b;
		hash *= 1099511628211ull;
	}

	bool in_value() const {
		if (stack.empty())
			return !top_done;
		const frame& f = stack.back();
		return f.open == '{' ? f.phase == 2 : f.phase == 0;
	}

	void value_done() {
		if (stack.empty())
			top_done = true;
		else if (stack.back().open == '{')
			stack.back().phase = 3;
		else
			stack.back().phase = 1;
	}

	void push_node(std::uint8_t k, std::size_t b) {
		tape.push_back({k, (std::uint32_t)b, 0});
		++nodes;
	}

	void scalar(std::size_t b, std::size_t e) {
		if (!ok)
			return;
		std::size_t i = b;
		while (i < e && ws(base[i])) ++i;
		if (i >= e)
			return;	 // whitespace only
		const bool as_key = !stack.empty() && stack.back().open == '{' && stack.back().phase == 0;
		if (as_key) {
			if (base[i] != '"') {
				ok = false;
				return;
			}
			const std::size_t j = parse_string(i, e);
			if (j == NPOS) {
				ok = false;
				return;
			}
			std::size_t k = j;
			while (k < e && ws(base[k])) ++k;
			if (k != e) {
				ok = false;
				return;
			}
			stack.back().phase = 1;
		} else {
			if (!in_value()) {
				ok = false;
				return;
			}
			const std::size_t j = parse_value(i, e);
			if (j == NPOS) {
				ok = false;
				return;
			}
			std::size_t k = j;
			while (k < e && ws(base[k])) ++k;
			if (k != e) {
				ok = false;
				return;
			}
			value_done();
		}
	}

	void symbol(std::size_t p, char c, std::int32_t) {
		if (!ok)
			return;
		switch (c) {
			case '{':
			case '[':
				if (!in_value()) {
					ok = false;
					return;
				}
				push_node(c == '{' ? J_OBJ : J_ARR, p);
				node_stack.push_back(tape.size() - 1);
				stack.push_back({c, 0});
				break;
			case '}':
				if (stack.empty() || stack.back().open != '{'
					|| !(stack.back().phase == 0 || stack.back().phase == 3)) {
					ok = false;
					return;
				}
				tape[node_stack.back()].end = (std::uint32_t)p;
				node_stack.pop_back();
				stack.pop_back();
				value_done();
				break;
			case ']':
				if (stack.empty() || stack.back().open != '['
					|| !(stack.back().phase == 0 || stack.back().phase == 1)) {
					ok = false;
					return;
				}
				tape[node_stack.back()].end = (std::uint32_t)p;
				node_stack.pop_back();
				stack.pop_back();
				value_done();
				break;
			case ':':
				if (stack.empty() || stack.back().open != '{' || stack.back().phase != 1) {
					ok = false;
					return;
				}
				stack.back().phase = 2;
				break;
			case ',':
				if (stack.empty()) {
					ok = false;
					return;
				}
				if (stack.back().open == '{') {
					if (stack.back().phase != 3) {
						ok = false;
						return;
					}
				} else if (stack.back().phase != 1) {
					ok = false;
					return;
				}
				stack.back().phase = 0;
				break;
			default: ok = false; break;
		}
	}

	void finish() {
		if (!stack.empty())
			ok = false;
		if (!top_done)
			ok = false;
	}

	void reset() {
		tape.clear();
		stack.clear();
		node_stack.clear();
		ok		 = true;
		top_done = false;
		hash	 = 1469598103934665603ull;
		nodes	 = 0;
	}

	static constexpr std::size_t NPOS = static_cast<std::size_t>(-1);

	std::size_t					 parse_value(std::size_t i, std::size_t e) {
		const std::uint8_t c = base[i];
		if (c == '"')
			return parse_string(i, e);
		if (c == '-' || (c >= '0' && c <= '9'))
			return parse_number(i, e);
		if (c == 't' || c == 'f' || c == 'n')
			return parse_lit(i, e);
		return NPOS;
	}

	std::size_t parse_string(std::size_t i, std::size_t e) {
		push_node(J_STR, i);
		mix('S');
		std::size_t j = i + 1;
		while (j < e) {
			std::uint8_t c = base[j++];
			if (c == '"') {
				tape.back().end = (std::uint32_t)j;
				mix(0);
				return j;
			}
			if (c == '\\') {
				if (j >= e)
					return NPOS;
				const std::uint8_t esc = base[j++];
				std::uint8_t	   dec;
				switch (esc) {
					case '"': dec = '"'; break;
					case '\\': dec = '\\'; break;
					case '/': dec = '/'; break;
					case 'b': dec = '\b'; break;
					case 'f': dec = '\f'; break;
					case 'n': dec = '\n'; break;
					case 'r': dec = '\r'; break;
					case 't': dec = '\t'; break;
					case 'u':
						{
							if (j + 4 > e)
								return NPOS;
							unsigned v = 0;
							for (int k = 0; k < 4; ++k) {
								const std::uint8_t h = base[j++];
								v <<= 4;
								if (h >= '0' && h <= '9')
									v |= h - '0';
								else if (h >= 'a' && h <= 'f')
									v |= h - 'a' + 10;
								else if (h >= 'A' && h <= 'F')
									v |= h - 'A' + 10;
								else
									return NPOS;
							}
							if (v < 0x80)
								mix(static_cast<std::uint8_t>(v));
							else if (v < 0x800) {
								mix(static_cast<std::uint8_t>(0xC0 | (v >> 6)));
								mix(static_cast<std::uint8_t>(0x80 | (v & 0x3F)));
							} else {
								mix(static_cast<std::uint8_t>(0xE0 | (v >> 12)));
								mix(static_cast<std::uint8_t>(0x80 | ((v >> 6) & 0x3F)));
								mix(static_cast<std::uint8_t>(0x80 | (v & 0x3F)));
							}
							continue;
						}
					default: return NPOS;
				}
				mix(dec);
			} else if (c < 0x20) {
				return NPOS;
			} else {
				mix(c);
			}
		}
		return NPOS;
	}

	std::size_t parse_number(std::size_t i, std::size_t e) {
		std::size_t j = i;
		if (base[j] == '-')
			++j;
		if (j >= e)
			return NPOS;
		if (base[j] == '0')
			++j;
		else if (base[j] >= '1' && base[j] <= '9')
			while (j < e && base[j] >= '0' && base[j] <= '9') ++j;
		else
			return NPOS;
		if (j < e && base[j] == '.') {
			++j;
			if (j >= e || !(base[j] >= '0' && base[j] <= '9'))
				return NPOS;
			while (j < e && base[j] >= '0' && base[j] <= '9') ++j;
		}
		if (j < e && (base[j] == 'e' || base[j] == 'E')) {
			++j;
			if (j < e && (base[j] == '+' || base[j] == '-'))
				++j;
			if (j >= e || !(base[j] >= '0' && base[j] <= '9'))
				return NPOS;
			while (j < e && base[j] >= '0' && base[j] <= '9') ++j;
		}
		push_node(J_NUM, i);
		tape.back().end = (std::uint32_t)j;
		mix('N');
		for (std::size_t k = i; k < j; ++k) mix(base[k]);
		return j;
	}

	std::size_t parse_lit(std::size_t i, std::size_t e) {
		auto eq = [&](const char* s, std::size_t len) {
			if (i + len > e)
				return false;
			for (std::size_t k = 0; k < len; ++k)
				if (base[i + k] != static_cast<std::uint8_t>(s[k]))
					return false;
			return true;
		};
		if (eq("true", 4)) {
			push_node(J_TRUE, i);
			tape.back().end = (std::uint32_t)(i + 4);
			mix('T');
			return i + 4;
		}
		if (eq("false", 5)) {
			push_node(J_FALSE, i);
			tape.back().end = (std::uint32_t)(i + 5);
			mix('F');
			return i + 5;
		}
		if (eq("null", 4)) {
			push_node(J_NULL, i);
			tape.back().end = (std::uint32_t)(i + 4);
			mix('0');
			return i + 4;
		}
		return NPOS;
	}
};

// Parse using the GPU stage-1 + the user hook.
template<scan_plan S>
bool pars_parse(
	gpu::scanner<S>& sc, const std::string& payload, bool want_pairs, json_parser& out
) {
	out.base				 = reinterpret_cast<const std::uint8_t*>(payload.data());
	out.n					 = payload.size();
	auto				   r = sc.scan_host(out.base, out.n, 0, want_pairs);
	model::structural_view sv;
	sv.pos	= r.structural.data();
	sv.pair = (want_pairs && !r.pair_pos.empty()) ? r.pair_pos.data() : nullptr;
	sv.n	= r.structural.size();
	model::replay(out.base, out.n, sv, out);
	out.finish();
	return out.ok;
}

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
	const char* fmt, const char* impl, const char* variant, std::size_t mb, std::vector<double> g,
	std::size_t tokens, std::size_t moved
) {
	std::sort(g.begin(), g.end());
	double sum = 0;
	for (double v : g) sum += v;
	const double mean = g.empty() ? 0.0 : sum / g.size();
	auto q = [&](double p) { return g[std::min(g.size() - 1, (std::size_t)(p * (g.size() - 1)))]; };
	std::printf(
		"%s,%s,%s,%zu,%.3f,%.3f,%.3f,%.3f,%.3f,%zu,%zu\n",
		fmt,
		impl,
		variant,
		mb,
		mean,
		q(0.50),
		q(0.95),
		g.front(),
		g.back(),
		tokens,
		moved
	);
	std::fflush(stdout);
}

}  // namespace

int main(int argc, char** argv) {
	const std::size_t mb	 = argc > 1 ? (std::size_t)std::atol(argv[1]) : 64;
	const int		  iters	 = argc > 2 ? std::atoi(argv[2]) : 10;
	const std::size_t target = mb * 1024 * 1024;

	std::string		  json;
	{
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

	constexpr auto	 kJ = compile::make_scan_plan<bench::json_grammar>();
	gpu::scanner<kJ> g;

	// --- correctness: our hook-driven parser vs the events it must consume ---
	json_parser check;
	const bool	ok_full = pars_parse<kJ>(g, json, /*want_pairs=*/true, check);
	std::fprintf(
		stderr,
		"[parse] json ok=%d nodes=%llu hash=%llx\n",
		(int)ok_full,
		(unsigned long long)check.nodes,
		(unsigned long long)check.hash
	);

	std::printf(
		"# format,impl,variant,scale_mb,GBps_mean,GBps_p50,GBps_p95,GBps_min,GBps_max,tokens,bytes_moved\n"
	);

	// --- pars: scan (GPU) + hook replay + DOM build (host) ---
	{
		const std::size_t tokens = check.nodes;
		json_parser		  pbench;
		emit(
			"json",
			"pars",
			"P3",
			mb,
			sample(
				json.size(),
				iters,
				[&] {
					pbench.reset();
					(void)pars_parse<kJ>(g, json, /*want_pairs=*/false, pbench);
				}
			),
			tokens,
			json.size() + tokens * sizeof(jnode)
		);
	}

	// --- simdjson: full DOM parse (builds the tape), then consume it ---
	{
		simdjson::dom::parser p;
		uint64_t			  sink = 0;
		emit(
			"json",
			"simdjson",
			"P",
			mb,
			sample(
				json.size(),
				iters,
				[&] {
					simdjson::dom::element root;
					if (p.parse(json).get(root))
						return;
					simdjson::dom::array arr = root.get_array();
					for (auto v : arr) sink += static_cast<std::uint64_t>(v.type());
				}
			),
			0,
			json.size()
		);
		(void)sink;
	}
	return 0;
}
