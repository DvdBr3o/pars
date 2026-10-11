// tests/test_meta.cpp — Layer 1+2 of the meta backend: scan_plan -> pipeline IR
// -> rewritten pipeline.  Host-only, so it runs under the plain C++ toolchain.
#include "pars/backend/cuda/meta/rewrite.hpp"

#include "../bench/grammars/csv.hpp"
#include "../bench/grammars/json.hpp"
#include "../bench/grammars/toml.hpp"

#include <cstdio>
#include <type_traits>

using namespace pars;
using namespace pars::meta;

constexpr auto kJ = compile::make_scan_plan<bench::json_grammar>();
constexpr auto kC = compile::make_scan_plan<bench::csv_grammar>();
constexpr auto kT = compile::make_scan_plan<bench::toml_grammar>();

// ---- Layer 1: pristine pipelines match the grammar shape --------------------
static_assert(std::is_same_v<
			  build_pipeline_t<kJ>,
			  pipeline<classify, mask<scan_plan::lex_kind::affine, 0>, clip, compact, nest>>);
static_assert(std::is_same_v<
			  build_pipeline_t<kC>,
			  pipeline<classify, mask<scan_plan::lex_kind::doubled, 0>, clip, compact, nest>>);
static_assert(mask_count_v<typename build_pipeline_t<kT>::stages> >= 2);

// ---- Layer 2: head fusion -----------------------------------------------
// JSON (affine, single mode) fuses classify+mask+clip into head<affine>.
static_assert(std::is_same_v<
			  rewritten_pipeline_t<kJ>,
			  pipeline<head<scan_plan::lex_kind::affine>, compact, nest>>);
// CSV (doubled, single mode) fuses into head<doubled>.
static_assert(std::is_same_v<
			  rewritten_pipeline_t<kC>,
			  pipeline<head<scan_plan::lex_kind::doubled>, compact, nest>>);
// TOML (multi-mode) must NOT fuse: classify survives, no head node.
static_assert(contains_v<classify, typename rewritten_pipeline_t<kT>::stages>);
static_assert(!contains_v<head<scan_plan::lex_kind::affine>, typename rewritten_pipeline_t<kT>::stages>);

// ---- policy mapping -----------------------------------------------------
static_assert(std::is_same_v<policy_of_t<scan_plan::lex_kind::affine>, affine_policy>);
static_assert(std::is_same_v<policy_of_t<scan_plan::lex_kind::doubled>, parity_policy>);
static_assert(std::is_same_v<policy_of_t<scan_plan::lex_kind::comment>, comment_policy>);
static_assert(std::is_same_v<policy_of_t<scan_plan::lex_kind::table>, table_policy>);

int main() {
	std::printf("[meta] IR + rewrite static assertions passed\n");
	std::printf(
		"[meta] json descs=%u csv descs=%u toml descs=%u\n",
		(unsigned)kJ.n_descs,
		(unsigned)kC.n_descs,
		(unsigned)kT.n_descs
	);
	return 0;
}
