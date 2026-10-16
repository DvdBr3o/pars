// pars/core/lower.hpp — grammar -> core::plan.
//
// STAGING NOTE: this first cut reuses the existing analysis in
// `pars/compile/plan.hpp` (DFA via Brzozowski derivatives, FIRST/LAST bracket
// derivation, literal collection) and *re-expresses* its output as the new
// unified IR.  The analysis code will be inlined here in the next cut; the
// point of this step is that the new executor runs off `core::plan` alone.
#pragma once

#include "pars/compile/plan.hpp"
#include "pars/core/plan.hpp"

#include <cstdint>

namespace pars::core {

namespace detail {

// ---------------------------------------------------------------------------
// S4-a: detect a single self-recursion whose body is the monoid/list pattern
//   rule ::= X (op X)*        op a single literal
// The repeated element is `<lit<op>, X>` (operand-first) or `<X, lit<op>>`
// (operator-first); the operator literal is the tree-schema operator.
// ---------------------------------------------------------------------------
template<class Node>
struct op_ch {
	static constexpr char value = 0;
};
template<char C>
struct op_ch<lit<C>> {
	static constexpr char value = C;
};

template<class Node>
struct elem_op {
	static constexpr char value = 0;
};
template<class A, class B>
struct elem_op<seq<A, B>> {
	static constexpr char value = (op_ch<A>::value != 0) ? op_ch<A>::value : op_ch<B>::value;
};

template<class Node>
struct rep_op {
	static constexpr char value = 0;
};
template<class G, int Mn, int Mx>
struct rep_op<rep<G, Mn, Mx>> {
	static constexpr char value = elem_op<G>::value;
};

template<class... Gs>
struct any_rep_op {
	static constexpr char value = 0;
};
template<class G0, class... Gs>
struct any_rep_op<G0, Gs...> {
	static constexpr char value =
		rep_op<G0>::value != 0 ? rep_op<G0>::value : any_rep_op<Gs...>::value;
};

template<class Node>
struct monoid_op {
	static constexpr char value = 0;
};
template<class... Gs>
struct monoid_op<seq<Gs...>> {
	static constexpr char value = any_rep_op<Gs...>::value;
};
template<class G, int Mn, int Mx>
struct monoid_op<rep<G, Mn, Mx>> {
	static constexpr char value = elem_op<G>::value;
};

}  // namespace detail

template<class Grammar, class List>
struct tree_scan;

template<class Grammar, class... Tags>
struct tree_scan<Grammar, type_list<Tags...>> {
	static constexpr void run(plan& P) noexcept { (one<Grammar, Tags>(P), ...); }

	template<class G, class Tag>
	static constexpr void one(plan& P) noexcept {
		if constexpr (compile::is_recursive_v<G, Tag>) {
			using Def = compile::rule_def_t<G, Tag>;
			// bracket-guarded recursion is the `stack` schema; everything else
			// that matches the monoid pattern is the `tree` schema
			if constexpr (!compile::bracket_guard<G, Def>::ok) {
				constexpr char op = detail::monoid_op<Def>::value;
				if (op != 0 && P.n_cells < kMaxCells) {
					cell c;
					c.sch = schema::tree;
					c.op  = op;
					P.cells[P.n_cells++] = c;
				}
			}
		}
	}
};

template<class Grammar>
[[nodiscard]] constexpr plan lower() noexcept {
	constexpr auto sp = compile::make_scan_plan<Grammar>();
	plan		   P {};

	// lexical rules -> finite cells (the transition monoid of their DFA)
	for (std::uint8_t d = 0; d < sp.n_descs; ++d) {
		const auto& dd = sp.descs[d];
		if (!dd.ok)
			continue;
		cell c;
		c.sch		= schema::finite;
		c.n_states	= dd.n_states;
		c.n_classes = dd.n_classes;
		c.start		= dd.start;
		c.rep_of	= dd.rep_of;
		c.delim		= dd.delim;
		c.escape	= dd.escape;
		switch (dd.kind) {
			case scan_plan::lex_kind::affine: c.factor_ = factor::affine; break;
			case scan_plan::lex_kind::doubled: c.factor_ = factor::doubled; break;
			case scan_plan::lex_kind::comment: c.factor_ = factor::comment; break;
			case scan_plan::lex_kind::fallback: c.factor_ = factor::fallback; break;
			default: c.factor_ = factor::table; break;
		}
		for (std::size_t s = 0; s < kMaxStates; ++s)
			for (std::size_t k = 0; k < kMaxClasses; ++k) {
				c.trans[s][k]  = dd.trans[s][k];
				c.masked[s][k] = dd.masked[s][k];
			}
		if (P.n_cells < kMaxCells)
			P.cells[P.n_cells++] = c;
	}

	// recursive rules -> stack cells (bracket pairs)
	for (std::uint8_t b = 0; b < sp.n_brackets && P.n_brackets < kMaxCells; ++b) {
		cell c;
		c.sch	= schema::stack;
		c.open	= sp.brackets[b].open;
		c.close = sp.brackets[b].close;
		if (P.n_cells < kMaxCells)
			P.cells[P.n_cells++] = c;
		P.opens[P.n_brackets]  = c.open;
		P.closes[P.n_brackets] = c.close;
		++P.n_brackets;
	}

	// structural atoms (punctuation)
	for (std::uint8_t a = 0; a < sp.n_punctuation && P.n_atoms < kMaxCells; ++a)
		P.atoms[P.n_atoms++] = sp.punctuation[a];

	// recursive rules that are not bracket-guarded but match the monoid pattern
	tree_scan<Grammar, typename Grammar::rules>::run(P);

	return P;
}

}  // namespace pars::core
