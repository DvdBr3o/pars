// bench/grammars/xml.hpp — XML stage-1 structural scanner declared via the DSL.
//
// NOTE: this is a *stage-1* grammar: it masks attribute values and derives the
// structural characters `< > / = ? !`, but does NOT model tag nesting (that
// needs the visibly-pushdown call/return extension for named tags).  It is
// therefore compared as a structural indexer, like the other formats.
#pragma once

#include "pars/pars.hpp"

namespace pars::bench {

struct xml_grammar {
    struct document {};
    struct element {};
    struct tagpiece {};
    struct name {};
    struct ws {};
    struct text {};
    struct dquote {};
    struct squote {};

    using rules = type_list<document, element, tagpiece, name, ws, text, dquote, squote>;
    using lexical_rules = type_list<ws, name, text, dquote, squote>;
    using start = document;
    template <class> struct def;
};

template <> struct xml_grammar::def<xml_grammar::ws> {
    using type = plus<cls<' ', '\t', '\n', '\r'>>;
};
template <> struct xml_grammar::def<xml_grammar::name> {
    using type = plus<cls<'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm',
                          'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
                          'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M',
                          'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
                          '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '_', ':', '-', '.'>>;
};
// text outside tags: any byte that is not '<'
template <> struct xml_grammar::def<xml_grammar::text> {
    using type = plus<seq<notp<cls<'<'>>, any>>;
};
// attribute values are masked modes (no escaping)
template <> struct xml_grammar::def<xml_grammar::dquote> {
    using type = seq<lit<'"'>, star<seq<notp<cls<'"'>>, any>>, lit<'"'>>;
};
template <> struct xml_grammar::def<xml_grammar::squote> {
    using type = seq<lit<'\''>, star<seq<notp<cls<'\''>>, any>>, lit<'\''>>;
};
template <> struct xml_grammar::def<xml_grammar::tagpiece> {
    using type = alt<rule<xml_grammar::ws>, rule<xml_grammar::dquote>, rule<xml_grammar::squote>,
                     rule<xml_grammar::name>, lit<'/'>, lit<'='>, lit<'?'>, lit<'!'>>;
};
template <> struct xml_grammar::def<xml_grammar::element> {
    using type = seq<lit<'<'>, star<rule<xml_grammar::tagpiece>>, lit<'>'>>;
};
template <> struct xml_grammar::def<xml_grammar::document> {
    using type = star<alt<rule<xml_grammar::element>, rule<xml_grammar::text>>>;
};

} // namespace pars::bench
