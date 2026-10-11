# Meta backend: assembling kernels by rewrite (experiment)

Branch: `experiment/kernel-meta-assembly`.

## Why

The reference GPU backend (`include/pars/backend/cuda/pipeline.cuh`) expresses every
grammar as **fixed kernels + runtime descriptor structs** (`GpuSpec` / `GpuPlan`).
Optimization is "detect a kind and pick a path" (`if constexpr (kFast)`), and the
only thing that varies between a JSON and a CSV parse is a table passed at launch.

This branch keeps the theory (R1..R7, grammar -> `scan_plan`) **unchanged** and
replaces the *lowering/emission* layer with a compile-time stage graph that is
**rewritten and then assembled into specialized kernels**.

## Layers

```
Layer 0 (unchanged): grammar type --make_scan_plan<G>()--> scan_plan S
Layer 1 (new):       S ------------------------------> Pipeline<Stage...>   meta/ir.hpp
Layer 2 (new):       Pipeline -- rewrite (metafns) ---> Pipeline'           meta/rewrite.hpp
Layer 3 (new):       Pipeline' -- emit ---------------> specialized kernels meta/emit.cuh, meta/scan.cuh
Layer 4 (new):       compact value spec (head_spec_t) for the fused head    meta/ir.hpp
```

### Layer 1 — typed IR (`meta/ir.hpp`)

One node per primitive instance:

| Node | Primitive |
|---|---|
| `classify` | R1 (terminals -> byte-class bitmasks) |
| `mask<Kind, D>` | R4 (one per lexical rule; `Kind` is the provable factorization) |
| `clip` | R3 (structural & ~masked) |
| `compact` | R5 (filter -> dense index) |
| `nest` | R6 (well-nested pairing) |
| `head<Kind>` | R1+R4+R3 fused for a single lexical mode (rewrite output) |
| `head_plain` | R1+R3 fused when there is no lexical mode |

`pipeline<Stage...>` is an ordered, typed stage list.

### Layer 2 — rewrite (`meta/rewrite.hpp`)

- `build_pipeline<S>` produces the pristine pipeline
  `pipeline<classify, mask<k0,0>, ..., clip, compact, nest>`.
- `fuse_head` collapses `classify, mask<K,0>, clip` -> `head<K>` when there is
  exactly one masking rule; multi-mode grammars keep the three stages separate.
- `rewrite<T>` drives the passes to a fixpoint.

Worked results (asserted in `tests/test_meta.cpp`):

| grammar | pristine | rewritten |
|---|---|---|
| JSON (1 affine mode) | `classify, mask<affine,0>, clip, compact, nest` | `head<affine>, compact, nest` |
| CSV (1 doubled mode) | `classify, mask<doubled,0>, clip, compact, nest` | `head<doubled>, compact, nest` |
| TOML (multi-mode) | `classify, mask*, clip, compact, nest` | unchanged (no fusion) |

### Layer 3 — emit (`meta/emit.cuh`, `meta/scan.cuh`)

- `emit_head<Kind, S>` dispatches on a compile-time **policy**. The parity
  (`doubled` / no-escape) policy is wired end-to-end:
  `head_classify_kernel<pol_parity>` (P1+R2 fused) -> popcount+scan (carry) ->
  `head_parity_apply_kernel` (R3 clip fused with the parity mask).
  The structural alphabet comes from `head_spec_t` (~26 bytes) instead of the
  multi-KB `GpuSpec`.
- `compact_p3<S>` is the shared R5 stage.
- `scan_device_meta<S>` runs `emit_head` when the rewritten pipeline is parity,
  and otherwise **delegates to the reference `scan_device_impl<S>`** so JSON,
  TOML etc. keep working while specializations come online.

### Layer 4 — compact spec (`head_spec_t`)

A `__global__` template cannot take the full `scan_plan` as a non-type template
parameter: the mangled symbol encodes every byte of the descriptor tables, and
MSVC aborts (C1064). `head_spec_t` is a small structural value (structural
chars + bracketing + one delimiter) small enough to pass.

## Toolchain finding

On this toolchain (CUDA 13.3 + MSVC 2026), a **struct NTTP on a `__global__`
template is broken**: even a 9-byte struct triggers MSVC `C1903` in the
generated `*.cudafe1.stub.c`, while an integral NTTP (`template<int N>`) compiles
cleanly. We therefore template kernels on an integral `head_policy` and pass the
compact `head_spec_t` by value (constant param space). A minimal repro lived at
`tests/probe.cu` during the experiment.

The build also needs, for CUDA 13.x's CCCL/Thrust:

- `-Xcompiler=/Zc:preprocessor` (MSVC conforming preprocessor)
- `--expt-relaxed-constexpr` (device code reads the constexpr plan)

Both are added by the `cccl_msvc` rule in `xmake.lua` and applied to every CUDA
target.

## Status

| Item | State |
|---|---|
| IR + rewrite engine | done (`meta/ir.hpp`, `meta/rewrite.hpp`), asserted in `pars.test.meta` |
| Frozen head fusion (parity) | done, end-to-end on CSV |
| Fused head for affine / table policies | **not yet** — delegates to `scan_device_impl` |
| Fused `compact` (block scan in the head) | **not yet** |
| `nest` specialization (single-bracket unroll) | **not yet** |
| chunk carry through the pipeline | **not yet** |
| correctness vs CPU | CSV (specialized) and JSON (delegated) both pass (`pars.test.meta.gpu`) |

## Build & run

```bash
xmake build pars.test.meta && xmake run pars.test.meta          # IR + rewrite asserts
xmake build pars.test.meta.gpu && xmake run pars.test.meta.gpu  # specialized kernels vs CPU
```

## Next steps

1. Add `emit_head` for the `affine` policy: reuse the descriptor DFA scan but
   with the transition tables baked, and emit `prefix_xor`/escape-scan inline.
2. Fuse R5 into the head (per-block popcount + block scan) to remove the
   `struct_out`/`openclose_out` global round-trip.
3. Specialize `nest` for a single bracket kind and fold it into the pipeline.
4. Bring `scan_chunk` onto the IR so the chunked path is assembled too.

## User API

`include/pars/parse.cuh` exposes the CUDA-accelerated entry point on top of the
meta backend:

```cpp
#include "pars/parse.cuh"
#include "grammars/json.hpp"

std::string_view buffer = ...;
auto r = pars<bench::json_grammar>(buffer);                 // structural index
auto p = pars<bench::json_grammar>(buffer, {.want_pairs=true}); // + bracket pairs
```

`parse_result` carries `structural` (byte offsets), `pair` (matching-bracket
index or -1), `balanced`/`well_formed`/`max_depth`, and a `visit(...)` that
replays the index into a `model::replay` visitor.

`parser<Grammar>` is the same engine with the device-level entry points used by
the tiered benchmark: `scan_device(d, n, stream, want_pairs, run_pairs, stage_times*)`,
`scan_chunk(...)`, `reserve(cap)`, `struct_ptr()`.

### Name resolution

The library namespace is `pars`, so the free function `pars` and the namespace
`pars` collide at global scope.  Inside a scope that declares it, the call reads
exactly `pars<Grammar>(buffer)`:

```cpp
namespace {
using pars::pars;                 // shadows the global `pars` namespace
auto r = pars<json_grammar>(buf); // unqualified: resolves to the function
}
```

Otherwise use the qualified form `pars::pars<Grammar>(buf)` or
`pars::parser<Grammar>{}(buf)`.  The benchmark files use the shadowing form.

### Toolchain note

`simdjson.h` cannot be included in an `nvcc` translation unit on Windows: CCCL
requires `/Zc:preprocessor` and simdjson fails under it (MSVC C2382).  simdjson
baselines are therefore compiled as host files (`bench/host_parse_simdjson.cpp`,
`bench/host_pointquery.cpp`).

