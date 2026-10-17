// bench/bench_core.cu — the unified pipeline on the GPU.
//
//   grammar -> core::plan -> (S2 adapter) -> scan kernels
//
// Asserts the new IR round-trips to the reference plan bit-for-bit, then
// measures the same tiers as bench_boundary so the numbers are directly
// comparable to the SOTA baseline.
#include "pars/pars.hpp"

#include "pars/backend/cuda/pipeline.cuh"
#include "pars/core/cuda.hpp"
#include "pars/core/lower.hpp"

#include "grammars/json.hpp"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

using namespace pars;
using clk = std::chrono::steady_clock;

constexpr auto kCore = core::lower<bench::json_grammar>();
constexpr auto kRef	 = compile::make_scan_plan<bench::json_grammar>();
constexpr auto kScan = core::to_scan_plan<kCore>();

// the new IR must reproduce the reference plan exactly (else the adapter bugs)
static_assert(kScan == kRef, "core::plan does not round-trip to the reference scan_plan");

static double gbps(std::size_t bytes, double s) { return static_cast<double>(bytes) / s / 1e9; }

template<class F>
static double sample_mean(std::size_t n, int iters, F&& f) {
	f();
	f();  // warm up
	double s = 0;
	for (int i = 0; i < iters; ++i) {
		auto a = clk::now();
		f();
		auto c = clk::now();
		s += std::chrono::duration<double>(c - a).count();
	}
	return gbps(n, s / iters);
}

int main(int argc, char** argv) {
	const std::size_t mb	 = argc > 1 ? (std::size_t)std::atol(argv[1]) : 256;
	const int		  iters	 = argc > 2 ? std::atoi(argv[2]) : 3;
	const std::size_t target = mb * 1024 * 1024;

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
	const std::size_t n		 = json.size();
	const std::size_t padded = (n + 31) / 32 * 32;

	std::uint8_t* dp = nullptr;
	cudaMalloc(&dp, padded);
	cudaMemset(dp, 0, padded);	// scanner reads ceil(n/32)*32 bytes
	cudaMemcpy(dp, json.data(), n, cudaMemcpyHostToDevice);

	core::scanner<kCore> sc;

	gpu::stage_times st {};
	auto			 dr = sc.scan_device(dp, n, 0, false, true, &st);
	std::printf("#stage,json,%.3f,%.3f,%.3f,%.3f\n", st.p1, st.p2, st.p3, st.p4);
	std::printf("# tokens=%u\n", dr.n_struct);

	std::printf(
		"# format,impl,variant,scale_mb,GBps,tokens\n"
	);
	const double n1a = sample_mean(n, iters, [&] { (void)sc.scan_device(dp, n, 0, false, false); });
	const double n1b = sample_mean(n, iters, [&] { (void)sc.scan_device(dp, n, 0, false, true); });
	const double n3	 = sample_mean(
		 n,
		 iters,
		 [&] {
			 (void)sc.scan_host(reinterpret_cast<const std::uint8_t*>(json.data()), n, 0, false);
		 }
	 );
	std::printf("json,core,N1a,%zu,%.3f,%u\n", mb, n1a, dr.n_struct);
	std::printf("json,core,N1b,%zu,%.3f,%u\n", mb, n1b, dr.n_struct);
	std::printf("json,core,N3,%zu,%.3f,%u\n", mb, n3, dr.n_struct);

	cudaFree(dp);
	return 0;
}
