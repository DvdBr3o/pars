// bench/bench_boundary.cu — three-tier benchmark with per-iteration statistics.
//
//   N1a  device-resident stage-1 (P1-P3)      N1b  +P4 bracket pairing
//   N3   host boundary (pinned+async)          N3c  chunked overlap (parity path)
//
// Output (one row per format x tier), p50/p95 over `iters` full scans:
//   format,impl,variant,scale_mb,GBps_p50,GBps_p95,GBps_min,GBps_max,tokens,bytes_moved
#include "pars/pars.hpp"
#include "pars/backend/cuda/pipeline.cuh"

#include "grammars/json.hpp"
#include "grammars/csv.hpp"
#include "grammars/toml.hpp"
#include "grammars/clike.hpp"
#include "grammars/ini.hpp"
#include "grammars/xml.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <random>
#include <string>
#include <vector>

using namespace std::chrono;
using namespace pars;

static double gbps(std::size_t bytes, double s) {
	return (double)bytes / s / 1e9;
}

static std::string repeat_rec(const std::string& rec, std::size_t target) {
	std::string out;
	out.reserve(target);
	while (out.size() < target) out += rec;
	return out;
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

static void emit(
	const char* fmt, const char* impl, const char* variant, std::size_t mb, std::vector<double> g,
	std::uint32_t tokens, std::size_t moved
) {
	std::sort(g.begin(), g.end());
	double sum = 0;
	for (double v : g) sum += v;
	const double mean = g.empty() ? 0.0 : sum / g.size();
	auto q = [&](double p) { return g[std::min(g.size() - 1, (std::size_t)(p * (g.size() - 1)))]; };
	std::printf(
		"%s,%s,%s,%zu,%.3f,%.3f,%.3f,%.3f,%.3f,%u,%zu\n",
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

static void hardware_facts(std::size_t n) {
	cudaDeviceProp prop {};
	cudaGetDeviceProperties(&prop, 0);
	std::fprintf(stderr, "# device=%s sm_%d%d\n", prop.name, prop.major, prop.minor);
	void* hp = nullptr;
	void* dp = nullptr;
	cudaHostAlloc(&hp, n, cudaHostAllocDefault);
	cudaMalloc(&dp, n);
	std::memset(hp, 1, n);
	auto time = [&](auto f) {
		f();
		auto t0 = steady_clock::now();
		for (int i = 0; i < 5; ++i) f();
		auto t1 = steady_clock::now();
		return gbps(n * 5, duration<double>(t1 - t0).count());
	};
	std::fprintf(
		stderr,
		"# pinned H2D=%.2f GB/s D2H=%.2f GB/s\n",
		time([&] { cudaMemcpy(dp, hp, n, cudaMemcpyHostToDevice); }),
		time([&] { cudaMemcpy(hp, dp, n, cudaMemcpyDeviceToHost); })
	);
	cudaFree(dp);
	cudaFreeHost(hp);
}

template<scan_plan S>
static bool validate(const std::string& s) {
	model::scanner<S> cpu;
	auto			  c = cpu.scan(s);
	gpu::scanner<S>	  g;
	auto			  r = g.scan_host(reinterpret_cast<const std::uint8_t*>(s.data()), s.size());
	if (c.positions.size() != r.structural.size())
		return false;
	for (std::size_t i = 0; i < c.positions.size(); ++i)
		if ((std::int32_t)c.positions[i] != r.structural[i])
			return false;
	return true;
}

template<scan_plan S>
static bool validate_random(const char* alphabet, std::size_t n, unsigned seed) {
	std::mt19937	  rng(seed);
	const std::size_t A = std::strlen(alphabet);
	std::string		  s(n, ' ');
	for (char& c : s) c = alphabet[rng() % A];
	return validate<S>(s);
}

// Overlapped chunked host path (parity path only).
template<scan_plan S>
static std::vector<double> sample_chunked(
	gpu::scanner<S>& sc, const std::string& payload, int iters, int nchunks, std::size_t& moved,
	std::uint32_t& tokens
) {
	const std::size_t n	  = payload.size();
	std::uint8_t*	  pin = nullptr;
	cudaHostAlloc(&pin, n, cudaHostAllocDefault);
	std::memcpy(pin, payload.data(), n);
	const std::size_t chunk	 = (((n + nchunks - 1) / nchunks) + 31) / 32 * 32;
	std::uint8_t*	  dev[2] = {nullptr, nullptr};
	cudaMalloc(&dev[0], chunk);
	cudaMalloc(&dev[1], chunk);
	sc.reserve_struct(n);
	cudaStream_t s0, s1;
	cudaStreamCreate(&s0);
	cudaStreamCreate(&s1);
	cudaEvent_t ev[2];
	cudaEventCreate(&ev[0]);
	cudaEventCreate(&ev[1]);
	// The host boundary must move the structural index back; do it into a
	// pinned host buffer (sized to the actual output, not the 4n upper bound).
	std::int32_t* hout = nullptr;

	auto		  run  = [&](bool copy) {
		gpu::chunk_carry carry {};
		std::size_t		 off = 0;
		int				 i	 = 0;
		{
			std::size_t len = std::min(chunk, n);
			cudaMemcpyAsync(dev[0], pin, len, cudaMemcpyHostToDevice, s1);
			cudaEventRecord(ev[0], s1);
		}
		for (; off < n; ++i) {
			const int		  cur = i & 1, nxt = (i + 1) & 1;
			const std::size_t len = std::min(chunk, n - off);
			if (off + len < n) {
				const std::size_t noff = off + len, nlen = std::min(chunk, n - noff);
				if (i >= 1)
					cudaStreamWaitEvent(s1, ev[nxt], 0);
				cudaMemcpyAsync(dev[nxt], pin + noff, nlen, cudaMemcpyHostToDevice, s1);
				cudaEventRecord(ev[nxt], s1);
			}
			cudaStreamWaitEvent(s0, ev[cur], 0);
			carry = sc.scan_chunk(dev[cur], len, carry, s0, off);
			off += len;
		}
		if (copy && carry.out_base)
			cudaMemcpyAsync(
				hout,
				sc.struct_ptr(),
				(std::size_t)carry.out_base * 4,
				cudaMemcpyDeviceToHost,
				s0
			);
		cudaStreamSynchronize(s0);
		return carry.out_base;
	};
	tokens = run(false);
	if (tokens) {
		cudaHostAlloc((void**)&hout, (std::size_t)tokens * 4, cudaHostAllocDefault);
		cudaMemcpy(hout, sc.struct_ptr(), (std::size_t)tokens * 4, cudaMemcpyDeviceToHost);
	}
	// correctness of chunking vs CPU
	model::scanner<S> cpu;
	auto			  c	 = cpu.scan(payload);
	bool			  ok = (c.positions.size() == (std::size_t)tokens);
	if (ok)
		for (std::size_t k = 0; k < (std::size_t)tokens; ++k)
			if ((std::int32_t)c.positions[k] != hout[k]) {
				ok = false;
				break;
			}
	std::fprintf(stderr, "[chunked] correct=%d chunks=%d\n", (int)ok, nchunks);

	moved  = n + (std::size_t)tokens * 4;
	auto g = sample(n, iters, [&] { run(true); });
	if (hout)
		cudaFreeHost(hout);
	cudaFree(dev[0]);
	cudaFree(dev[1]);
	cudaFreeHost(pin);
	cudaStreamDestroy(s0);
	cudaStreamDestroy(s1);
	return g;
}

template<scan_plan S>
static void bench_format(
	const char* fmt, const std::string& payload, const char* alphabet, std::size_t mb, int iters
) {
	const bool ok_full = validate<S>(payload);
	const bool ok_rand = validate_random<S>(alphabet, 1 << 20, 12345u)
					  && validate_random<S>(alphabet, 1 << 20, 999u);
	std::fprintf(stderr, "[validate] %s full=%d random=%d\n", fmt, (int)ok_full, (int)ok_rand);

	const std::size_t n		 = payload.size();
	const std::size_t padded = (n + 31) / 32 * 32;
	gpu::scanner<S>	  g;
	std::uint8_t*	  dp = nullptr;
	cudaMalloc(&dp, padded);
	cudaMemcpy(dp, payload.data(), n, cudaMemcpyHostToDevice);
	if (padded > n)
		cudaMemset(dp + n, 0, padded - n);

	gpu::stage_times	st {};
	auto				dr	   = g.scan_device(dp, n, 0, false, true, &st);
	const std::uint32_t tokens = dr.n_struct;
	std::printf("#stage,%s,%.3f,%.3f,%.3f,%.3f\n", fmt, st.p1, st.p2, st.p3, st.p4);

	emit(
		fmt,
		"pars",
		"N1a",
		mb,
		sample(n, iters, [&] { (void)g.scan_device(dp, n, 0, false, false); }),
		tokens,
		0
	);
	emit(
		fmt,
		"pars",
		"N1b",
		mb,
		sample(n, iters, [&] { (void)g.scan_device(dp, n, 0, false, true); }),
		tokens,
		0
	);
	// N2 = H2D + R1-R6, no D2H: the interval cuJSON's published total covers.
	emit(
		fmt,
		"pars",
		"N2",
		mb,
		sample(
			n,
			iters,
			[&] {
				cudaMemcpy(dp, payload.data(), n, cudaMemcpyHostToDevice);
				(void)g.scan_device(dp, n, 0, false, true);
				cudaStreamSynchronize(0);
			}
		),
		tokens,
		n
	);
	cudaFree(dp);

	emit(
		fmt,
		"pars",
		"N3",
		mb,
		sample(
			n,
			iters,
			[&] {
				auto r =
					g.scan_host(reinterpret_cast<const std::uint8_t*>(payload.data()), n, 0, false);
				(void)r;
			}
		),
		tokens,
		n + (std::size_t)tokens * 4
	);

	// chunked overlap (all formats; generic carry)
	{
		std::size_t	  moved = 0;
		std::uint32_t tk	= 0;
		auto		  g3	= sample_chunked<S>(g, payload, iters, 16, moved, tk);
		emit(fmt, "pars", "N3c", mb, g3, tk, moved);
	}
}

int main(int argc, char** argv) {
	const std::size_t mb	 = argc > 1 ? (std::size_t)std::atol(argv[1]) : 64;
	const int		  iters	 = argc > 2 ? std::atoi(argv[2]) : 10;
	const std::size_t target = mb * 1024 * 1024;

	hardware_facts(target);

	std::string json;
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
	std::string csv =
		repeat_rec("alpha,beta,\"quoted, field\",\"has \"\"q\"\"\",42,true\n", target);
	std::string toml = repeat_rec(
		"[server]\nhost = \"localhost\"\nport = 8080\nname = 'a # b'\n"
		"# comment { } , = with \"quotes\"\narr = [1, 2, 3]\n",
		target
	);
	std::string clike = repeat_rec("int x; /* c { ; } */ y = 1; // { ; }\n", target);
	std::string ini =
		repeat_rec("[section]\nkey = value\nnum = 123\n; comment\nother = x\n", target);
	std::string	   xml = repeat_rec("<item id=\"1\" name=\"x &amp; y\">alpha</item>", target);

	constexpr auto kJ  = compile::make_scan_plan<bench::json_grammar>();
	constexpr auto kC  = compile::make_scan_plan<bench::csv_grammar>();
	constexpr auto kT  = compile::make_scan_plan<bench::toml_grammar>();
	constexpr auto kL  = compile::make_scan_plan<bench::clike_grammar>();
	constexpr auto kI  = compile::make_scan_plan<bench::ini_grammar>();
	constexpr auto kX  = compile::make_scan_plan<bench::xml_grammar>();

	std::printf(
		"# format,impl,variant,scale_mb,GBps_mean,GBps_p50,GBps_p95,GBps_min,GBps_max,tokens,bytes_moved\n"
	);
	bench_format<kJ>("json", json, "{}[]:,\"\\0123456789tfn.-eE \n", mb, iters);
	bench_format<kC>("csv", csv, ",.\n\r\"abc0123 ", mb, iters);
	bench_format<kT>("toml", toml, "[]{}.,=\"'#\nabc0123 \n", mb, iters);
	bench_format<kL>("clike", clike, "{};/*\n abc123", mb, iters);
	bench_format<kI>("ini", ini, "[]=;\n abc0123", mb, iters);
	bench_format<kX>("xml", xml, "<>/=?!\"' ab0123", mb, iters);
	return 0;
}
