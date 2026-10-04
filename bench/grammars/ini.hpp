// bench/grammars/ini.hpp — INI declared via the pars DSL.
#pragma once

#include "pars/pars.hpp"

namespace pars::bench {

struct ini_grammar {
    struct file {};
    struct ws {};
    struct comment {};
    struct atom {};
    struct key {};
    struct value {};
    struct section {};
    struct entry {};
    struct line_end {};

    using rules = type_list<file, ws, comment, atom, key, value, section, entry, line_end>;
    using lexical_rules = type_list<ws, comment, atom>;
    using start = file;
    template <class> struct def;
};

template <> struct ini_grammar::def<ini_grammar::ws> {
    using type = star<cls<' ', '\t'>>;
};
template <> struct ini_grammar::def<ini_grammar::comment> {
    using type = seq<lit<';'>, star<seq<notp<cls<'\n'>>, any>>>;
};
template <> struct ini_grammar::def<ini_grammar::atom> {
    using type = plus<seq<notp<cls<' ', '\t', '\n', '\r', '[', ']', '=', ';', '#'>>, any>>;
};
template <> struct ini_grammar::def<ini_grammar::key> { using type = rule<ini_grammar::atom>; };
template <> struct ini_grammar::def<ini_grammar::value> { using type = rule<ini_grammar::atom>; };
template <> struct ini_grammar::def<ini_grammar::line_end> { using type = lit<'\n'>; };
template <> struct ini_grammar::def<ini_grammar::section> {
    using type = seq<lit<'['>, rule<ini_grammar::ws>, rule<ini_grammar::key>,
                     rule<ini_grammar::ws>, lit<']'>>;
};
template <> struct ini_grammar::def<ini_grammar::entry> {
    using type = seq<rule<ini_grammar::key>, rule<ini_grammar::ws>, lit<'='>,
                     rule<ini_grammar::ws>, rule<ini_grammar::value>>;
};
template <> struct ini_grammar::def<ini_grammar::file> {
    using type = star<alt<rule<ini_grammar::ws>, rule<ini_grammar::comment>,
                          rule<ini_grammar::section>, rule<ini_grammar::entry>,
                          rule<ini_grammar::line_end>>>;
};

} // namespace pars::bench
