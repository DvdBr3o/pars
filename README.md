<h1 align="center">pars</h1>

<div align="center">Vivid and powerful PEG parser in C++20</div>

---

## Features

1. Compile-time
2. Combinable
3. Customizable

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
