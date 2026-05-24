<h1 align="center">pars</h1>

<div align="center">Vivid and powerful PEG parser in C++20</div>

---

> [!WARNING]
> This repo does NOT sync with the latest implementation of pars. The latest version of pars is currently inlined into project [luna](https://github.com/DvdBr3o/luna) and will be versioned into this repo once it is functional complete and stable.

## Features

1. Compile-time
2. Combinable
3. Customizable

## A Quick Example

Parsing an HTTP `Authorization: Bearer <token>` header is a common production task in gateways,
API servers, and CLI tools:

```cpp
struct BearerAuth {
	std::string token;
};

constexpr auto token_char = cran('a', 'z') | cran('A', 'Z') | cran('0', '9') | cset('-', '_', '.');
constexpr auto bearer_auth =
	(cstr(U"Authorization:") >> *c(' ') >> cstr(U"Bearer") >> +c(' ') >> +token_char)
	^ value_to([](auto&&, auto&&, auto&&, auto&&, const auto& token) {
		  return BearerAuth {.token = u32chars_to_u8(token)};
	  });

auto st	 = TextParserState {u8"Authorization: Bearer eyJhbGciOiJIUzI1NiJ9"};
auto res = bearer_auth.match(st);
assert(res.value().token == "eyJhbGciOiJIUzI1NiJ9");
```

This style scales naturally from “extract one header” to larger config, markdown, DSL, or source
code parsers.

## How it works

### Primitives

`pars` provides basic parser rule match primitives to users.

| **Type** | **Primitives** | **Interface**          | **Function** |
| -------- | -------------- | ---------------------- | ------------ |
| char     | Char           | `c('')`                |              |
| char     | CharSet        | `cset(''...)`          |              |
| char     | CharRange      | `cran('', '')`         |              |
| char     | AnyChar        | `cany`                 |              |
| combine  | Sequential     | `a >> b`               |              |
| combine  | Choice         | a \| b                 |              |
| combine  | Optional       | `-a`                   |              |
| combine  | Repeatable     | `*a`                   |              |
| combine  | OnceOrMore     | `+a`                   |              |
| peek     | PeekIs         | `~a`                   |              |
| peek     | PeekNot        | `!a`                   |              |
| topology | Fix            | `fix(self -> f(self))` |              |

### Optimization

`pars` automatically optimizes combined parser rules by utilizing algebraic properties in compile time.

| **Optimization**           | **Pre-opt**                      | **Post-opt**        |
| -------------------------- | -------------------------------- | ------------------- |
| Left Distributive Property | `(a >> b)` \| `(a >> c)`         | `a >>` (`b` \| `c`) |
| Idempotent Law             | `a` \| `a`                       | `a`                 |
| Law of Absortion           | `*a >> a`                        | `+a`                |
| Law of Absortion           | `a` \| `*a`                      | `*a`                |
| Law of Double Negatives    | `!!a`                            | `a`                 |
| Peeking Merge              | `!a` \| `(!a >> b)`              | `!a`                |
| Peeking Aborb              | `~a >> a`                        | `a`                 |
| char Unionization          | `c('a')` \| `c('b')`             | `cset('a', 'b')`    |
| char Intervalization       | `c('a')` \| `c('b')` \| `c('c')` | `cran('a', 'c')`    |
