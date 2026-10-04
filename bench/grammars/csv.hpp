// bench/grammars/csv.hpp — RFC-4180 CSV declared through the pars DSL.
//
// `unquoted` is named as a lexical rule; `quoted` is a doubled-delimiter mode.
#pragma once

#include "pars/pars.hpp"

namespace pars::bench {

struct csv_grammar {
    struct file {};
    struct record {};
    struct field {};
    struct quoted {};
    struct unquoted {};

    using rules = type_list<file, record, field, quoted, unquoted>;
    using lexical_rules = type_list<quoted, unquoted>;
    using start = file;
    template <class> struct def;
};

template <> struct csv_grammar::def<csv_grammar::unquoted> {
    using type = star<seq<notp<cls<',', '\n', '\r', '"'>>, any>>;
};
template <> struct csv_grammar::def<csv_grammar::quoted> {
    using type = seq<lit<'"'>,
                     star<alt<seq<lit<'"'>, lit<'"'>>, seq<notp<cls<'"'>>, any>>>,
                     lit<'"'>>;
};
template <> struct csv_grammar::def<csv_grammar::field> {
    using type = alt<rule<csv_grammar::quoted>, rule<csv_grammar::unquoted>>;
};
template <> struct csv_grammar::def<csv_grammar::record> {
    using type = seq<rule<csv_grammar::field>, star<seq<lit<','>, rule<csv_grammar::field>>>>;
};
template <> struct csv_grammar::def<csv_grammar::file> {
    using type = seq<rule<csv_grammar::record>, star<seq<lit<'\n'>, rule<csv_grammar::record>>>,
                     opt<lit<'\n'>>>;
};

} // namespace pars::bench
