# 𝔖′ 幻灯片（理论 ↔ 源码 template IR rewrite）

> 每一页左边是理论对象，右边是它在 pars 源码里的**模板匹配（match）→ IR 重写（rewrite）→ 执行**。
> 目录：A 总览 / B 一个原语 / C 四种 schema 的 rewrite / D by-case 全流程 / E 执行后端。

---

# Part A · 总览：理论对象 ↔ 源码

# Slide A1 — 一张对照表

| 理论对象 | 源码 | 关键符号 |
| --- | --- | --- |
| 摘要幺半群 + 前缀积 | `include/pars/algebra/summary.hpp` | `Summary`、`prefix_reduce` |
| 四种 schema 的实例 | 同上 | `state_map` / `bracket_balance` / `reducer` / `ranked_choice` |
| IR：schema + cell | `include/pars/core/plan.hpp` | `enum schema`、`struct cell`、`struct plan` |
| 文法 → IR 的 rewrite | `include/pars/core/lower.hpp` | `lower<G>()`、`tree_scan`、`monoid_op` |
| 词法 DFA（rewrite 的输入） | `include/pars/compile/automaton.hpp` | `reify`、`der`、`build_dfa` |
| 递归/括号/标点的分析 | `include/pars/compile/plan.hpp` | `bracket_guard`、`first_terms`、`collect_lits` |
| 统一 API | `include/pars/scan.hpp` | `scan<G>`、`scan_device<G>`、`scan_result` |
| 执行（host / GPU） | `core/host.hpp`、`core/tree.hpp`、`core/cuda.hpp` | `scan<P>`、`reduce<P>`、`to_scan_plan<P>` |

**一句话**：`lower<G>()` 把文法**重写成** `plan = { cell(schema, …) }`；执行器把每个 cell 变成逐字节元素 `τ_b`，一次 `prefix_reduce`。

---

# Part B · 一个原语

# Slide B1 — `Summary` 概念与 `prefix_reduce`

理论：解析 = 幺半群同态 ⇒ 前缀积 = 并行前缀扫描。

```cpp
// algebra/summary.hpp
template<class S>
concept Summary = requires(const S& a, const S& b) {
    { S::compose(a, b) } -> std::same_as<S>;   // ⊗（结合）
    { S::identity() }    -> std::same_as<S>;   // e
};

template<Summary S>
void prefix_reduce(S* p, std::size_t n) {      // 唯一并行原语
    for (std::size_t d = 1; d < n; d <<= 1)
        for (std::size_t i = n; i-- > d;)
            p[i] = S::compose(p[i - d], p[i]);
}
```

四个实例（同文件）就是四种 `S`：

```cpp
state_map<NS>          // finite : Q->Q 变换
bracket_balance        // stack  : (未配对闭, 未配对开)
reducer<V,Op>          // tree   : (has,v) + op
ranked_choice<V>       // choice : (matched,rank,v) + min
```

**这是整个框架唯一的并行原语**；后端只是把它搬到 GPU（`thrust::inclusive_scan` / `TCompose64`）。

---

# Part C · 四种 schema 的 template rewrite

# Slide C1 — 文法 AST（被匹配的对象）

```cpp
// dsl.hpp
eps   any   lit<'c'>   cls<Cs...>   seq<...>   alt<...>   rep<G,Min,Max>   notp<G>   rule<Tag>
```

`lower<Grammar>()` 对每条 `rule<Tag>` 的 `def<Tag>::type` 做**模板匹配**，决定 schema。
匹配结果写进 IR：

```cpp
// core/plan.hpp
enum class schema : uint8_t { finite, stack, tree, choice, interval };

struct cell {                       // 一个 summary 生成器
    schema sch;
    std::array<uint8_t,256> rep_of; // byte -> class
    uint8_t n_states, n_classes, start;
    uint8_t trans[8][8], masked[8][8];   // finite
    char open, close;                    // stack
    char op;                             // tree
    …
};
struct plan { cell cells[8]; uint8_t n_cells; char atoms[8]; … };
```

每个 schema 就是**一组固定的 IR 字段**——这是 rewrite 的靶子。

---

# Slide C2 — rewrite 1：正则词法 → `finite` cell

**匹配**：非递归规则的规则体（`lit`/`cls`/`seq`/`alt`/`star`/受支持的 `notp`）。

```cpp
// compile/automaton.hpp
reify<Grammar, lit<'c'>>          -> rk::ch           // 正则表达式（arena）
reify<Grammar, cls<Cs...>>        -> rk::set
reify<Grammar, seq<notp<cls<S>>, any>> -> rk::set(补集)   // 唯一支持的 notp 形态
der(A, id, c)                     -> 导数             // Brzozowski
build_dfa(A, root)                -> dfa { trans, accepting, rep_of }
```

**重写**（`core/lower.hpp` → `make_desc`）：DFA → `cell{finite}`

```cpp
cell c; c.sch = schema::finite;
c.rep_of = dd.rep_of;  c.trans = dd.trans;  c.masked = dd.masked;
c.start  = dd.start;   c.n_states = dd.n_states;
```

**执行**：`state_map<NS>`（DFA 转成变换幺半群）→ `prefix_reduce`。

---

# Slide C3 — rewrite 2：递归 + 两端字面量 → `stack` cell

**匹配**：递归规则、且顶层 `seq` 恰有两个字面量端点（首/尾、互异、递归在中间）。

```cpp
// compile/plan.hpp
template<class Grammar, class... Gs>
struct bracket_guard<Grammar, seq<Gs...>> {
    static constexpr char open  = first_c();
    static constexpr char close = last_c();
    static constexpr bool ok =
        (N >= 3) && first_lit() && last_lit() && (nlits == 2) && (open != close);
};
```

**重写**（`core/lower.hpp`）：

```cpp
cell c; c.sch = schema::stack; c.open = b.open; c.close = b.close;
```

**执行**：`bracket_balance`（单类型退化为 `(闭,开)`）→ `prefix_reduce` → 深度 → 排序配对。
多类型时在配对处做类型校验（`match_brackets`）。

---

# Slide C4 — rewrite 3：单自递归 + 结合算子 → `tree` cell

**匹配**：递归、非 bracket-guarded、且规则体是 `X (op X)*`（`rep` 里含唯一字面量算子）。

```cpp
// core/lower.hpp（detail）
op_ch<lit<C>>            -> C
elem_op<seq<A,B>>        -> op_ch<A> ? op_ch<A> : op_ch<B>
rep_op<rep<G,Min,Max>>   -> elem_op<G>::value
any_rep_op<Gs...>        -> 取第一个非零
monoid_op<seq<Gs...>>    -> any_rep_op<Gs...>::value     // 得到 op，如 '+'
```

**重写**：

```cpp
if (op != 0) { cell c; c.sch = schema::tree; c.op = op; P.cells[P.n_cells++] = c; }
```

**执行**：`reducer<V,Op>`（`Op` 可结合）→ `prefix_reduce`；GPU 走 `tree_reduce_gpu<Op>`。

---

# Slide C5 — rewrite 4：判别性 `alt` → `finite`

**理论事实**：PEG `/` 结合、但**不分配于连接**；"取第一个成功" = 对成功者取 `min(rank)`。

**匹配/重写**：若各备选首字符集在有界窗口内不相交（判别性），`alt` 退化为**并集 DFA**
（`r_alt` 做 ACI 排序去重 → 语言并集），于是落到 `finite` cell：

```cpp
// automaton.hpp
r_alt(A, a, b)   // 拍平 + 去重（集合语义）
// 判别性时刻画的是同一个 DFA；非判别性（需回溯）⇒ 不满足可压缩，落 interval
```

**执行**：与 `finite` 完全相同（同一个 `state_map` + `prefix_reduce`）。
非判别性 ⇒ `interval`（区间不动点，`O(n²)`，兜底）。

---

# Part D · By case 全流程（源码级）

# Slide D1 — `{"a":"b"}` 从文法到索引

**输入** `{"a":"b"}`；`lower<json_grammar>()` 的匹配结果：

| 规则 | match | rewrite → cell |
| --- | --- | --- |
| `string` | 正则 DFA（首字节 `"`，前缀转义 `\`） | `cell{finite}` |
| `object` | `bracket_guard`：顶层 `{` `}` | `cell{stack, '{', '}'}` |
| `:` `,` | `collect_lits`（独立字面量） | `plan.atoms` |
| `value` 的 alt | 判别性（首字节不相交） | 并入 `finite` |

```cpp
static constexpr auto P = core::lower<bench::json_grammar>();
// P.cells = { finite(string), stack({}) }, P.atoms = { ':', ',' }
```

---

# Slide D2 — 逐字节元素 → `prefix_reduce` → 掩码

**执行器**：`core/host.hpp::scan<P>()`（GPU 对应 `core/cuda.hpp` + `pipeline.cuh`）。

```cpp
using SM = algebra::state_map<kMaxStates>;
for (i) { SM e = SM::identity(); for (s<n_states) e.f[s] = cell.trans[s][rep_of[b]]; p[i]=e; }
algebra::prefix_reduce(p.data(), n);                    // ⊗ = compose
// 读前缀摘要：处理字节 i 之前的状态 = (i==0? start : p[i-1].f[start])
```

对 `{"a":"b"}` 的 string cell：

```
byte:        {    "    a    "    :    "    b    "    }
state before: -    0    1    1    0    0    1    1    0
mask(in)   : -    0    1    1    0    0    1    1    0
```

---

# Slide D3 — 输出算子：结构索引 + 配对

```cpp
// core/host.hpp
if (masked) continue;
if (structural_table<P>()[b]) { out.pos.push_back(i); out.ch.push_back(b); }

// stack cell 的深度 + 配对（模型里是 R6；源码在 GPU 是 depth_init→scan→sort→pair）
```

结果：`structural = [0,4,8]`（`{` `:` `}`），`pair = {0↔8}`。
**全程只有 `prefix_reduce` + 与 schema 无关的输出算子（popcount+scan+scatter / 排序配对）。**

---

# Part E · 执行后端（同一 IR，不同放置）

# Slide E1 — 一个 API，三种放置

```cpp
// scan.hpp
template<class Grammar> scan_result scan(std::string_view, scan_options);   // 流式（H2D 与 scan 重叠）
template<class Grammar> gpu::device_result scan_device(const uint8_t*, n);  // device-resident
result.visit(bytes, visitor);                                               // 消费者
```

内部映射：

| 层 | 文件 | 作用 |
| --- | --- | --- |
| IR 适配 | `core/cuda.hpp::to_scan_plan<P>()` | `cell` → `scan_plan`（后端输入） |
| GPU 熔合扫描 | `pipeline.cuh` | `classify` → `desc_wordt64` → `thrust::inclusive_scan(TCompose64)` → `desc_replay64` → `scatter` |
| 树归约 | `core/cuda.hpp::tree_reduce_gpu` | 分块局部归约 + `thrust::reduce` |
| 流式重叠 | `scan.hpp::stream_ctx` | 双缓冲、两 stream、`scan_chunk` carry |

**同一套 `cell`/`prefix_reduce`，只换放置与消费者。**

---

# Slide E2 — 收束

```
理论：解析 = 同态；并行 ⟺ 摘要可压缩；四 schema = 四个后缀幺半群
                    ↓  (template match → IR rewrite)
源码：dsl AST ─reify/der/build_dfa→ dfa
              ─bracket_guard→ open/close
              ─monoid_op→ op
              ───────────────→ cell{schema,…} ──→ plan
                                         ↓
            逐字节元素 τ_b + prefix_reduce ──→ 结构索引 / 深度配对 / 归约值
```

**一个原语（`prefix_reduce`）、一个 IR（`cell`）、四个 rewrite（finite/stack/tree/choice）。**
