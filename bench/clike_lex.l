/* bench/clike_lex.l — flex spec for the C-like token grammar.
 *
 * Records the byte offsets of `;`, `{`, `}` outside comments/strings, i.e. the
 * same structural index that the pars C-like grammar produces, so the two are
 * directly comparable.
 */
%{
#include <cstdint>
#include <vector>
extern std::vector<std::uint32_t> g_pos;
extern const char* g_base;
%}
%option noyywrap nounput noinput
%x COMMENT
%%
"/*"                { BEGIN(COMMENT); }
<COMMENT>"*/"       { BEGIN(INITIAL); }
<COMMENT>\n         { }
<COMMENT>.          { }
"//"[^\n]*          { }
[;{}]               { g_pos.push_back((std::uint32_t)(yytext - g_base)); }
\n                  { }
.                   { }
%%
