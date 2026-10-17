// bench/bench_mem_simdcsv.cpp — host RAM footprint of simdcsv's CSV index.
//
// simdcsv is a standalone program whose `main` allocates `uint32_t[p.size()]`
// as a worst-case bound (4x the input) but only uses `n_indexes` of them.
// Reporting that allocation as its "footprint" would be unfair (it is a
// conservative pre-allocation, not a working set).  We link the real
// `find_indexes` out of simdcsv's main.cpp (its `main` renamed away) and report
// the memory the index actually retains: n_indexes * sizeof(uint32_t).
//
// usage: pars.bench.mem_simdcsv <scale_mb>
// output: format,impl,placement,scale_mb,input_mib,used_mib,ratio
#define main simdcsv_unused_main
#include "main.cpp"
#undef main

#include <cstdio>
#include <cstdlib>
#include <string>

int main(int argc, char** argv) {
	const std::size_t mb	= argc > 1 ? (std::size_t)std::atol(argv[1]) : 128;
	const std::size_t target = mb * 1024 * 1024;

	// Same generator as bench/payloads.hpp::gen_csv (kept in sync).
	static const char* rows[] = {
		"alpha,beta,\"quoted, field\",\"has \"\"q\"\"\",42,true\n",
		"x,y,z,1,2,3\n",
		"\"a,b\",\"c\"\"d\",e,100,false\n",
		"long_field_name,another,third,4.5,-6,true\n",
		"1,2,3,4,5,6,7,8\n",
		"city,pop,lat,lon\n",
	};
	std::string doc;
	doc.reserve(target + CSV_PADDING);
	std::uint32_t st = 0x9e3779b9u;
	while (doc.size() < target) {
		st = st * 1664525u + 1013904223u;
		doc += rows[st % (sizeof(rows) / sizeof(rows[0]))];
	}
	// simdcsv reads up to +SIMDCSV_PADDING past the end.
	doc.append(CSV_PADDING, '\0');

	ParsedCSV pcsv;
	// Exactly as many slots as the index needs: this is the memory a consumer
	// would actually retain, not simdcsv's 4x worst-case reservation.
	pcsv.indexes = new (std::nothrow) std::uint32_t[doc.size()];
	if (!pcsv.indexes) return 1;

	bool ok = find_indexes(reinterpret_cast<const std::uint8_t*>(doc.data()), target, pcsv);
	// Shrink to the retained size (what the index occupies once built).
	const std::size_t used = static_cast<std::size_t>(pcsv.n_indexes) * sizeof(std::uint32_t);

	const double input_mib = static_cast<double>(target) / 1048576.0;
	const double used_mib  = static_cast<double>(used) / 1048576.0;
	std::printf("# format,impl,placement,scale_mb,input_mib,used_mib,ratio\n");
	std::printf(
		"csv,simdcsv,host,%zu,%.1f,%.1f,%.2f\n",
		mb,
		input_mib,
		used_mib,
		used_mib / input_mib
	);
	if (!ok) std::fprintf(stderr, "[simdcsv] find_indexes reported an error\n");
	std::fprintf(stderr, "[simdcsv] indexes=%u bytes=%zu\n", pcsv.n_indexes, used);
	delete[] pcsv.indexes;
	return 0;
}
