// pars/model/scan.hpp — scan-algebra primitives R3 (mask) + R4 (automaton scan) + R5
// (compaction), CPU backend.
//
// P1 execution semantics: every lexical masking rule is an automaton descriptor
// (`scan_plan::lex_desc`) executed as a state transform per byte; the running
// state is the prefix product of those transforms (R4).  No per-format branches.
#pragma once

#include "pars/model/classify.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace pars::model {

struct scan_result {
	std::vector<std::uint32_t> positions;  // structural byte offsets
	std::vector<std::uint8_t>  chars;
	std::vector<std::uint32_t> bracket_pos;
	std::vector<char>		   bracket_char;
	bool					   unclosed_mode = false;
};

template<scan_plan S>
class scanner {
	static constexpr classifier kCls			  = build_classifier(S);
	static constexpr bool		kStructIsBrackets = (S.n_punctuation == 0);

public:
	[[nodiscard]] scan_result scan(std::string_view in) const {
		const std::uint8_t*					p = reinterpret_cast<const std::uint8_t*>(in.data());
		const std::size_t					n = in.size();

		std::array<std::uint8_t, kMaxModes> st {};
		for (std::size_t d = 0; d < S.n_descs; ++d) st[d] = S.descs[d].start;

		scan_result out;
		for (std::size_t i = 0; i < n; ++i) {
			const unsigned char c	   = p[i];
			const std::uint32_t cls	   = kCls.of(c);

			bool				masked = false;
			for (std::size_t d = 0; d < S.n_descs; ++d) {
				const auto&		   desc = S.descs[d];
				const std::uint8_t cl	= desc.rep_of[c];
				if (desc.masked[st[d]][cl])
					masked = true;
				st[d] = desc.trans[st[d]][cl];
			}
			if (masked)
				continue;

			if (cls & cbit::structural) {
				out.positions.push_back(static_cast<std::uint32_t>(i));
				out.chars.push_back(c);
				if (cls & (cbit::open | cbit::close)) {
					out.bracket_pos.push_back(static_cast<std::uint32_t>(i));
					out.bracket_char.push_back(static_cast<char>(c));
				}
			}
		}

		for (std::size_t d = 0; d < S.n_descs; ++d)
			if (st[d] != S.descs[d].start) {
				out.unclosed_mode = true;
				break;
			}
		return out;
	}
};

}  // namespace pars::model
