// bench/bench_query.cpp — host query tier over the structural index.
//
// Mirrors cuJSON's real-world use case: the GPU computes the structural index
// (R1-R6) and copies it to the host; a *user-defined*, lazy, query-driven
// iterator then materialises only the values the query touches.  We report
//   parse time (R1-R6 + transport)  |  query time  |  total,
// exactly the Parsing / Query / Total split cuJSON uses.
//
// The iterator below is JSON-specific (the user's job); the framework only
// supplies the structural cursor (pars/model/cursor.hpp).
#include "pars/pars.hpp"
#include "pars/backend/cuda/pipeline.cuh"

#include "grammars/json.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace std::chrono;
using namespace pars;

namespace {

using model::structural_cursor;
using model::trim;

// The user's lazy JSON query: count top-level records with type == value,
// summing their `id` (forces a real value decode).
struct json_query {
	const structural_cursor& c;
	std::uint64_t			 id_sum = 0;

	explicit json_query(const structural_cursor& cur) : c(cur) {}

	// Token index just after the value that starts right after `colon`.
	std::size_t skip_value(std::size_t colon) const {
		const std::size_t vt = colon + 1;
		if (!c.valid(vt))
			return vt;
		const char vc = c.ch(vt);
		if (vc == '{' || vc == '[') {
			const std::int32_t m = c.match_index(vt);
			return (m >= 0) ? static_cast<std::size_t>(m) + 1 : vt + 1;
		}
		return vt;	// scalar: token `vt` is already the separator
	}

	bool scalar_equals(std::size_t colon, std::string_view want) const {
		std::string_view g = trim(c.gap(colon + 1));
		if (g.size() >= 2 && g.front() == '"' && g.back() == '"')
			g = g.substr(1, g.size() - 2);
		return g == want;
	}

	std::uint64_t scalar_int(std::size_t colon) const {
		std::string_view g = trim(c.gap(colon + 1));
		std::uint64_t	 v = 0;
		for (char ch : g)
			if (ch >= '0' && ch <= '9')
				v = v * 10 + static_cast<std::uint64_t>(ch - '0');
		return v;
	}

	bool object_matches(std::size_t open, std::size_t close) {
		std::size_t j	  = open + 1;
		bool		match = false;
		while (j < close) {
			if (c.ch(j) == '}')
				break;
			if (c.ch(j) != ':') {  // skip a stray value
				j = skip_value(j - 1);
				if (j < close && c.ch(j) == ',')
					++j;
				continue;
			}
			std::string_view key = trim(c.gap(j));
			if (key.size() >= 2 && key.front() == '"' && key.back() == '"')
				key = key.substr(1, key.size() - 2);
			if (key == "type")
				match = scalar_equals(j, "PushEvent");
			else if (key == "id")
				id_sum += scalar_int(j);
			j = skip_value(j);
			if (j < close && c.ch(j) == ',')
				++j;
		}
		return match;
	}

	// Count records whose `type` field equals `value`.
	std::size_t count_pushevents() {
		if (c.n < 2 || c.ch(0) != '[')
			return 0;
		const std::int32_t close   = c.match_index(0);
		const std::size_t  end	   = (close >= 0) ? static_cast<std::size_t>(close) : c.n;
		std::size_t		   matches = 0;
		std::size_t		   elem	   = 1;
		while (elem < end) {
			const char ch = c.ch(elem);
			if (ch == ']')
				break;
			if (ch == '{') {
				const std::int32_t m = c.match_index(elem);
				if (m < 0)
					break;
				if (object_matches(elem, static_cast<std::size_t>(m)))
					++matches;
				elem = static_cast<std::size_t>(m) + 1;
			} else {
				elem = skip_value(elem - 1);
			}
			if (elem < c.n && c.ch(elem) == ',')
				++elem;
		}
		return matches;
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

double mean(std::vector<double> v) {
	if (v.empty())
		return 0.0;
	double s = 0;
	for (double x : v) s += x;
	return s / v.size();
}

}  // namespace

int main(int argc, char** argv) {
	const std::size_t mb	 = argc > 1 ? (std::size_t)std::atol(argv[1]) : 64;
	const int		  iters	 = argc > 2 ? std::atoi(argv[2]) : 10;
	const std::size_t target = mb * 1024 * 1024;

	// GitHub-Archive-like records: 3 of every 4 are PushEvents.
	const std::string push =
		"{\"type\":\"PushEvent\",\"id\":12345,\"repo\":{\"name\":\"r\",\"id\":7},\"actor\":{\"id\":9}}";
	const std::string fork =
		"{\"type\":\"ForkEvent\",\"id\":54321,\"repo\":{\"name\":\"r\"},\"actor\":{\"id\":9}}";
	std::string json;
	std::size_t records = 0, expected = 0;
	json.push_back('[');
	while (json.size() < target) {
		if (records)
			json.push_back(',');
		if (records % 4 == 3)
			json += fork;
		else {
			json += push;
			++expected;
		}
		++records;
	}
	json += ']';

	constexpr auto		kJ = compile::make_scan_plan<bench::json_grammar>();
	gpu::scanner<kJ>	g;
	const std::uint8_t* base = reinterpret_cast<const std::uint8_t*>(json.data());

	// One scan to obtain the host-side structural index (+ bracket partners).
	auto					 hr = g.scan_host(base, json.size(), 0, /*want_pairs=*/true);
	model::structural_cursor cur;
	cur.pos	 = hr.structural.data();
	cur.pair = hr.pair_pos.data();
	cur.base = base;
	cur.n	 = hr.structural.size();

	json_query		  q(cur);
	const std::size_t got = q.count_pushevents();
	std::fprintf(
		stderr,
		"[query] json matched=%zu expected=%zu id_sum=%llu ok=%d\n",
		got,
		expected,
		(unsigned long long)q.id_sum,
		(int)(got == expected)
	);

	const double		parse_g = mean(sample(json.size(), iters, [&] {
		auto r = g.scan_host(base, json.size(), 0, /*want_pairs=*/true);
		if (r.structural.empty())
			std::fprintf(stderr, "empty\n");
	}));

	std::vector<double> qsec;
	qsec.reserve(iters);
	for (int i = 0; i < iters; ++i) {
		auto	   a = steady_clock::now();
		json_query qq(cur);
		(void)qq.count_pushevents();
		auto b = steady_clock::now();
		qsec.push_back(duration<double>(b - a).count());
	}
	const double q_sec	   = mean(qsec);
	const double parse_sec = static_cast<double>(json.size()) / (parse_g * 1e9);
	const double q_ns	   = q_sec / (double)records * 1e9;
	const double total	   = static_cast<double>(json.size()) / (parse_sec + q_sec) / 1e9;

	std::printf(
		"# format,impl,scale_mb,records,matched,parse_GBps,query_ns_per_record,total_GBps\n"
	);
	std::printf("json,pars,%zu,%zu,%zu,%.3f,%.1f,%.3f\n", mb, records, got, parse_g, q_ns, total);
	return 0;
}
