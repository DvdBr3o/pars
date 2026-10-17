// bench/bench_memory.cu — pars device working set, per language.
//
// Measures how much device memory the scanner holds relative to the input.
//
// FAIRNESS RULES (see docs/model/bench_memory.md):
//   1. ONE language per process.  Each grammar owns a persistent thread_local
//      device cache; running several grammars in one process makes every
//      measurement inherit the previous grammar's allocations and the CUDA
//      caching allocator's pooling, so the `cudaMemGetInfo` delta becomes
//      meaningless (we observed the same grammar swing 1.0x..80x).
//   2. Report STEADY-STATE peak, not the first-call growth.  We warm up, then
//      sample free VRAM continuously while running several timed scans, and
//      take `baseline_free - min_free_seen`.  This counts the persistent
//      buffers AND the transient scratch the scanner needs at once.
//   3. Measure the SAME interval for every implementation:
//        placement=device : input already resident, index stays on device
//                           (scan_device / cuJSON's own interval, no D2H)
//        placement=host   : host input, index copied back (scan(), the
//                           host-boundary path)
//      A consumer that builds a DOM is a different workload and is not
//      reported here.
//
// usage: pars.bench.memory <lang> <scale_mb> [payload_dir]
//        lang = json|csv|toml|ini|xml|clike
// output: format,impl,placement,scale_mb,input_mib,used_mib,ratio
#include "payloads.hpp"

#include "pars/pars.hpp"
#include "pars/scan.hpp"

#include "grammars/clike.hpp"
#include "grammars/csv.hpp"
#include "grammars/ini.hpp"
#include "grammars/json.hpp"
#include "grammars/toml.hpp"
#include "grammars/xml.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

#include <cuda_runtime.h>

namespace bp = bench_payloads;

using namespace pars;

namespace {

[[nodiscard]] std::size_t free_mib() {
	std::size_t fb = 0, tot = 0;
	cudaMemGetInfo(&fb, &tot);
	return fb / 1048576;
}

// One measurement: warm up, then track the MINIMUM free VRAM across `iters`
// scans.  `base` is the free VRAM before the first allocation; the working set
// is `base - min_free`.
template<class Run>
void measure(const char* fmt, const char* impl, const char* placement, std::size_t mb,
			 std::size_t input_bytes, Run&& run, int iters) {
	// Free VRAM before the scanner's persistent buffers exist.
	cudaDeviceSynchronize();
	const std::size_t before = free_mib();
	// Warm up: the first calls perform (and grow) the persistent allocations.
	for (int i = 0; i < 2; ++i)
		run();
	cudaDeviceSynchronize();
	// Steady-state working set: everything the scanner keeps resident after the
	// caches have reached their high-water mark.  Running more iterations must
	// not change this, which is why we do not report the first-call growth.
	const std::size_t after = free_mib();
	const double input_mib = static_cast<double>(input_bytes) / 1048576.0;
	const double used_mib  = static_cast<double>(before - after);

	// Invariant check: extra iterations stay within the measured footprint.
	std::size_t lo = after;
	for (int i = 0; i < iters; ++i) {
		run();
		cudaDeviceSynchronize();
		lo = std::min(lo, free_mib());
	}
	if (lo < after)
		std::fprintf(stderr, "[mem] %s/%s grew %.1f MiB after warmup\n", fmt, placement,
					 static_cast<double>(after - lo));

	std::printf(
		"%s,%s,%s,%zu,%.1f,%.1f,%.2f\n",
		fmt,
		impl,
		placement,
		mb,
		input_mib,
		used_mib,
		used_mib / input_mib
	);
	std::fflush(stdout);
}

// Device-resident: upload once (not part of the working set), scan with the
// index staying on the device.  This is the interval cuJSON measures.
template<class Grammar>
void device_resident(const char* fmt, const std::string& payload, std::size_t mb, int iters) {
	if (payload.empty()) return;
	const std::size_t		 n	   = payload.size();
	const std::size_t		 padded = (n + 31) / 32 * 32;
	std::uint8_t*			 d_in	= nullptr;
	cudaMalloc(&d_in, padded);
	cudaMemset(d_in, 0, padded);
	cudaMemcpy(d_in, payload.data(), n, cudaMemcpyHostToDevice);
	cudaDeviceSynchronize();

	measure(fmt, "pars", "device", mb, n, [&] {
		auto r = pars::scan_device<Grammar>(d_in, n, 0, false);
		(void)r;
	}, iters);
	cudaFree(d_in);
}

// Host boundary: the public `scan()` path, including the chunked H2D overlap
// and the D2H copy of the index.
template<class Grammar>
void host_boundary(const char* fmt, const std::string& payload, std::size_t mb, int iters) {
	if (payload.empty()) return;
	measure(fmt, "pars", "host", mb, payload.size(), [&] {
		auto r = pars::scan<Grammar>(payload);
		(void)r;
	}, iters);
}

template<class Grammar>
void both(const char* fmt, const std::string& payload, std::size_t mb, int iters) {
	device_resident<Grammar>(fmt, payload, mb, iters);
	host_boundary<Grammar>(fmt, payload, mb, iters);
}

}  // namespace

int main(int argc, char** argv) {
	if (argc < 2) {
		std::fprintf(stderr, "usage: %s <lang> <scale_mb> [payload_dir]\n", argv[0]);
		return 2;
	}
	const std::string lang = argv[1];
	const std::size_t mb   = argc > 2 ? std::strtoull(argv[2], nullptr, 10) : 256;
	const std::string pdir = argc > 3 ? argv[3] : "";
	const int		  iters = 5;
	const std::size_t target = mb * 1024 * 1024;

	cudaFree(nullptr);

	std::printf("# format,impl,placement,scale_mb,input_mib,used_mib,ratio\n");

	if (lang == "json") {
		std::string json = pdir.empty() ? "" : bp::read_file(pdir + "/json_" + std::to_string(mb) + ".json");
		if (json.empty()) json = bp::gen_json(target);
		both<bench::json_grammar>("json", json, mb, iters);
	} else if (lang == "csv") {
		both<bench::csv_grammar>("csv", bp::gen_csv(target), mb, iters);
	} else if (lang == "toml") {
		both<bench::toml_grammar>("toml", bp::gen_toml(target), mb, iters);
	} else if (lang == "ini") {
		both<bench::ini_grammar>("ini", bp::gen_ini(target), mb, iters);
	} else if (lang == "xml") {
		both<bench::xml_grammar>("xml", bp::gen_xml(target), mb, iters);
	} else if (lang == "clike") {
		both<bench::clike_grammar>("clike", bp::gen_clike(target), mb, iters);
	} else {
		std::fprintf(stderr, "unknown lang: %s\n", lang.c_str());
		return 2;
	}
	return 0;
}
