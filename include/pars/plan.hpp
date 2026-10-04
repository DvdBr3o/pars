// pars/plan.hpp — the lowered scan plan (the output of the compiler, the input
// of every scan-algebra backend).
//
// It is deliberately tiny and format-agnostic.  The scan algebra lowers a grammar to:
//   * a character classifier  (R1)  — derived by the backend from `punct`,
//     `brackets`, `modes`,
//   * a set of finite-state transducers (R4) — one per lexical mode,
//   * bracket pairs (R6),
//   * separators (punctuation).
//
// The plan is a C++20 structural type so it can be used as an NTTP.
#pragma once

#include <array>
#include <cstdint>

namespace pars {

constexpr std::size_t kMaxPunct	   = 8;
constexpr std::size_t kMaxBrackets = 8;
constexpr std::size_t kMaxModes	   = 8;
constexpr std::size_t kMaxComments = 4;

struct bracket_spec {
	char				  open												   = 0;
	char				  close												   = 0;
	friend constexpr bool operator==(const bracket_spec&, const bracket_spec&) = default;
};

// escape == 0      : no escaping (TOML literal strings)
// escape == delim  : doubled delimiter (CSV "" )
// otherwise        : prefix escape (JSON/TOML basic strings)
struct mode_spec {
	char				  delim											 = 0;
	char				  escape										 = 0;
	friend constexpr bool operator==(const mode_spec&, const mode_spec&) = default;
};

struct scan_plan {
	std::array<char, kMaxPunct>			   punctuation {};
	std::array<bracket_spec, kMaxBrackets> brackets {};
	std::array<mode_spec, kMaxModes>	   modes {};
	std::array<char, kMaxComments>		   comments {};	 // line-comment delimiters
	std::uint8_t						   n_punctuation = 0;
	std::uint8_t						   n_brackets	 = 0;
	std::uint8_t						   n_modes		 = 0;
	std::uint8_t						   n_comments	 = 0;

	// P1: per lexical-masking-rule automaton descriptor (the execution
	// semantics).  `kind` records the provable factorization used for
	// specialization; `table` is the general transition-monoid automaton.
	static constexpr std::size_t kMaxDfaStates	= 8;
	static constexpr std::size_t kMaxDfaClasses = 8;

	enum class lex_kind : std::uint8_t { affine, comment, doubled, table, fallback };

	struct lex_desc {
		lex_kind	 kind	   = lex_kind::table;
		std::uint8_t n_states  = 0;
		std::uint8_t n_classes = 0;
		std::uint8_t start	   = 0;
		bool		 ok		   = true;
		// delimiter/escape for the affine fast path (GPU)
		char delim = 0, escape = 0;
		// byte -> class
		std::array<std::uint8_t, 256> rep_of {};
		// state x class -> next state (reset already folded in)
		std::array<std::array<std::uint8_t, kMaxDfaClasses>, kMaxDfaStates> trans {};
		// per-transition mask bit: this byte is consumed by an in-progress token
		std::array<std::array<std::uint8_t, kMaxDfaClasses>, kMaxDfaStates> masked {};

		friend constexpr bool operator==(const lex_desc&, const lex_desc&) = default;
	};

	std::array<lex_desc, kMaxModes> descs {};
	std::uint8_t					n_descs										   = 0;

	friend constexpr bool			operator==(const scan_plan&, const scan_plan&) = default;
};

}  // namespace pars
