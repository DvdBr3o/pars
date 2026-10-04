// pars/compile/plan.hpp — frontend: grammar -> lowered scan plan.
//
// Shape-free derivation:
//   * lexical rules  -> DFA (Brzozowski derivatives, automaton.hpp); the mode
//     delimiter/escape and line-comment delimiter are read off the automaton.
//   * recursive rules-> bracket pairs from FIRST/LAST terminal sets (the
//     visibly-pushdown guard), not from syntactic position.
//   * structural punctuation -> standalone terminals of non-lexical rules.
#pragma once

#include "pars/compile/automaton.hpp"
#include "pars/dsl.hpp"
#include "pars/plan.hpp"

#include <array>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace pars::compile {

template<class Grammar, class Tag>
using rule_def_t = typename Grammar::template def<Tag>::type;

template<class G>
struct is_lit_node : std::false_type {};

template<char C>
struct is_lit_node<lit<C>> : std::true_type {
	static constexpr char value = C;
};

template<class G, class = void>
struct has_lexical : std::false_type {};

template<class G>
struct has_lexical<G, std::void_t<typename G::lexical_rules>> : std::true_type {};

// ---------------------------------------------------------------------------
// recursion
// ---------------------------------------------------------------------------
template<class Grammar, class Node, class Tag, class Visited = type_list<>>
struct mentions : std::false_type {};

template<class Grammar, class Tag2, class Tag, class... Visited>
struct mentions_rule {
	static constexpr bool value = []() constexpr -> bool {
		if constexpr (std::is_same_v<Tag2, Tag>)
			return true;
		else if constexpr (list_contains_v<Tag2, type_list<Visited...>>)
			return false;
		else
			return mentions<Grammar, rule_def_t<Grammar, Tag2>, Tag, type_list<Visited..., Tag2>>::
				value;
	}();
};

template<class Grammar, class Tag2, class Tag, class... Visited>
struct mentions<Grammar, rule<Tag2>, Tag, type_list<Visited...>> :
	std::bool_constant<mentions_rule<Grammar, Tag2, Tag, Visited...>::value> {};

template<class Grammar, class... Gs, class Tag, class... Visited>
struct mentions<Grammar, seq<Gs...>, Tag, type_list<Visited...>> :
	std::bool_constant<(mentions<Grammar, Gs, Tag, type_list<Visited...>>::value || ...)> {};

template<class Grammar, class... Gs, class Tag, class... Visited>
struct mentions<Grammar, alt<Gs...>, Tag, type_list<Visited...>> :
	std::bool_constant<(mentions<Grammar, Gs, Tag, type_list<Visited...>>::value || ...)> {};

template<class Grammar, class G, int Min, int Max, class Tag, class... Visited>
struct mentions<Grammar, rep<G, Min, Max>, Tag, type_list<Visited...>> :
	mentions<Grammar, G, Tag, type_list<Visited...>> {};

template<class Grammar, class G, class Tag, class... Visited>
struct mentions<Grammar, notp<G>, Tag, type_list<Visited...>> :
	mentions<Grammar, G, Tag, type_list<Visited...>> {};

template<class Grammar, class Tag>
inline constexpr bool is_recursive_v =
	mentions<Grammar, rule_def_t<Grammar, Tag>, Tag, type_list<>>::value;

// ---------------------------------------------------------------------------
// P0.3  notp soundness: only `seq<notp<cls<S>>, any>` is regular and supported.
// ---------------------------------------------------------------------------
template<class Grammar, class Node, class Visited = type_list<>>
struct notp_ok : std::true_type {};

template<class Grammar, class G, class Visited>
struct notp_ok<Grammar, notp<G>, Visited> : std::false_type {};

template<class Grammar, char... Cs, class Visited>
struct notp_ok<Grammar, seq<notp<cls<Cs...>>, any>, Visited> : std::true_type {};

template<class Grammar, class... Gs, class Visited>
struct notp_ok<Grammar, seq<Gs...>, Visited> :
	std::bool_constant<(notp_ok<Grammar, Gs, Visited>::value && ...)> {};

template<class Grammar, class... Gs, class Visited>
struct notp_ok<Grammar, alt<Gs...>, Visited> :
	std::bool_constant<(notp_ok<Grammar, Gs, Visited>::value && ...)> {};

template<class Grammar, class G, int Min, int Max, class Visited>
struct notp_ok<Grammar, rep<G, Min, Max>, Visited> : notp_ok<Grammar, G, Visited> {};

template<class Grammar, class Tag, class... Vs>
struct notp_ok<Grammar, rule<Tag>, type_list<Vs...>> {
	static constexpr bool value = []() constexpr -> bool {
		if constexpr (list_contains_v<Tag, type_list<Vs...>>)
			return true;
		else
			return notp_ok<Grammar, rule_def_t<Grammar, Tag>, type_list<Vs..., Tag>>::value;
	}();
};

// ---------------------------------------------------------------------------
// P0.2  optional explicit fallback rules
// ---------------------------------------------------------------------------
template<class G, class = void>
struct has_fallback : std::false_type {};

template<class G>
struct has_fallback<G, std::void_t<typename G::fallback_rules>> : std::true_type {};

template<class Grammar, class Tag>
constexpr bool is_fallback() {
	if constexpr (has_fallback<Grammar>::value)
		return list_contains_v<Tag, typename Grammar::fallback_rules>;
	else
		return false;
}

// ---------------------------------------------------------------------------
// P0.1  bracket guard: sound structural criterion (top level has exactly the
// two literal endpoints, distinct, with the recursion strictly inside).
// Well-nestedness is still validated at runtime by model/nest.hpp.
// ---------------------------------------------------------------------------
template<class Grammar, class Def>
struct bracket_guard {
	static constexpr bool ok   = false;
	static constexpr char open = 0, close = 0;
};

template<class Grammar, class... Gs>
struct bracket_guard<Grammar, seq<Gs...>> {
	static constexpr std::size_t N	   = sizeof...(Gs);
	static constexpr std::size_t nlits = (std::size_t {0} + ... + (is_lit_node<Gs>::value ? 1 : 0));

	static constexpr bool		 first_lit() noexcept {
		if constexpr (N == 0)
			return false;
		else
			return is_lit_node<nth_t<type_list<Gs...>, 0>>::value;
	}

	static constexpr bool last_lit() noexcept {
		if constexpr (N == 0)
			return false;
		else
			return is_lit_node<nth_t<type_list<Gs...>, N - 1>>::value;
	}

	static constexpr char first_c() noexcept {
		if constexpr (N == 0)
			return 0;
		else {
			using F = nth_t<type_list<Gs...>, 0>;
			if constexpr (is_lit_node<F>::value)
				return is_lit_node<F>::value;
			else
				return 0;
		}
	}

	static constexpr char last_c() noexcept {
		if constexpr (N == 0)
			return 0;
		else {
			using L = nth_t<type_list<Gs...>, N - 1>;
			if constexpr (is_lit_node<L>::value)
				return is_lit_node<L>::value;
			else
				return 0;
		}
	}

	static constexpr char open = first_c(), close = last_c();
	static constexpr bool ok =
		(N >= 3) && first_lit() && last_lit() && (nlits == 2) && (open != close);
};

// P0.2  direct self-reference count (cycle glue has none).
template<class Node, class Tag>
struct count_rule : std::integral_constant<int, 0> {};

template<class Tag, class Tag2>
struct count_rule<rule<Tag2>, Tag> :
	std::integral_constant<int, std::is_same_v<Tag, Tag2> ? 1 : 0> {};

template<class... Gs, class Tag>
struct count_rule<seq<Gs...>, Tag> :
	std::integral_constant<int, (count_rule<Gs, Tag>::value + ... + 0)> {};

template<class... Gs, class Tag>
struct count_rule<alt<Gs...>, Tag> :
	std::integral_constant<int, (count_rule<Gs, Tag>::value + ... + 0)> {};

template<class G, int Min, int Max, class Tag>
struct count_rule<rep<G, Min, Max>, Tag> : count_rule<G, Tag> {};

template<class G, class Tag>
struct count_rule<notp<G>, Tag> : count_rule<G, Tag> {};

// P0.2  per-rule support: recursion anchors must be bracket-guarded (R7 not yet
// implemented); cycle glue (no direct self reference) is handled semantically;
// lexical rules must use only supported notp forms.
template<class Grammar, class Tag>
constexpr bool rule_ok() {
	if constexpr (is_fallback<Grammar, Tag>())
		return true;
	else if constexpr (is_recursive_v<Grammar, Tag>)
		return bracket_guard<Grammar, rule_def_t<Grammar, Tag>>::ok
			|| (count_rule<rule_def_t<Grammar, Tag>, Tag>::value == 0);
	else if constexpr (has_lexical<Grammar>::value)
		return !list_contains_v<Tag, typename Grammar::lexical_rules>
			|| notp_ok<Grammar, rule_def_t<Grammar, Tag>>::value;
	else
		return notp_ok<Grammar, rule_def_t<Grammar, Tag>>::value;
}

template<class Grammar, class List>
struct all_ok;

template<class Grammar, class... Tags>
struct all_ok<Grammar, type_list<Tags...>> :
	std::bool_constant<(rule_ok<Grammar, Tags>() && ...)> {};

template<class Grammar>
inline constexpr bool is_supported = all_ok<Grammar, typename Grammar::rules>::value;

// Hard compile-time gate (P0.2).
template<class Grammar>
consteval void require_supported() {
	static_assert(
		is_supported<Grammar>,
		"[pars] grammar has a rule with no sound data-parallel lowering "
		"(unsupported recursion, or an unsupported notp form). Declare it "
		"in `fallback_rules` if it should run on a fallback backend."
	);
}

// ---------------------------------------------------------------------------
// nullability + FIRST/LAST terminal sets (for bracket derivation)
// ---------------------------------------------------------------------------
using term_set = std::array<bool, 256>;

template<class Grammar, class Node, class Visited = type_list<>>
struct nullable_node : std::false_type {};

template<class Grammar, class Visited>
struct nullable_node<Grammar, eps, Visited> : std::true_type {};

template<class Grammar, class... Gs, class Visited>
struct nullable_node<Grammar, seq<Gs...>, Visited> :
	std::bool_constant<(nullable_node<Grammar, Gs, Visited>::value && ...)> {};

template<class Grammar, class... Gs, class Visited>
struct nullable_node<Grammar, alt<Gs...>, Visited> :
	std::bool_constant<(nullable_node<Grammar, Gs, Visited>::value || ...)> {};

template<class Grammar, class G, int Min, int Max, class Visited>
struct nullable_node<Grammar, rep<G, Min, Max>, Visited> :
	std::bool_constant<(Min == 0) || nullable_node<Grammar, G, Visited>::value> {};

template<class Grammar, class G, class Visited>
struct nullable_node<Grammar, notp<G>, Visited> : std::true_type {};

template<class Grammar, class Tag, class... Vs>
struct nullable_node<Grammar, rule<Tag>, type_list<Vs...>> {
	static constexpr bool value = []() constexpr -> bool {
		if constexpr (list_contains_v<Tag, type_list<Vs...>>)
			return false;
		else
			return nullable_node<Grammar, rule_def_t<Grammar, Tag>, type_list<Vs..., Tag>>::value;
	}();
};

// Leading / trailing terminal byte sets of a node.
template<class Grammar, class Node, class Visited = type_list<>>
struct first_terms {
	static constexpr term_set run() noexcept { return {}; }
};

template<class Grammar, class Visited>
struct first_terms<Grammar, any, Visited> {
	static constexpr term_set run() noexcept {
		term_set s {};
		for (int i = 0; i < 256; ++i) s[i] = true;
		return s;
	}
};

template<class Grammar, char C, class Visited>
struct first_terms<Grammar, lit<C>, Visited> {
	static constexpr term_set run() noexcept {
		term_set s {};
		s[(unsigned char)C] = true;
		return s;
	}
};

template<class Grammar, char... Cs, class Visited>
struct first_terms<Grammar, cls<Cs...>, Visited> {
	static constexpr term_set run() noexcept {
		term_set s {};
		((s[(unsigned char)Cs] = true), ...);
		return s;
	}
};

template<class Grammar, class... Gs, class Visited>
struct first_terms<Grammar, seq<Gs...>, Visited> {
	static constexpr term_set run() noexcept {
		term_set out {};
		go<0>(out, false);
		return out;
	}

	template<std::size_t I>
	static constexpr void go(term_set& out, bool stopped) noexcept {
		if constexpr (I < sizeof...(Gs)) {
			if (!stopped) {
				using G	   = nth_t<type_list<Gs...>, I>;
				term_set f = first_terms<Grammar, G, Visited>::run();
				for (int i = 0; i < 256; ++i) out[i] = out[i] || f[i];
				go<I + 1>(out, !nullable_node<Grammar, G, Visited>::value);
			}
		}
	}
};

template<class Grammar, class... Gs, class Visited>
struct first_terms<Grammar, alt<Gs...>, Visited> {
	static constexpr term_set run() noexcept {
		term_set out {};
		(
			(void)[&] {
				term_set f = first_terms<Grammar, Gs, Visited>::run();
				for (int i = 0; i < 256; ++i) out[i] = out[i] || f[i];
			}(),
			...);
		return out;
	}
};

template<class Grammar, class G, int Min, int Max, class Visited>
struct first_terms<Grammar, rep<G, Min, Max>, Visited> {
	static constexpr term_set run() noexcept { return first_terms<Grammar, G, Visited>::run(); }
};

template<class Grammar, class G, class Visited>
struct first_terms<Grammar, notp<G>, Visited> {
	static constexpr term_set run() noexcept { return {}; }
};

template<class Grammar, class Tag, class... Vs>
struct first_terms<Grammar, rule<Tag>, type_list<Vs...>> {
	static constexpr term_set run() noexcept {
		if constexpr (list_contains_v<Tag, type_list<Vs...>>)
			return {};
		else
			return first_terms<Grammar, rule_def_t<Grammar, Tag>, type_list<Vs..., Tag>>::run();
	}
};

// ---------------------------------------------------------------------------
// lexical / structural split
// ---------------------------------------------------------------------------
template<class Grammar, class Tag>
constexpr bool is_lexical_rule() {
	if constexpr (has_lexical<Grammar>::value)
		return list_contains_v<Tag, typename Grammar::lexical_rules>;
	else
		return !is_recursive_v<Grammar, Tag>;
}

// ---------------------------------------------------------------------------
// automaton-derived lexical classification
// ---------------------------------------------------------------------------
struct lexical_info {
	int	 kind	= 0;  // 0 none, 1 mode, 2 comment
	char delim	= 0;
	char escape = 0;
};

template<class Grammar, class Tag>
constexpr lexical_info derive_lexical(const std::array<bool, 256>& structural) {
	arena				A	 = make_arena();
	const std::uint16_t root = reify<Grammar, rule_def_t<Grammar, Tag>>::run(A);
	dfa					D	 = build_dfa(A, root);
	if (!D.ok)
		return lexical_info {};
	// Unified mask semantics: only rules that actually mask (a non-start state
	// can reach accept through a structural byte) become modes/comments.
	const std::array<bool, kMaxDfaStates> mask = masking_states(D, structural);
	bool								  any  = false;
	for (bool b : mask) any = any || b;
	if (!any)
		return lexical_info {};

	lexical_info out {};
	const int	 n = D.states();
	if (n < 1)
		return out;
	int D0 = -1, count = 0;
	for (int a = 0; a < 256; ++a)
		if (D.next(0, a) >= 0) {
			D0 = a;
			++count;
		}
	if (count != 1 || D0 < 0)
		return out;
	const int S1 = D.next(0, D0);
	if (S1 <= 0)
		return out;
	out.delim = static_cast<char>(D0);

	if (D.accepting[S1]) {
		bool comment = true;
		for (int a = 0; a < 256; ++a) {
			int j = D.next(S1, a);
			if (j >= 0 && j != S1) {
				comment = false;
				break;
			}
		}
		if (comment) {
			out.kind = 2;
			return out;
		}
	}
	{
		int S2 = D.next(S1, D0);
		if (S2 >= 0 && S2 != S1 && D.next(S2, D0) == S1) {
			out.kind   = 1;
			out.escape = static_cast<char>(D0);
			return out;
		}
	}
	for (int E = 0; E < 256; ++E) {
		if (E == D0)
			continue;
		int S2 = D.next(S1, E);
		if (S2 < 0 || S2 == S1)
			continue;
		bool absorb = true;
		for (int a = 0; a < 256; ++a) {
			int j = D.next(S2, a);
			if (j >= 0 && j != S1) {
				absorb = false;
				break;
			}
		}
		if (absorb) {
			out.kind   = 1;
			out.escape = static_cast<char>(E);
			return out;
		}
	}
	if (!D.accepting[S1]) {
		const int q	   = D.next(S1, D0);
		bool	  body = false;
		for (int a = 0; a < 256; ++a) {
			if (a == D0)
				continue;
			if (D.next(S1, a) >= 0) {
				body = true;
				break;
			}
		}
		if (q >= 0 && D.accepting[q] && body) {
			out.kind   = 1;
			out.escape = 0;
			return out;
		}
	}
	return out;
}

// ---------------------------------------------------------------------------
// plan assembly
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// P1: per-rule automaton descriptor (transition-monoid execution semantics).
// ---------------------------------------------------------------------------
template<class Grammar, class Tag>
constexpr scan_plan::lex_desc make_desc(const std::array<bool, 256>& structural) {
	using lex_desc = scan_plan::lex_desc;
	lex_desc			out {};
	arena				A	 = make_arena();
	const std::uint16_t root = reify<Grammar, rule_def_t<Grammar, Tag>>::run(A);
	dfa					D	 = build_dfa(A, root);
	if (!D.ok) {
		out.ok	 = false;
		out.kind = scan_plan::lex_kind::fallback;
		return out;
	}
	const std::array<bool, kMaxDfaStates> mask = masking_states(D, structural);
	bool								  any  = false;
	for (bool b : mask) any = any || b;
	if (!any) {
		out.ok = false;
		return out;
	}

	const int n	 = D.n_states;
	const int nc = (int)D.n_reps;
	if (n > (int)scan_plan::kMaxDfaStates || nc > (int)scan_plan::kMaxDfaClasses) {
		out.ok	 = false;  // cap reached -> diagnostic / fallback
		out.kind = scan_plan::lex_kind::fallback;
		return out;
	}
	out.n_states  = (std::uint8_t)n;
	out.n_classes = (std::uint8_t)nc;
	out.start	  = (std::uint8_t)D.start;
	// fold the lexer reset into the table: on a dead token transition, restart a
	// token if the byte can start one, else go back to start.
	for (int s = 0; s < n; ++s)
		for (int c = 0; c < nc; ++c) {
			const int tok	 = D.trans[s][c];
			out.masked[s][c] = (s != D.start && tok >= 0) ? 1 : 0;
			int nd			 = tok;
			if (nd < 0) {
				int n0 = D.trans[D.start][c];
				nd	   = (n0 >= 0) ? n0 : D.start;
			}
			out.trans[s][c] = (std::uint8_t)nd;
		}
	for (int b = 0; b < 256; ++b) out.rep_of[b] = (std::uint8_t)D.rep_of[b];

	lexical_info info = derive_lexical<Grammar, Tag>(structural);
	if (info.kind == 1) {
		out.delim = info.delim;
		if (info.escape == info.delim) {
			out.kind   = scan_plan::lex_kind::doubled;
			out.escape = 0;
		} else {
			out.kind   = scan_plan::lex_kind::affine;
			out.escape = info.escape;
		}
	} else if (info.kind == 2) {
		out.kind  = scan_plan::lex_kind::comment;
		out.delim = info.delim;
	} else {
		out.kind = scan_plan::lex_kind::table;
	}
	out.ok = true;
	return out;
}

template<class Grammar, class List>
struct lexical_scan;

template<class Grammar, class... Tags>
struct lexical_scan<Grammar, type_list<Tags...>> {
	static constexpr void run(scan_plan& S, const std::array<bool, 256>& structural) noexcept {
		(one<Tags>(S, structural), ...);
	}

	template<class Tag>
	static constexpr void one(scan_plan& S, const std::array<bool, 256>& structural) noexcept {
		if constexpr (is_lexical_rule<Grammar, Tag>()) {
			if constexpr (!notp_ok<Grammar, rule_def_t<Grammar, Tag>>::value)
				return;
			term_set f	 = first_terms<Grammar, rule_def_t<Grammar, Tag>>::run();
			int		 cnt = 0;
			for (int i = 0; i < 256; ++i)
				if (f[i])
					++cnt;
			if (cnt != 1)
				return;
			lexical_info info = derive_lexical<Grammar, Tag>(structural);
			if (info.kind == 1 && S.n_modes < kMaxModes) {
				mode_spec m {info.delim, info.escape};
				bool	  ex = false;
				for (std::size_t i = 0; i < S.n_modes; ++i)
					if (S.modes[i] == m)
						ex = true;
				if (!ex)
					S.modes[S.n_modes++] = m;
			} else if (info.kind == 2 && S.n_comments < kMaxComments) {
				char d	= info.delim;
				bool ex = false;
				for (std::size_t i = 0; i < S.n_comments; ++i)
					if (S.comments[i] == d)
						ex = true;
				if (!ex)
					S.comments[S.n_comments++] = d;
			}
			// P1 execution semantics: append the automaton descriptor.
			if (S.n_descs < kMaxModes) {
				scan_plan::lex_desc d = make_desc<Grammar, Tag>(structural);
				if (d.ok)
					S.descs[S.n_descs++] = d;
			}
		}
	}
};

template<class Grammar, class List>
struct bracket_scan;

template<class Grammar, class... Tags>
struct bracket_scan<Grammar, type_list<Tags...>> {
	static constexpr void run(scan_plan& S) noexcept { (one<Tags>(S), ...); }

	template<class Tag>
	static constexpr void one(scan_plan& S) noexcept {
		if constexpr (is_recursive_v<Grammar, Tag>) {
			using Def = rule_def_t<Grammar, Tag>;
			if constexpr (bracket_guard<Grammar, Def>::ok) {
				constexpr bracket_spec b {
					bracket_guard<Grammar, Def>::open,
					bracket_guard<Grammar, Def>::close
				};
				if (S.n_brackets < kMaxBrackets) {
					bool ex = false;
					for (std::size_t i = 0; i < S.n_brackets; ++i)
						if (S.brackets[i] == b)
							ex = true;
					if (!ex)
						S.brackets[S.n_brackets++] = b;
				}
			}
		}
	}
};

// standalone literal collection for punctuation
using lit_mask = std::array<bool, 256>;

template<class Grammar, class Node, class Visited = type_list<>>
struct collect_lits {
	static constexpr void run(lit_mask&) noexcept {}
};

template<class Grammar, char C, class Visited>
struct collect_lits<Grammar, lit<C>, Visited> {
	static constexpr void run(lit_mask& o) noexcept { o[(unsigned char)C] = true; }
};

template<class Grammar, class... Gs, class Visited>
struct collect_lits<Grammar, seq<Gs...>, Visited> {
	static constexpr void run(lit_mask& o) noexcept {
		if constexpr ((is_lit_node<Gs>::value && ...))
			return;	 // keyword
		(elem<Gs>(o), ...);
	}

	template<class G>
	static constexpr void elem(lit_mask& o) noexcept {
		if constexpr (is_lit_node<G>::value)
			o[(unsigned char)is_lit_node<G>::value] = true;
		else
			collect_lits<Grammar, G, Visited>::run(o);
	}
};

template<class Grammar, class... Gs, class Visited>
struct collect_lits<Grammar, alt<Gs...>, Visited> {
	static constexpr void run(lit_mask& o) noexcept {
		(collect_lits<Grammar, Gs, Visited>::run(o), ...);
	}
};

template<class Grammar, class G, int Min, int Max, class Visited>
struct collect_lits<Grammar, rep<G, Min, Max>, Visited> {
	static constexpr void run(lit_mask& o) noexcept { collect_lits<Grammar, G, Visited>::run(o); }
};

template<class Grammar, class G, class Visited>
struct collect_lits<Grammar, notp<G>, Visited> {
	static constexpr void run(lit_mask& o) noexcept { collect_lits<Grammar, G, Visited>::run(o); }
};

template<class Grammar, class Tag, class... Vs>
struct collect_lits<Grammar, rule<Tag>, type_list<Vs...>> {
	static constexpr void run(lit_mask& o) noexcept {
		if constexpr (list_contains_v<Tag, type_list<Vs...>>)
			return;
		else if constexpr (is_lexical_rule<Grammar, Tag>())
			return;
		else
			collect_lits<Grammar, rule_def_t<Grammar, Tag>, type_list<Vs..., Tag>>::run(o);
	}
};

template<class Grammar, class Rules>
struct collect_all;

template<class Grammar, class... Tags>
struct collect_all<Grammar, type_list<Tags...>> {
	static constexpr void run(lit_mask& o) noexcept { (maybe<Tags>(o), ...); }

	template<class Tag>
	static constexpr void maybe(lit_mask& o) noexcept {
		if constexpr (!is_lexical_rule<Grammar, Tag>())
			collect_lits<Grammar, rule_def_t<Grammar, Tag>, type_list<Tag>>::run(o);
	}
};

template<class Grammar>
[[nodiscard]] constexpr scan_plan make_scan_plan() noexcept {
	scan_plan S {};
	bracket_scan<Grammar, typename Grammar::rules>::run(S);

	// Structural byte set (brackets + standalone terminals of non-lexical
	// rules) drives the unified masking analysis.
	lit_mask lit {};
	collect_all<Grammar, typename Grammar::rules>::run(lit);
	std::array<bool, 256> structural {};
	for (std::size_t i = 0; i < S.n_brackets; ++i) {
		structural[(unsigned char)S.brackets[i].open]  = true;
		structural[(unsigned char)S.brackets[i].close] = true;
	}
	for (int c = 0; c < 256; ++c)
		if (lit[c])
			structural[c] = true;

	lexical_scan<Grammar, typename Grammar::rules>::run(S, structural);

	lit_mask exclude {};
	for (std::size_t i = 0; i < S.n_brackets; ++i) {
		exclude[(unsigned char)S.brackets[i].open]	= true;
		exclude[(unsigned char)S.brackets[i].close] = true;
	}
	for (std::size_t i = 0; i < S.n_modes; ++i) {
		exclude[(unsigned char)S.modes[i].delim] = true;
		if (S.modes[i].escape)
			exclude[(unsigned char)S.modes[i].escape] = true;
	}
	for (std::size_t i = 0; i < S.n_comments; ++i) exclude[(unsigned char)S.comments[i]] = true;
	std::size_t np = 0;
	for (int c = 0; c < 256 && np < kMaxPunct; ++c)
		if (lit[c] && !exclude[c])
			S.punctuation[np++] = static_cast<char>(c);
	S.n_punctuation = static_cast<std::uint8_t>(np);
	return S;
}

template<class Grammar, class Tag>
constexpr bool is_supported_rule() {
	return rule_ok<Grammar, Tag>();
}

}  // namespace pars::compile
