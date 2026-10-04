// bench/grammars/toml.hpp — a TOML core grammar declared via the pars DSL.
// (Multiline strings are omitted; single-line basic/literal strings and '#'
// comments exercise multi-mode + comment derivation.)
#pragma once

#include "pars/pars.hpp"

namespace pars::bench {

struct toml_grammar {
    struct document {};
    struct ws {};
    struct comment {};
    struct statement {};
    struct keyval {};
    struct table {};
    struct array_table {};
    struct key {};
    struct dotted_key {};
    struct value {};
    struct basic_string {};
    struct literal_string {};
    struct bare {};
    struct array {};
    struct inline_table {};

    using rules = type_list<document, ws, comment, statement, keyval, table, array_table,
                            key, dotted_key, value, basic_string, literal_string,
                            bare, array, inline_table>;
    using lexical_rules = type_list<ws, comment, basic_string, literal_string, bare>;
    using start = document;
    template <class> struct def;
};

template <> struct toml_grammar::def<toml_grammar::ws> {
    using type = star<cls<' ', '\t'>>;
};
template <> struct toml_grammar::def<toml_grammar::comment> {
    using type = seq<lit<'#'>, star<seq<notp<cls<'\n'>>, any>>>;
};
template <> struct toml_grammar::def<toml_grammar::bare> {
    using type = plus<seq<notp<cls<' ', '\t', '\n', '\r', '[', ']', '{', '}', '=', '.', '#', ',', '"', '\''>>,
                          any>>;
};
template <> struct toml_grammar::def<toml_grammar::basic_string> {
    using type = seq<lit<'"'>,
                     star<alt<seq<lit<'\\'>, any>, seq<notp<cls<'"', '\\'>>, any>>>,
                     lit<'"'>>;
};
template <> struct toml_grammar::def<toml_grammar::literal_string> {
    using type = seq<lit<'\''>, star<seq<notp<cls<'\''>>, any>>, lit<'\''>>;
};
template <> struct toml_grammar::def<toml_grammar::key> {
    using type = alt<rule<toml_grammar::bare>, rule<toml_grammar::basic_string>,
                     rule<toml_grammar::literal_string>>;
};
template <> struct toml_grammar::def<toml_grammar::dotted_key> {
    using type = seq<rule<toml_grammar::key>,
                     star<seq<rule<toml_grammar::ws>, lit<'.'>, rule<toml_grammar::ws>,
                              rule<toml_grammar::key>>>>;
};
template <> struct toml_grammar::def<toml_grammar::array> {
    using type = seq<lit<'['>, rule<toml_grammar::ws>,
                     opt<seq<rule<toml_grammar::value>, rule<toml_grammar::ws>,
                             star<seq<lit<','>, rule<toml_grammar::ws>,
                                      rule<toml_grammar::value>, rule<toml_grammar::ws>>>>>,
                     lit<']'>>;
};
template <> struct toml_grammar::def<toml_grammar::inline_table> {
    using type = seq<lit<'{'>, rule<toml_grammar::ws>,
                     opt<seq<rule<toml_grammar::keyval>, rule<toml_grammar::ws>,
                             star<seq<lit<','>, rule<toml_grammar::ws>,
                                      rule<toml_grammar::keyval>, rule<toml_grammar::ws>>>>>,
                     lit<'}'>>;
};
template <> struct toml_grammar::def<toml_grammar::value> {
    using type = alt<rule<toml_grammar::basic_string>, rule<toml_grammar::literal_string>,
                     rule<toml_grammar::array>, rule<toml_grammar::inline_table>,
                     rule<toml_grammar::bare>>;
};
template <> struct toml_grammar::def<toml_grammar::keyval> {
    using type = seq<rule<toml_grammar::dotted_key>, rule<toml_grammar::ws>, lit<'='>,
                     rule<toml_grammar::ws>, rule<toml_grammar::value>>;
};
template <> struct toml_grammar::def<toml_grammar::table> {
    using type = seq<lit<'['>, rule<toml_grammar::ws>, rule<toml_grammar::dotted_key>,
                     rule<toml_grammar::ws>, lit<']'>>;
};
template <> struct toml_grammar::def<toml_grammar::array_table> {
    using type = seq<lit<'['>, lit<'['>, rule<toml_grammar::ws>, rule<toml_grammar::dotted_key>,
                     rule<toml_grammar::ws>, lit<']'>, lit<']'>>;
};
template <> struct toml_grammar::def<toml_grammar::statement> {
    using type = alt<rule<toml_grammar::table>, rule<toml_grammar::array_table>,
                     rule<toml_grammar::keyval>>;
};
template <> struct toml_grammar::def<toml_grammar::document> {
    using type = star<alt<rule<toml_grammar::ws>, rule<toml_grammar::comment>,
                          rule<toml_grammar::statement>>>;
};

} // namespace pars::bench
