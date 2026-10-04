// bench/grammars/json.hpp — JSON declared only through the pars DSL.
#pragma once

#include "pars/pars.hpp"

namespace pars::bench {

struct json_grammar {
	struct value {};

	struct object {};

	struct array {};

	struct members {};

	struct member {};

	struct string {};

	struct number {};

	struct digits {};

	struct ws {};

	using rules = type_list<value, object, array, members, member, string, number, digits, ws>;
	using start = value;
	template<class>
	struct def;
};

template<>
struct json_grammar::def<json_grammar::ws> {
	using type = star<cls<' ', '\t', '\n', '\r'>>;
};

template<>
struct json_grammar::def<json_grammar::digits> {
	using type = plus<cls<'0', '1', '2', '3', '4', '5', '6', '7', '8', '9'>>;
};

template<>
struct json_grammar::def<json_grammar::number> {
	using d	 = cls<'0', '1', '2', '3', '4', '5', '6', '7', '8', '9'>;
	using nz = cls<'1', '2', '3', '4', '5', '6', '7', '8', '9'>;
	using type =
		seq<opt<lit<'-'>>, alt<lit<'0'>, seq<nz, star<d>>>,
			opt<seq<lit<'.'>, rule<json_grammar::digits>>>,
			opt<seq<cls<'e', 'E'>, opt<cls<'+', '-'>>, rule<json_grammar::digits>>>>;
};

template<>
struct json_grammar::def<json_grammar::string> {
	using type =
		seq<lit<'"'>, star<alt<seq<lit<'\\'>, any>, seq<notp<cls<'"', '\\'>>, any>>>, lit<'"'>>;
};

template<>
struct json_grammar::def<json_grammar::member> {
	using type =
		seq<rule<json_grammar::string>, rule<json_grammar::ws>, lit<':'>, rule<json_grammar::ws>,
			rule<json_grammar::value>>;
};

template<>
struct json_grammar::def<json_grammar::members> {
	using type = seq<rule<json_grammar::member>, star<seq<lit<','>, rule<json_grammar::member>>>>;
};

template<>
struct json_grammar::def<json_grammar::array> {
	using type =
		seq<lit<'['>, rule<json_grammar::ws>,
			opt<seq<rule<json_grammar::value>, star<seq<lit<','>, rule<json_grammar::value>>>>>,
			lit<']'>>;
};

template<>
struct json_grammar::def<json_grammar::object> {
	using type = seq<lit<'{'>, rule<json_grammar::ws>, opt<rule<json_grammar::members>>, lit<'}'>>;
};

template<>
struct json_grammar::def<json_grammar::value> {
	using type =
		alt<rule<json_grammar::object>, rule<json_grammar::array>, rule<json_grammar::string>,
			rule<json_grammar::number>, seq<lit<'t'>, lit<'r'>, lit<'u'>, lit<'e'>>,
			seq<lit<'f'>, lit<'a'>, lit<'l'>, lit<'s'>, lit<'e'>>,
			seq<lit<'n'>, lit<'u'>, lit<'l'>, lit<'l'>>>;
};

}  // namespace pars::bench
