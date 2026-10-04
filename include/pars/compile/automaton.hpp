// pars/compile/automaton.hpp — the unified, shape-free frontend.
//
// Every lexical rule body is a regular language; we compile it to a DFA by
// Brzozowski derivatives and derive masking from the automaton alone.  The
// construction uses *no dynamic allocation* (fixed-capacity arenas / arrays) so
// it can be constant-evaluated by strict frontends (notably nvcc's host pass).
#pragma once

#include "pars/dsl.hpp"

#include <array>
#include <cstdint>

namespace pars::compile {

// ---------------------------------------------------------------------------
// Value-level regular expression, stored in a fixed-capacity arena.
// ---------------------------------------------------------------------------
enum class rk : std::uint8_t { empty, eps, any, ch, set, cat, alt, star };

constexpr std::size_t kArenaCap		= 8192;
constexpr std::size_t kMaxKids		= 16;
constexpr std::size_t kMaxPreds		= 32;
constexpr std::size_t kMaxDfaStates = 16;
constexpr std::size_t kMaxDfaReps	= 16;

struct rnode {
	rk					  k	  = rk::empty;
	unsigned char		  c	  = 0;
	std::uint16_t		  min = 0, max = 0;
	std::array<bool, 256> s {};
};

struct arena {
	std::array<rnode, kArenaCap>							   nd {};
	std::array<std::array<std::uint16_t, kMaxKids>, kArenaCap> kid {};
	std::array<std::uint8_t, kArenaCap>						   nk {};
	std::uint32_t											   n		= 0;
	std::uint16_t											   empty_id = 0, eps_id = 0, any_id = 0;
	bool													   overflow = false;

	[[nodiscard]] constexpr const rnode& operator[](std::uint16_t i) const { return nd[i]; }
};

[[nodiscard]] constexpr std::uint16_t mk(arena& A, rnode r) {
	if (A.n >= kArenaCap) {
		A.overflow = true;
		return 0;
	}
	A.nd[A.n] = r;
	A.nk[A.n] = 0;
	return static_cast<std::uint16_t>(A.n++);
}

[[nodiscard]] constexpr std::uint16_t mk_raw(arena& A, rk k) {
	rnode r;
	r.k = k;
	return mk(A, r);
}

[[nodiscard]] constexpr arena make_arena() {
	arena A {};
	A.empty_id = mk_raw(A, rk::empty);
	A.eps_id   = mk_raw(A, rk::eps);
	A.any_id   = mk_raw(A, rk::any);
	return A;
}

[[nodiscard]] constexpr bool eq(const arena& A, std::uint16_t a, std::uint16_t b) {
	if (a == b)
		return true;
	const rnode& x = A[a];
	const rnode& y = A[b];
	if (x.k != y.k)
		return false;
	switch (x.k) {
		case rk::empty:
		case rk::eps:
		case rk::any: return true;
		case rk::ch: return x.c == y.c;
		case rk::set: return x.s == y.s;
		case rk::cat:
			{
				if (A.nk[a] != A.nk[b])
					return false;
				for (int i = 0; i < A.nk[a]; ++i)
					if (!eq(A, A.kid[a][i], A.kid[b][i]))
						return false;
				return true;
			}
		case rk::alt:
			{
				if (A.nk[a] != A.nk[b])
					return false;
				std::array<bool, kMaxKids> used {};
				for (int i = 0; i < A.nk[a]; ++i) {
					bool found = false;
					for (int j = 0; j < A.nk[b]; ++j)
						if (!used[j] && eq(A, A.kid[a][i], A.kid[b][j])) {
							used[j] = true;
							found	= true;
							break;
						}
					if (!found)
						return false;
				}
				return true;
			}
		case rk::star: return eq(A, A.kid[a][0], A.kid[b][0]);
	}
	return false;
}

// canonical constructors -----------------------------------------------------
[[nodiscard]] constexpr std::uint16_t r_cat(arena& A, std::uint16_t a, std::uint16_t b) {
	if (A[a].k == rk::empty || A[b].k == rk::empty)
		return A.empty_id;
	std::array<std::uint16_t, kMaxKids> out {};
	int									n	 = 0;
	auto								push = [&](std::uint16_t x) {
		if (A[x].k == rk::eps)
			return;
		if (A[x].k == rk::cat) {
			for (int i = 0; i < A.nk[x]; ++i)
				if (n < (int)kMaxKids)
					out[n++] = A.kid[x][i];
		} else if (n < (int)kMaxKids)
			out[n++] = x;
	};
	push(a);
	push(b);
	if (n == 0)
		return A.eps_id;
	if (n == 1)
		return out[0];
	std::uint16_t id = mk_raw(A, rk::cat);
	A.nk[id]		 = (std::uint8_t)n;
	for (int i = 0; i < n; ++i) A.kid[id][i] = out[i];
	return id;
}

[[nodiscard]] constexpr std::uint16_t r_alt(arena& A, std::uint16_t a, std::uint16_t b) {
	std::array<std::uint16_t, kMaxKids> tmp {};
	int									n	 = 0;
	auto								push = [&](std::uint16_t x) {
		if (A[x].k == rk::empty)
			return;
		if (A[x].k == rk::alt)
			for (int i = 0; i < A.nk[x]; ++i) tmp[n++] = A.kid[x][i];
		else
			tmp[n++] = x;
	};
	push(a);
	push(b);
	std::array<std::uint16_t, kMaxKids> out {};
	int									m = 0;
	for (int i = 0; i < n; ++i) {
		bool dup = false;
		for (int j = 0; j < m; ++j)
			if (eq(A, out[j], tmp[i])) {
				dup = true;
				break;
			}
		if (!dup)
			out[m++] = tmp[i];
	}
	if (m == 0)
		return A.empty_id;
	if (m == 1)
		return out[0];
	std::uint16_t id = mk_raw(A, rk::alt);
	A.nk[id]		 = (std::uint8_t)m;
	for (int i = 0; i < m; ++i) A.kid[id][i] = out[i];
	return id;
}

[[nodiscard]] constexpr std::uint16_t r_star(arena& A, std::uint16_t a) {
	if (A[a].k == rk::empty || A[a].k == rk::eps)
		return A.eps_id;
	if (A[a].k == rk::star)
		return a;
	std::uint16_t id = mk_raw(A, rk::star);
	A.nk[id]		 = 1;
	A.kid[id][0]	 = a;
	return id;
}

[[nodiscard]] constexpr std::uint16_t r_cat3(
	arena& A, std::uint16_t a, std::uint16_t b, std::uint16_t c
) {
	return r_cat(A, r_cat(A, a, b), c);
}

// nullable / derivative ------------------------------------------------------
[[nodiscard]] constexpr bool nullable(const arena& A, std::uint16_t id) {
	switch (A[id].k) {
		case rk::eps: return true;
		case rk::empty:
		case rk::any:
		case rk::ch:
		case rk::set: return false;
		case rk::star: return true;
		case rk::alt:
			{
				for (int i = 0; i < A.nk[id]; ++i)
					if (nullable(A, A.kid[id][i]))
						return true;
				return false;
			}
		case rk::cat:
			{
				for (int i = 0; i < A.nk[id]; ++i)
					if (!nullable(A, A.kid[id][i]))
						return false;
				return true;
			}
	}
	return false;
}

[[nodiscard]] constexpr std::uint16_t der(arena& A, std::uint16_t id, unsigned char c) {
	switch (A[id].k) {
		case rk::empty:
		case rk::eps: return A.empty_id;
		case rk::any: return A.eps_id;
		case rk::ch: return (c == A[id].c) ? A.eps_id : A.empty_id;
		case rk::set: return A[id].s[c] ? A.eps_id : A.empty_id;
		case rk::alt:
			{
				std::uint16_t acc = A.empty_id;
				for (int i = 0; i < A.nk[id]; ++i) acc = r_alt(A, acc, der(A, A.kid[id][i], c));
				return acc;
			}
		case rk::cat:
			{
				if (A.nk[id] == 0)
					return A.empty_id;
				const std::uint16_t first = A.kid[id][0];
				std::uint16_t		rest  = A.eps_id;
				for (int i = 1; i < A.nk[id]; ++i) rest = r_cat(A, rest, A.kid[id][i]);
				std::uint16_t acc = r_cat(A, der(A, first, c), rest);
				if (nullable(A, first))
					acc = r_alt(A, acc, der(A, rest, c));
				return acc;
			}
		case rk::star: return r_cat(A, der(A, A.kid[id][0], c), id);
	}
	return A.empty_id;
}

// character predicates -------------------------------------------------------
[[nodiscard]] constexpr void collect_preds(
	const arena& A, std::uint16_t id, std::array<std::array<bool, 256>, kMaxPreds>& p,
	std::size_t& np
) {
	if (A[id].k == rk::ch) {
		if (np < kMaxPreds) {
			std::array<bool, 256> s {};
			s[A[id].c] = true;
			p[np++]	   = s;
		}
	} else if (A[id].k == rk::set) {
		if (np < kMaxPreds)
			p[np++] = A[id].s;
	}
	for (int i = 0; i < A.nk[id]; ++i) collect_preds(A, A.kid[id][i], p, np);
}

// DFA (fixed capacity) -------------------------------------------------------
struct dfa {
	std::array<std::array<int, kMaxDfaReps>, kMaxDfaStates> trans {};
	std::array<bool, kMaxDfaStates>							accepting {};
	std::array<std::uint16_t, kMaxDfaStates>				sid {};
	std::array<std::uint8_t, kMaxDfaStates>					nkids {};
	std::uint8_t											n_states = 0;
	std::array<int, 256>									rep_of {};
	std::array<int, kMaxDfaReps>							reps {};
	std::uint8_t											n_reps = 0;
	int														start  = 0;
	bool													ok	   = true;

	[[nodiscard]] constexpr int next(int s, int byte) const { return trans[s][rep_of[byte]]; }

	[[nodiscard]] constexpr int states() const { return n_states; }
};

[[nodiscard]] constexpr dfa build_dfa(arena& A, std::uint16_t root) {
	dfa D;
	for (int s = 0; s < (int)kMaxDfaStates; ++s)
		for (int c = 0; c < (int)kMaxDfaReps; ++c) D.trans[s][c] = -1;

	std::array<std::array<bool, 256>, kMaxPreds> preds {};
	std::size_t									 np = 0;
	collect_preds(A, root, preds, np);
	for (int b = 0; b < 256; ++b) {
		int found = -1;
		for (int j = 0; j < D.n_reps && found < 0; ++j) {
			bool same = true;
			for (std::size_t p = 0; p < np; ++p)
				if (preds[p][b] != preds[p][D.reps[j]]) {
					same = false;
					break;
				}
			if (same)
				found = j;
		}
		if (found < 0) {
			if (D.n_reps >= kMaxDfaReps) {
				D.ok		= false;
				D.rep_of[b] = 0;
				continue;
			}
			D.reps[D.n_reps] = b;
			found			 = D.n_reps++;
		}
		D.rep_of[b] = found;
	}

	D.start		   = 0;
	D.sid[0]	   = root;
	D.accepting[0] = nullable(A, root);
	D.n_states	   = 1;
	for (int s = 0; s < D.n_states; ++s) {
		for (int c = 0; c < D.n_reps; ++c) {
			const std::uint16_t nd = der(A, D.sid[s], static_cast<unsigned char>(D.reps[c]));
			if (A[nd].k == rk::empty)
				continue;
			int found = -1;
			for (int t = 0; t < D.n_states; ++t)
				if (eq(A, D.sid[t], nd)) {
					found = t;
					break;
				}
			if (found < 0) {
				if (D.n_states >= kMaxDfaStates) {
					D.ok = false;
					continue;
				}
				found					= D.n_states;
				D.sid[D.n_states]		= nd;
				D.accepting[D.n_states] = nullable(A, nd);
				++D.n_states;
			}
			D.trans[s][c] = found;
		}
	}
	return D;
}

[[nodiscard]] constexpr std::array<bool, kMaxDfaStates> reach_accept(const dfa& D) {
	std::array<bool, kMaxDfaStates> can {};
	for (int i = 0; i < D.n_states; ++i) can[i] = D.accepting[i];
	bool changed = true;
	while (changed) {
		changed = false;
		for (int i = 0; i < D.n_states; ++i) {
			if (can[i])
				continue;
			for (int c = 0; c < D.n_reps; ++c) {
				const int k = D.trans[i][c];
				if (k >= 0 && can[k]) {
					can[i]	= true;
					changed = true;
					break;
				}
			}
		}
	}
	return can;
}

// Masking states: non-start states with a path to accept through a structural
// byte.  Used to decide whether a lexical rule participates in masking.
[[nodiscard]] constexpr std::array<bool, kMaxDfaStates> masking_states(
	const dfa& D, const std::array<bool, 256>& structural
) {
	std::array<bool, kMaxDfaStates> mask {};
	const auto						can = reach_accept(D);
	for (int i = 0; i < D.n_states; ++i)
		for (int b = 0; b < 256; ++b) {
			if (!structural[b])
				continue;
			const int k = D.next(i, b);
			if (k >= 0 && can[k]) {
				mask[i] = true;
				break;
			}
		}
	bool changed = true;
	while (changed) {
		changed = false;
		for (int i = 0; i < D.n_states; ++i) {
			if (mask[i])
				continue;
			for (int c = 0; c < D.n_reps; ++c) {
				const int k = D.trans[i][c];
				if (k >= 0 && mask[k]) {
					mask[i] = true;
					changed = true;
					break;
				}
			}
		}
	}
	mask[D.start] = false;
	return mask;
}

// ---------------------------------------------------------------------------
// Type -> arena node id
// ---------------------------------------------------------------------------
template<class Grammar, class Node, class Visited = type_list<>>
struct reify {
	static constexpr std::uint16_t run(arena& A) { return A.empty_id; }
};

template<class Grammar, class Visited>
struct reify<Grammar, eps, Visited> {
	static constexpr std::uint16_t run(arena& A) { return A.eps_id; }
};

template<class Grammar, class Visited>
struct reify<Grammar, any, Visited> {
	static constexpr std::uint16_t run(arena& A) { return A.any_id; }
};

template<class Grammar, char C, class Visited>
struct reify<Grammar, lit<C>, Visited> {
	static constexpr std::uint16_t run(arena& A) {
		rnode r;
		r.k = rk::ch;
		r.c = (unsigned char)C;
		return mk(A, r);
	}
};

template<class Grammar, char... Cs, class Visited>
struct reify<Grammar, cls<Cs...>, Visited> {
	static constexpr std::uint16_t run(arena& A) {
		rnode r;
		r.k = rk::set;
		((r.s[(unsigned char)Cs] = true), ...);
		return mk(A, r);
	}
};

// seq<notp<cls<S>>, any> == one byte not in S
template<class Grammar, char... Cs, class Visited>
struct reify<Grammar, seq<notp<cls<Cs...>>, any>, Visited> {
	static constexpr std::uint16_t run(arena& A) {
		rnode r;
		r.k = rk::set;
		for (int i = 0; i < 256; ++i) r.s[i] = true;
		((r.s[(unsigned char)Cs] = false), ...);
		return mk(A, r);
	}
};

template<class Grammar, class... Gs, class Visited>
struct reify<Grammar, seq<Gs...>, Visited> {
	static constexpr std::uint16_t run(arena& A) {
		std::array<std::uint16_t, sizeof...(Gs)> ids {reify<Grammar, Gs, Visited>::run(A)...};
		std::uint16_t							 acc = A.eps_id;
		for (std::size_t i = 0; i < ids.size(); ++i) acc = r_cat(A, acc, ids[i]);
		return acc;
	}
};

template<class Grammar, class... Gs, class Visited>
struct reify<Grammar, alt<Gs...>, Visited> {
	static constexpr std::uint16_t run(arena& A) {
		std::array<std::uint16_t, sizeof...(Gs)> ids {reify<Grammar, Gs, Visited>::run(A)...};
		std::uint16_t							 acc = A.empty_id;
		for (std::size_t i = 0; i < ids.size(); ++i) acc = r_alt(A, acc, ids[i]);
		return acc;
	}
};

template<class Grammar, class G, int Min, int Max, class Visited>
struct reify<Grammar, rep<G, Min, Max>, Visited> {
	static constexpr std::uint16_t run(arena& A) {
		std::uint16_t acc = A.eps_id;
		for (int i = 0; i < Min; ++i) acc = r_cat(A, acc, reify<Grammar, G, Visited>::run(A));
		if (Max < 0)
			acc = r_cat(A, acc, r_star(A, reify<Grammar, G, Visited>::run(A)));
		else
			for (int i = Min; i < Max; ++i)
				acc = r_cat(A, acc, r_alt(A, reify<Grammar, G, Visited>::run(A), A.eps_id));
		return acc;
	}
};

template<class Grammar, class G, class Visited>
struct reify<Grammar, notp<G>, Visited> {
	static constexpr std::uint16_t run(arena& A) { return A.empty_id; }
};

template<class Grammar, class Tag, class... Vs>
struct reify<Grammar, rule<Tag>, type_list<Vs...>> {
	static constexpr std::uint16_t run(arena& A) {
		if constexpr (list_contains_v<Tag, type_list<Vs...>>)
			return A.any_id;
		else
			return reify<
				Grammar,
				typename Grammar::template def<Tag>::type,
				type_list<Vs..., Tag>>::run(A);
	}
};

}  // namespace pars::compile
