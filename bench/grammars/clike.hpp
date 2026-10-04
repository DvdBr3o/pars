// bench/grammars/clike.hpp — a tiny C-like token grammar whose block comment is
// NOT one of the four legacy templates.  It must be handled by the general
// per-rule automaton (Table) path, proving the executor covers arbitrary
// regular lexical rule bodies.
#pragma once

#include "pars/pars.hpp"

namespace pars::bench {

struct clike_grammar {
    struct file {};
    struct ws {};
    struct ident {};
    struct line_comment {};
    struct block_comment {};
    struct code {};
    struct block {};

    using rules = type_list<file, ws, ident, line_comment, block_comment, code, block>;
    using lexical_rules = type_list<ws, ident, line_comment, block_comment>;
    using start = file;
    template <class> struct def;
};

template <> struct clike_grammar::def<clike_grammar::ws> {
    using type = star<cls<' ', '\t', '\n', '\r'>>;
};
template <> struct clike_grammar::def<clike_grammar::ident> {
    using type = plus<cls<'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm',
                          'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
                          'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M',
                          'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
                          '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '_'>>;
};
// // ... to end of line
template <> struct clike_grammar::def<clike_grammar::line_comment> {
    using type = seq<lit<'/'>, lit<'/'>, star<seq<notp<cls<'\n'>>, any>>>;
};
// /* ... */  :  the standard regex  /\*([^*]|\*+[^*/])*\*+/
template <> struct clike_grammar::def<clike_grammar::block_comment> {
    using type = seq<lit<'/'>, lit<'*'>,
                     star<alt<seq<notp<cls<'*'>>, any>,
                              seq<plus<lit<'*'>>, seq<notp<cls<'*', '/'>>, any>>>>,
                     plus<lit<'*'>>, lit<'/'>>;
};
template <> struct clike_grammar::def<clike_grammar::block> {
    using type = seq<lit<'{'>, rule<clike_grammar::code>, lit<'}'>>;
};
template <> struct clike_grammar::def<clike_grammar::code> {
    using type = star<alt<rule<clike_grammar::ws>, rule<clike_grammar::block_comment>,
                          rule<clike_grammar::line_comment>, rule<clike_grammar::ident>,
                          lit<';'>, rule<clike_grammar::block>>>;
};
template <> struct clike_grammar::def<clike_grammar::file> {
    using type = rule<clike_grammar::code>;
};

} // namespace pars::bench
