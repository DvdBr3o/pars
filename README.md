# pars — the scan algebra 𝔖: grammars to parallel parsers

`pars` compiles a PEG/BNF **grammar declaration** (ordinary C++ types) into a
data-parallel parsing implementation.  The design is not format-specific: the
framework lowers any grammar that is *finitely-stateful + data-parallel +
well-nested/associative* to a pipeline of a small set of primitives, which is
exactly the structure behind cuJSON / simdjson / simdcsv.

See **[`docs/model/model.md`](docs/model/model.md)** (readable) and
**[`docs/model/model.tex`](docs/model/model.tex)** (formal) for the derivation.

## The scan algebra 𝔖 in one paragraph

A naive branch program (scalar `if`/`while` + bounded state + a stack) is rewritten
into branchless masked data flow.  A stateful recurrence becomes a **prefix product
in a transformation monoid** — a parallel scan; a 1-bit flag is the affine case
`x' = (a & x) ^ b`, whose prefix is simdjson's `prefix_xor`.  Filtering becomes
**compaction**; the parser stack becomes a **well-nested reduction** (depth scan +
sort + pair).  The primitives:

```
R1 classify  R2 state->mask  R3 select  R4 monoid scan
R5 compact   R6 well-nested reduce      R7 associative reduce
```

## Layout

```
include/pars/
  dsl.hpp                    surface PEG/BNF types
  plan.hpp                   scan_plan (compiler output / backend input)
  compile/plan.hpp           grammar -> scan_plan
  model/monoid.hpp           R4 algebra: transformation monoid, affine, scans
  model/classify.hpp         R1: terminals -> byte classes
  model/scan.hpp             R3+R4+R5 CPU backend (per-mode FSM)
  model/nest.hpp             R6: depth-sort bracket pairing
  backend/cuda/pipeline.cuh  CUDA backend (classify/mask/compact/pair)
bench/grammars/{json,csv}.hpp  grammars declared only on the bench side
tests/test_model.cpp
```

## Build & run

```bash
xmake build pars_tests && xmake run pars_tests
xmake build pars_bench_gpu && xmake run pars_bench_gpu      # CUDA throughput
xmake build pars_bench && xmake run pars_bench              # CPU throughput
```

## Status

JSON on the generated CUDA pipeline matches cuJSON's order of magnitude; CSV on
GPU still trails hand-written AVX2 simdcsv.  See `docs/model/model.md` §6 for
numbers and `§7` for the boundary (unbounded lookahead / non-well-nested
recursion are fallbacks; stage-2 parsing is out of scope).
