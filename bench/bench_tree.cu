// bench/bench_tree.cu — the tree schema (R7) on the GPU.
//
// A single self-recursion with an associative operator is a monoid; the leaves
// are the integer tokens and the fold is a device reduction.  This measures the
// schema's throughput and validates it against a host sum.
//
//   grammar: expr ::= num ('+' expr)*   -> tree cell (op '+')
//   reduce : sum of all integer leaves, via prefix_reduce<reducer<+,i64>>
#include "pars/core/cuda.hpp"
#include "pars/core/lower.hpp"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace pars;
using clk = std::chrono::steady_clock;

struct expr_grammar {
	struct expr {};
	struct num {};
	using rules = type_list<expr, num>;
	using start = expr;
	template<class>
	struct def;
};
template<>
struct expr_grammar::def<expr_grammar::num> {
	using type = plus<cls<'0', '1', '2', '3', '4', '5', '6', '7', '8', '9'>>;
};
template<>
struct expr_grammar::def<expr_grammar::expr> {
	using type = seq<rule<expr_grammar::num>, star<seq<lit<'+'>, rule<expr_grammar::expr>>>>;
};

constexpr auto kExpr = core::lower<expr_grammar>();

int main(int argc, char** argv) {
	const std::size_t mb	 = argc > 1 ? (std::size_t)std::atol(argv[1]) : 256;
	const int		  iters	 = argc > 2 ? std::atoi(argv[2]) : 5;
	const std::size_t target = mb * 1024 * 1024;

	std::string payload;
	std::int64_t host_sum = 0;
	{
		std::uint32_t v = 1;
		while (payload.size() < target) {
			if (!payload.empty())
				payload.push_back('+');
			payload += std::to_string(v);
			host_sum += v;
			v = (v % 1000) + 1;
		}
	}
	const std::size_t n = payload.size();

	std::uint8_t* dp = nullptr;
	cudaMalloc(&dp, n);
	cudaMemcpy(dp, payload.data(), n, cudaMemcpyHostToDevice);

	// validate: device reduce (driven by the plan's tree cell) == host sum
	const std::int64_t got = core::reduce_device<kExpr>(dp, n);
	cudaDeviceSynchronize();
	const bool ok = (got == host_sum);

	auto bench = [&](auto&& f) {
		f();
		f();
		double s = 0;
		for (int i = 0; i < iters; ++i) {
			auto a = clk::now();
			(void)f();
			auto b = clk::now();
			s += std::chrono::duration<double>(b - a).count();
		}
		return static_cast<double>(n) / (s / iters) / 1e9;
	};

	// `reduce_device<P>` lowers the plan's tree cell to `tree_reduce_gpu` with the
	// default chunk, so these two rows exercise the same kernel; they are kept
	// to show that the plan path adds no overhead, not as independent impls.
	const double g_plan = bench([&] { return core::reduce_device<kExpr>(dp, n); });
	const double g_raw	= bench([&] { return core::tree_reduce_gpu<core::add_i64>(dp, n, 1024); });
	cudaDeviceSynchronize();

	std::printf("# format,impl,schema,scale_mb,GBps,checksum,ok\n");
	std::printf("expr,pars,tree(plan),%zu,%.3f,%lld,%d\n", mb, g_plan, (long long)got, (int)ok);
	std::printf("expr,pars,tree(raw),%zu,%.3f,%lld,%d\n", mb, g_raw, (long long)got, (int)ok);
	std::fprintf(
		stderr,
		"[tree] n=%zu host_sum=%lld got=%lld ok=%d\n",
		n,
		(long long)host_sum,
		(long long)got,
		(int)ok
	);
	cudaFree(dp);
	return ok ? 0 : 1;
}
