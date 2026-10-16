// pars/scan.hpp — the unified, elegant API.
//
// One entry point per grammar, policies as values:
//
//   pars::scan<Grammar>(host_bytes, opts)              // streaming host boundary (SOTA)
//   pars::scan_device<Grammar>(device_ptr, n)          // device-resident (fastest)
//   result.visit(bytes, visitor)                       // consumer (ondemand / DOM)
//
// Everything is derived from the grammar through the algebra core
// (`core::lower<Grammar>()` -> `core::plan` -> `prefix_reduce`): there is no
// per-format code, no policy globals, no runtime plan construction.
//
// Placement is a policy: `scan` overlaps H2D with the scan (chunked double
// buffer, two streams) so the host boundary reaches the device roofline;
// `scan_device` is zero-copy for data that is already resident.
#pragma once

#include "pars/core/cuda.hpp"
#include "pars/core/lower.hpp"
#include "pars/core/plan.hpp"
#include "pars/model/visit.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

#include <cuda_runtime.h>

namespace pars {

struct scan_options {
	bool pairs	= false;  // materialise bracket partners (forces a global pass)
	int	 chunks = 16;	  // streaming chunks; 0 -> single-shot device scan
	bool pinned = true;	  // pinned host staging
};

// The unified result: a structural index (+ optional bracket partners), with a
// consumer hook.  Language semantics stay in the visitor (model/visit.hpp).
struct scan_result {
	std::vector<std::int32_t> structural;
	std::vector<std::int32_t> pair;
	bool					  well_formed = true;

	[[nodiscard]] std::size_t tokens() const noexcept { return structural.size(); }

	template<class Visitor>
	void visit(std::string_view s, Visitor&& v) const {
		model::structural_view sv {
			structural.data(),
			pair.empty() ? nullptr : pair.data(),
			structural.size()
		};
		model::replay(
			reinterpret_cast<const std::uint8_t*>(s.data()), s.size(), sv, std::forward<Visitor>(v)
		);
	}
};

namespace detail {

// Per-grammar, per-thread staging: persistent device cache + streaming buffers.
template<class Grammar>
struct stream_ctx {
	static constexpr core::plan kPlan = core::lower<Grammar>();

	gpu::scanner<core::to_scan_plan<kPlan>()> sc;
	cudaStream_t							  s0 = nullptr, s1 = nullptr;
	cudaEvent_t								  ev[2] {};
	std::uint8_t*							  dev[2] {nullptr, nullptr};
	std::size_t								  devcap = 0;
	std::uint8_t*							  pin = nullptr;
	std::size_t								  pincap = 0;

	stream_ctx() {
		cudaFree(nullptr);	 // force the runtime + primary context up on this thread
		cudaStreamCreate(&s0);
		cudaStreamCreate(&s1);
		cudaEventCreate(&ev[0]);
		cudaEventCreate(&ev[1]);
	}
	~stream_ctx() {
		if (dev[0]) cudaFree(dev[0]);
		if (dev[1]) cudaFree(dev[1]);
		if (pin) cudaFreeHost(pin);
		cudaStreamDestroy(s0);
		cudaStreamDestroy(s1);
		cudaEventDestroy(ev[0]);
		cudaEventDestroy(ev[1]);
	}
	void ensure(std::size_t chunk, std::size_t n, bool pinned) {
		if (chunk > devcap) {
			if (dev[0]) cudaFree(dev[0]);
			if (dev[1]) cudaFree(dev[1]);
			gpu::detail::check(cudaMalloc(&dev[0], chunk), "stream dev0");
			gpu::detail::check(cudaMalloc(&dev[1], chunk), "stream dev1");
			devcap = chunk;
		}
		if (pinned && n > pincap) {
			if (pin) cudaFreeHost(pin);
			gpu::detail::check(cudaHostAlloc(&pin, n, cudaHostAllocDefault), "stream pin");
			pincap = n;
		}
	}
};

template<class Grammar>
[[nodiscard]] inline stream_ctx<Grammar>& ctx() {
	static thread_local stream_ctx<Grammar> c;
	return c;
}

}  // namespace detail

// Device-resident, zero-copy: input already on the device, results stay on the
// device.  The compute roofline of the generated scanner.
template<class Grammar>
[[nodiscard]] inline gpu::device_result scan_device(
	const std::uint8_t* d, std::size_t n, cudaStream_t s = 0, bool pairs = false
) {
	return detail::ctx<Grammar>().sc.scan_device(d, n, s, pairs, true, nullptr);
}

// Host boundary with H2D/scan overlap (chunked double buffer, two streams):
// the SOTA path.  `chunks == 0` degenerates to a single-shot upload + scan.
template<class Grammar>
[[nodiscard]] inline scan_result scan(std::string_view input, scan_options opt = {}) {
	scan_result r;
	if (input.empty())
		return r;
	auto&			  c = detail::ctx<Grammar>();
	const std::size_t n = input.size();

	if (opt.chunks <= 1) {
		// single-shot: used when pairs are needed (global pass) or by policy
		auto hr = c.sc.scan_host(reinterpret_cast<const std::uint8_t*>(input.data()), n, 0, opt.pairs);
		r.structural = std::move(hr.structural);
		r.pair		 = std::move(hr.pair_pos);
		r.well_formed = hr.well_formed;
		return r;
	}

	const std::size_t chunk = (((n + opt.chunks - 1) / opt.chunks) + 31) / 32 * 32;
	c.ensure(chunk, n, opt.pinned);
	std::memcpy(c.pin, input.data(), n);
	c.sc.reserve_struct(n);

	gpu::chunk_carry carry {};
	std::size_t		 off = 0;
	int				 i	 = 0;
	cudaMemcpyAsync(c.dev[0], c.pin, std::min(chunk, n), cudaMemcpyHostToDevice, c.s1);
	cudaEventRecord(c.ev[0], c.s1);
	for (; off < n; ++i) {
		const int		  cur = i & 1, nxt = (i + 1) & 1;
		const std::size_t len = std::min(chunk, n - off);
		if (off + len < n) {
			const std::size_t noff = off + len, nlen = std::min(chunk, n - noff);
			if (i >= 1)
				cudaStreamWaitEvent(c.s1, c.ev[nxt], 0);
			cudaMemcpyAsync(c.dev[nxt], c.pin + noff, nlen, cudaMemcpyHostToDevice, c.s1);
			cudaEventRecord(c.ev[nxt], c.s1);
		}
		cudaStreamWaitEvent(c.s0, c.ev[cur], 0);
		carry = c.sc.scan_chunk(c.dev[cur], len, carry, c.s0, off);
		off += len;
	}
	cudaStreamSynchronize(c.s0);

	r.structural.resize(carry.out_base);
	if (carry.out_base)
		cudaMemcpy(
			r.structural.data(), c.sc.struct_ptr(), std::size_t(carry.out_base) * 4, cudaMemcpyDeviceToHost
		);
	return r;
}

}  // namespace pars
