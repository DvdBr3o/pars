// pars/model/visit.hpp — the user-defined point (hook).
//
// Stage-1 produces a *structural index*: the byte offsets of the structural
// symbols (delimiters, quotes boundaries, brackets) plus, when requested, the
// matching bracket for each bracket symbol.  Everything that is language
// specific — which runs of bytes form a value, how a value is decoded, what
// tree is built — is NOT known to the framework.  This header is the single
// extension point through which a user injects that behaviour.
//
// `replay` walks the input in byte order and emits a covering, non-overlapping
// event stream:
//   * scalar(begin, end)          — a maximal run of non-structural bytes
//   * symbol(pos, c, match)       — a structural byte; `match` is the byte offset
//                                   of its matching bracket, or -1
// The visitor is a duck-typed, caller-supplied object; the framework neither
// allocates nor interprets anything on its behalf.  This keeps include/ free of
// any format-specific logic while making a complete parser expressible.
#pragma once

#include <cstddef>
#include <cstdint>

namespace pars::model {

// Normalized structural index.  `pos` are byte offsets; `pair[i]` is the
// structural *index* of the matching bracket (or -1) when pairing is available,
// else `pair` is null.  Passing indices keeps the adapter allocation-free.
struct structural_view {
	const std::int32_t* pos	 = nullptr;
	const std::int32_t* pair = nullptr;
	std::size_t			n	 = 0;
};

template<class Visitor>
void replay(const std::uint8_t* base, std::size_t n, const structural_view& sv, Visitor&& v) {
	std::size_t prev = 0;
	for (std::size_t i = 0; i < sv.n; ++i) {
		const std::size_t p = static_cast<std::size_t>(sv.pos[i]);
		if (p > prev)
			v.scalar(prev, p);
		const std::int32_t match =
			(sv.pair && sv.pair[i] >= 0) ? sv.pos[sv.pair[i]] : std::int32_t {-1};
		v.symbol(p, static_cast<char>(base[p]), match);
		prev = p + 1;
	}
	if (n > prev)
		v.scalar(prev, n);
}

}  // namespace pars::model
