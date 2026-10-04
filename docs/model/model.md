# 扫描代数 𝔖：从文法到并行解析的统一推导

> 本文件是 `docs/model/model.tex` 的可读版本。两者内容一致；tex 是正式推导。

## 0. 目标

给一份 PEG/BNF 文法，机械地推导出一个数据并行（CPU SIMD / CUDA）的解析实现，
并解释 cujson / simdjson / simdcsv 为什么快。核心不是“识别格式再套技巧”，而是
把它们的做法归约到一个统一扫描代数 **𝔖**。

## 1. 朴素分支程序（NBP）

**NBP** 是只对字节流按位置执行的标量程序，控制流仅由
`if/else`（条件 = 当前字节类别 + 有界寄存器状态）、位置循环、有界寄存器、
一个栈、`emit` 组成。cuJSON 的全部“智能”就是两段 NBP：

```C++
// NBP-1 分类 + 字符串屏蔽
bool in_string=false, escaped=false;
for (i=0;i<n;++i){ c=data[i];
  if(in_string){ if(escaped)escaped=false; else if(c=='\\')escaped=true; else if(c=='"')in_string=false; }
  else { if(c=='"')in_string=true; else if(is_structural(c)){ emit_structural(i); if(is_bracket(c)) emit_bracket(i);} } }

// NBP-2 栈配对
stack s;
for(tok in bracket_indices){ if(is_open(data[tok])) s.push(tok); else { j=s.pop(); pair[j]=tok; } }
```

## 2. 𝔖 的原语与重写规则

设 `class: byte → C` 由终结符集合推导。

- **R1 分类**　`c==lit → M_lit`；`c∈S → ⋁M_char`。SIMD：`cmpeq`+`movemask`。
- **R2 状态→掩码**　k-bit 寄存器 → k 个 bitmask（bit i = 处理字节 i 前的状态位）。
- **R3 分支→选择**　`if(c) x=f(x) else x=g(x) → x=(c&f(x))|(~c&g(x))`。
- **R4 前缀状态→幺半群扫描**　状态递推 `state_{i+1}=T_{class_i}(state_i)`，每字节
  诱导状态变换 `τ_i:S→S`，于是
  ```
  state_i = (τ_{i-1} ∘ … ∘ τ_0)(s0)
  ```
  是变换幺半群 `(S→S, ∘)` 上的前缀积 = 一次并行扫描。
  - `S={0,1}`：`τ(x)=(a&x)^b`，仿射；复合 `(a1,b1)∘(a2,b2)=(a1&a2,(b1&a2)^b2)`。
    纯翻转 → **simdjson** **`prefix_xor`**；转义奇偶 → **cuJSON escape scan**。
- **R5 过滤→压缩**　`for i if M[i] out.push(i)` → `popcount + exclusive_scan + scatter`。
- **R6 栈→良嵌套归约**　良嵌套括号语言中栈内容由深度决定：
  `depth = 前缀和(+1/-1)`（Z 上的群扫描）；按深度稳定排序；相邻配对；局部校验类型。
  这就是 cuJSON 的 `depth_init→scan→stable_sort→validate`。
- **R7 结合递归→幺半群归约**　单自引用且算子可结合 → 幺半群 reduce。

**𝔖 = ⟨R1,R2,R3,R4,R5,R6,R7⟩。** 一个 NBP 若其控制流可分解为这些原语，则
**𝔖-可归约**；三个特征是：**有限状态 / 数据并行 / 良嵌套或结合**。

## 3. 三种 SOTA 的统一解释

| SOTA            | 𝔖 分解                                     |
| --------------- | ------------------------------------------ |
| simdjson stage1 | R1 + R4(prefix_xor/escaped_scan) + R3 + R5 |
| cuJSON tokenize | 同 simdjson（`__vcmpeq4`）                 |
| cuJSON parser   | R6（深度=R4 的 Z 扫描，配对=按深度排序）   |
| simdcsv         | R1 + R4（引号翻转）+ R3 + R5               |

它们不是三种技巧，而是同一组原语在不同文法上的实例化。

## 4. 文法 → 计划（编译，无模式识别）

`compile::make_scan_plan<G>()` 不使用任何语法外形模式匹配（旧的 `mode_of` /
`bracket_of` / `comment_of` 已删除），而是：

1. **词法规则 → DFA（Brzozowski 导数）**。把规则体 reify 成正则表达式，
   对每个字节求导数，状态 = 规范化的导数；`alt` 做 ACI 排序/去重防止爆炸。
   字母表按“每个字符谓词上的成员关系”划分等价类（`compile/automaton.hpp`），
   即使谓词是稠密补集也保持 O(不同字符类) 的规模。
   - **mode/comment 由此读出**：唯一首字节 D（`|FIRST|=1` 才建 DFA）→ 起点；
     若存在 `der(S1,D)` 且 `der(S2,D)=S1` → 双写（CSV）；若存在 E 使
     `der(S1,E)=S2` 且 S2 吸收回 S1 → 前缀转义（JSON `"`）；否则无转义（TOML `'`）。
   - **统一掩码定义**：一个字节被 mask ⇔ 其前的 DFA 状态非起点、且存在一条
     经结构字节到达接受状态的路径。这一个定义同时覆盖前缀转义、无转义、双写、
     行注释，无需分情况。
2. **递归规则 → 括号对（FIRST/LAST）**：递归 SCC 的 `FIRST`/`LAST` 终结符集合
   各为单字符且不同 → R6 括号对。这是 visibly-pushdown 的守护条件，不是外形匹配。
3. **punctuation**：结构规则中的独立字面量（排除括号/定界符/词法规则）。
4. **可支持性**：递归规则必须落在 R6/R7，否则诊断。

“词法/结构”划分是标准 BNF 分层，可由 `lexical_rules` 标注；缺省推断为非递归规则。

验证：JSON/CSV/TOML 三者都只靠上述构造得到正确的计划（TOML 派生 2 个 mode、
1 个 comment、2 组括号、3 个标点，见 `tests/` 与 `bench/grammars/toml.hpp`）。

**正确性闸门（P0）**：

- `masking_states`（统一掩码语义）现在是**执行语义的驱动**：一条词法规则只有在其
  DFA 确实“在结构字节上能到达接受”时才成为 mode/comment，不再是死代码。
- `notp`：只有 `seq<notp<cls<S>>, any>`（单字节补集）是正则且受支持；一般负向前瞻
  由 `notp_ok` 在编译期拒绝，导数构造绝不静默过近似。
- 括号：`bracket_guard` 要求 `seq` 的**顶层字面量恰好是两个端点**（首/尾、互异、
  递归严格在中间）。这否决了 `a ::= '(' a ')' ';'` 这类“尾字面量不是配对闭括号”
  的误判；良嵌套性仍由 `model/nest.hpp` 在运行期验证。这是**充分**（而非必要）的
  结构判据，必要时可标 fallback。
- `require_supported<G>()` / `is_supported<G>`：编译期硬闸门；不支持的规则（无法
  sound lowering 的递归、或不受支持的 `notp`）会报错，除非列入 `fallback_rules`。
- 标点排除项现在也含 comment 定界符。

**P1 执行语义（转移幺半群 + 前缀积 + 可证因子化）**：
`scan_plan` 现在携带每条词法屏蔽规则的 `lex_desc`（仍是 NTTP/structural type）：

| kind       | 内容                                          | 执行                                       |
| ---------- | --------------------------------------------- | ------------------------------------------ |
| `affine`   | 定界符集 + 前缀转义字节                       | `x' = (a & x) ^ b`（simdjson/cuJSON 形式） |
| `doubled`  | 定界符集                                      | 合法输入下即奇偶翻转                       |
| `comment`  | 起始字节集 + 换行复位                         | 到行尾                                     |
| `table`    | ≤8 状态 × ≤8 类的转移表 + per-transition mask | 通用幺半群元素（状态变换）                 |
| `fallback` | 超容量/不支持                                 | 诊断                                       |

- **执行语义**：每个字节诱导一个状态变换，前缀 compose 得到前缀态（R4）。掩码不是
  “状态∈inside”，而是\*\*“该字节被一个进行中的 token 消费”\*\*（per-transition bit）——
  这是精确的，能正确处理双写引号闭合后的下一个字节（`inside(state)` 二值化在此会
  差一位）。
- **因子化目前只是“检测并标注”**：由 DFA 结构读出 `kind`（唯一首字节 + 双写/前缀/
  无转义/单字节注释，否则 table），但 CPU `model/scan.hpp` 对所有 kind **统一执行**
  `trans`/`masked` 表；`affine`/`doubled`/`comment` **尚未发射特化**。特化将在 P1c 的
  GPU 侧发射（affine→`prefix_xor`）。`kind` 与原 DFA 一致性由差分随机测试覆盖。
- **reset 语义边界**：死转移复位是“无回溯”（`trans[start][c]` 或 `start`），对
  定界符锚定的 mode/comment 精确；对一般 token 的最长匹配/回溯**不等价**——这是
  “覆盖所有正则规则体”claim 的精度边界。
- **覆盖性证据**：新增 `bench/grammars/clike.hpp` 的**块注释** **`/* … */`**（标准
  `/\*([^*]|\*+[^*/])*\*+/`，多字符、两个不同定界符），**不是**四种模板之一，走 Table
  路径且 CPU 扫描与 C 参考一致（含 `/**/`、`/***/`、`*` 连写等对抗用例）。其 DFA 为
  5 状态（≤8 cap）。这证明覆盖类扩展到“任意正则规则体”，而非“识别四个惯用法”。
- 编译期报告：每条词法规则 → `{affine|doubled|comment|table|fallback}`（测试中
  `count_kind(...)` 静态断言）。

P1 剩余：**P1c** 让 GPU 消费同一张 descriptor（→ 多 mode + 注释，解锁 TOML GPU 与
killer demo）；**P1d** 特化等价性的形式化证明 + 性能报告。

## 5. 实现布局

```
include/pars/
  dsl.hpp                  surface PEG/BNF 类型
  plan.hpp                 scan_plan（编译输出，后端输入）
  compile/plan.hpp         文法 -> scan_plan（本模型第 4 节）
  model/monoid.hpp         R4 代数：变换幺半群 / 仿射 / 前缀扫描
  model/classify.hpp       R1：终结符 -> 字节类别
  model/scan.hpp           R3+R4+R5：CPU 后端（逐模式 FSM）
  model/nest.hpp           R6：深度排序配对
  backend/cuda/pipeline.cuh CUDA 后端（P1 classify → P2 mask → P3 compact → P4 pair）
bench/grammars/{json,csv}.hpp   仅在 bench 侧声明的文法
```

框架里没有 JSON/CSV/TOML 专属代码；文法只在 bench 侧。

## 6. Benchmark（67.1 MB，同机，RTX 4060 Laptop / CUDA 13.4）

| 格式 | 实现                     | GB/s       | 说明                                       |
| ---- | ------------------------ | ---------- | ------------------------------------------ |
| JSON | pars 𝔖 + CUDA            | 0.98–1.23  | 含 H2D 拷贝；token 数 20,742,755           |
| JSON | cuJSON（手写 CUDA）      | \~0.96–1.1 | token 数 20,742,757（含根/哨兵）           |
| JSON | simdjson（CPU ondemand） | 1.49       | 索引/值遍历                                |
| CSV  | pars 𝔖 + CUDA            | 2.15       | 修正双写后 token 数 8,567,094（=朴素参考） |
| CSV  | simdcsv（CPU AVX2）      | 9.37       | 引号翻转                                   |

结论：JSON 上 𝔖 生成的 CUDA 与 cujson 同量级；CSV 的 GPU 仍落后 CPU 的 simdcsv，
差距来自 67 MB 下 H2D 拷贝未摊薄与 CSV 结构密度低。重要修正：旧实现的双写引号
“相邻成对”判定是错误的（会漏计），本实现用奇偶翻转，token 数与朴素参考一致。

## 7. 边界

𝔖 覆盖“正则规则体 + visibly-pushdown 递归 + 结合递归”的文法。**不覆盖**无界回溯与
非良嵌套递归——这些必须标为 fallback，而不是硬塞进 SIMD。stage2 语义解析不在 𝔖 的
并行化范围内（它是顺序/按需的），这也是三家 SOTA 的共同边界。

---

# 补充：成本模型，以及 CSV 异常的根因（对 𝔖 的修正）

上面 §1–§7 描述了 𝔖 **哪些计算**可以并行；但把“计算可并行”当成“吞吐高”是错的。
实测表明 CSV 慢的原因**不在算法**，而在 𝔖 之前没有建模的**数据移动与放置（schedule）**。

## 8. CSV 为什么慢：实测归因

同机（RTX 4060 Laptop，CUDA 13.4）、同一份 67.1 MB 输入：

| 运行配置                                                | JSON     | CSV           |
| ------------------------------------------------------- | -------- | ------------- |
| 仅 kernel（输入/输出常驻显存，无 H2D/D2H）              | **62.5** | **71.6** GB/s |
| + 每次 H2D 输入拷贝                                     | 3.81     | 6.61 GB/s     |
| + 结果 D2H 拷回 host（仅 structural，pair 改为 opt-in） | 1.78     | 3.91 GB/s     |

纯 kernel 有 60–70 GB/s（𝔖 的并行计算完全成立），但端到端只有 1–3 GB/s：
**99% 的时间花在 PCIe 搬运上**。`nsys` 归因：Device-to-Host 占 memop 时间的
**63.7%**（主要是把 structural index 与 pair_pos 全量拷回 host），Host-to-Device
占 **35.1%**（67 MB 输入，pageable 实测 7.9 GB/s；pinned 12.4 GB/s）。

对照 simdcsv（9.15 GB/s）它在 RAM 里单趟完成、无 PCIe；我们的“端到端”却要把
67 MB 搬进、把 \~68 MB（8.5M token × 4B × 2 数组）搬出。**这不是算法慢 3.5×，
是 harness 把 Θ(n) 的移动算进了吞吐。**

## 9. 修正后的扫描代数 𝔖′：给原语加“放置 + 融合 + 流式”代价

把 schedule 纳入 IR。对输入 n 字节、产出 k 个结构 token 的流水线：

```
T = T_move_in(n) + T_compute(n, k) + T_move_out(k) + T_intermediate
```

- `T_compute` 由 R1–R7 给出（下表），在 dispatch 显存上可达 60–70 GB/s；
- `T_move` 与 `T_compute` **互相独立**，且在本机 `T_move ≫ T_compute`；
- `T_intermediate`：每个原语若把整段中间数组落回显存再读，就是 Θ(n) 额外搬运。

| 原语                   | work        | depth    | 备注                  |
| ---------------------- | ----------- | -------- | --------------------- |
| R1 分类                | O(n)        | O(1)     | 每字节独立            |
| R4 幺半群扫描          | O(n)        | O(log n) | carry 跨 word         |
| R5 压缩                | O(n)        | O(log n) | popcount+scan+scatter |
| R6 良嵌套              | O(k log k)  | O(log k) | 排序主导              |
| 移动（输入/输出/中间） | Θ(n+k) 字节 | —        | **本机瓶颈**          |

**推论（成本定理）**：𝔖 保证 `T_compute = O(n / BW_gpu) · polylog`，但端到端吞吐
`n / T` 由 `max(T_compute, T_move)` 决定。若不融合原语、不把稀疏中间量/结果留在
显存、不做流式输入，则 `T_move` 主导，𝔖 的并行性对吞吐**不可观测**。

**CSV 之所以暴露问题**：其结构性密度低（6/47 ≈ 13%）、无嵌套，`T_compute` 近乎免费，
于是固定的 `T_move`（进 67 MB、出 68 MB）成为全部成本。JSON 计算更重，但在本
harness 下同样被 `T_move` 主导。

**对编译器的要求（𝔖′ 新增的 pass）**：

1. **placement**：选择中间量/结果是 device-resident 还是 host-visible；R5 的输出默认
   应链式喂给 R6/stage2，而不是逐次拷回。
2. **fusion**：R1∘R3∘R4 合成单趟（simdjson/simdcsv 的做法），避免逐原语物化 Θ(n) 数组。
3. **streaming**：输入分块 + pinned + async，与 compute overlap；这正是 cuJSON 分块的原因。
4. **observable boundary 明确**：benchmark 要区分“结果驻留显存”的 `T_compute` 与
   “端到端含 PCIe”的 `n/T`，不可混为一谈。

这一修正不改变 §1–§7 的正确性，但推翻“𝔖-可归约 ⇒ 高吞吐”的隐含断言：**𝔖 刻画的是
计算并行性，不是端到端性能；后者需要额外的调度/代价层。**

## 10. 相关工作与 novelty 定位

𝔖 的代数部件都是已知结果，本文的贡献不在此：

| 部件                           | 已有工作                                                    |
| ------------------------------ | ----------------------------------------------------------- |
| 变换幺半群 + 并行前缀扫描      | Blelloch, _Prefix sums and their applications_ (1990)       |
| 自动机即幺半群 / 正则语言      | 教科书（Kleene、DFA→变换幺半群）                            |
| 栈 → 良嵌套归约 / 深度排序配对 | cuJSON, ASPLOS'26                                           |
| 括号匹配幺半群（stack monoid） | Raph Levien, _The Stack Monoid_ (2020)                      |
| 良嵌套递归 = visibly pushdown  | Alur–Madhusudan, _Visibly Pushdown Languages_ (STOC'04)     |
| SIMD 结构化索引                | simdjson；simdcsv                                           |
| GPU JSON / 多格式              | cuJSON (ASPLOS'26)；GpJSON (VLDB'25)；Mison、Sparser、Pison |

**Novelty 应定位为 systematization + compiler，而非代数本身**：𝔖 把上述部件统一成
一条“文法 → 原语 → 调度”的**机械推导**，并显式区分计算并行性与移动代价。这一点在
现有工作中没有人作为编译器 pass 来做。

## 11. 实现与模型的对齐状态（诚实清单）

| 模型条目                   | 实现状态                                                                                                                              |
| -------------------------- | ------------------------------------------------------------------------------------------------------------------------------------- |
| 前端（无模式识别）         | **已实现**：Brzozowski 导数 DFA + FIRST/LAST 推导；`masking_states` 作为 mode/comment 分类与统一掩码语义的驱动                        |
| P0 正确性闸门              | **已实现**：`bracket_guard`（充分结构判据）、`notp_ok`、`require_supported`/`fallback_rules`、comment 排除                            |
| R1,R3,R4,R5,R6             | 已实现（CPU + CUDA），有等价性/正确性测试                                                                                             |
| P1 IR 表驱动 DFA           | **P1a/P1b 完成**：`scan_plan` 携带 per-rule `lex_desc`（affine/doubled/comment/table），CPU 按状态变换前缀积执行；块注释（Table）正确 |
| P1c GPU 消费 descriptor    | **完成（P1c）**：GPU 按 per-rule desc 执行；单 mode 的 affine/doubled 走 `prefix_xor`/奇偶快路径，其余走**融合的幺半群前缀扫描**；JSON/CSV/TOML/clike 的 GPU↔CPU↔参考全量校验通过 |
| P1d.0 前端去分配           | **完成**：固定容量 `arena`（无 vector/string），nvcc 可 constexpr 实例化 clike 的 table plan |
| P1d.1 融合 + 特化          | **部分**：所有 desc 融合为**单趟打包 uint64 扫描**（绝对槽编码 + 256 项 per-byte 变换表）；TOML 设备 9→22 GB/s（目标 ≥30 未达）；JSON/CSV 不回退 |
| P1d.2 特化等价性证明       | **未实现**（目前靠差分随机测试） |
| P1d.3 全量/对抗 GPU 校验   | **部分**（结构索引全量互验；`unclosed_mode`/pair 未在 GPU 侧校验） |
| R7 结合递归                | **未实现**                                                                                                                            |
| R7 结合递归                | **未实现**                                                                                                                            |
| GPU 多 mode / 行注释       | **未实现**（TOML 计划可派生，但 GPU 只支持 1 mode、无 comment 通道）                                                                  |
| CPU SIMD 后端              | **未实现**（`model/scan.hpp` 是标量）                                                                                                 |
| 文法数                     | JSON / CSV / TOML（TOML 计划已验证；GPU/表驱动待 P1）                                                                                 |
| placement/fusion/streaming | **部分**：pair 拷回 opt-in（JSON +57%、CSV +55%）；pinned/async/overlap 未做                                                          |

## 12. 一条修正后的评估结论

- JSON：`T_compute` 与 cuJSON 同量级；端到端受 `T_move` 限制。
- CSV：算法不慢（kernel 71 GB/s）；端到端慢是搬运与结果物化。
- 下一步优先级：placement/fusion/streaming（最低成本、直接抬高所有格式），
  然后 GPU 多 mode + 注释（解 TOML），再补 R7 与 CPU SIMD，最后扩文法与端到端。

---

# 补充：benchmark 边界与三口径规范（D.5）

## 框架边界：只做“边界 + 内核 + 分块”，不做传输

`include/pars/backend/cuda/pipeline.cuh` 现在**没有任何 `getenv`**，bulk H2D/D2H
**只存在于 `scan_host`**（标注为 adapter，调用方可替换）。API：

- `scan_device<S>(d_data, n, stream, want_pairs)`：**零拷贝**。输入已在显存，
  内部只有必要的 4 字节标量读回（计数/校验位）；结果留在显存
  （`device_result{structural, pair_pos, n_struct}`）。
- `scan_host<S>(h_data, n, want_pairs)`：唯一含 bulk 传输的适配器（pinned+async）。
- `chunk_carry{scan, out_base}`：跨 chunk 的前缀幺半群元素 + 压缩输出偏移（D.3，待实现）。
- 编译期 `static_assert(total_packed_states<S>() <= 16)`：融合编码的状态预算，
  超限直接编译错误，杜绝静默 no-mask / 移位 UB。

## 三口径（每行带 tier）

- **N1 device-resident**：输入计时外上传一次，循环 `scan_device`，结果留显存 →
  生成器的计算 roofline。
- **N2 on-device pipeline**：同 N1，动机场景为“数据经 GDS/上游 kernel 到达”（数值同 N1）。
- **N3 host-boundary**：`scan_host`（pinned+async）。天花板
  `BW · n / (n + k·w)`，其中 `BW` 为实测 pinned 带宽，`k`=输出 token 数，`w`=每 token 字节。

硬件事实写进输出头：`nvidia-smi` 的 device 名 + 实测 pinned H2D/D2H 带宽（本机 12.5/13.1 GB/s）。

## 输出 schema

```
# format,impl,tier,GBps,bytes_moved,input_bytes,tokens
```

## 当前结果（67 MB，RTX 4060 Laptop，`bench_boundary`）

per-stage（ms，一次扫描）：`json p1=1.05 p2=2.10 p3=3.06 p4=6.65` / `toml p2=2.70 p4=1.84` /
`csv p2=0.15 p4=0.00` / `clike p2=2.48 p4=0.00`。

| format | N1a（P1–P3，对标 simdcsv index） | N1b（+P4） | N3 host |
|---|---|---|---|
| json | 11.8 | 5.2 | 1.64 |
| csv | 36.8 | 37.0 | 3.78 |
| toml | 15.3 | 10.9 | 3.10 |
| clike | 18.1 | 18.1 | 3.95 |

正确性：**全量 + 随机对抗** 四格式 GPU↔CPU↔naive 逐索引一致（`full=1 random=1`）。

### 归因与修复记录

- **P0 实测纠正了早期误判**：JSON 从 60 掉到 4.2 的主因是**通用 P2**（desc 幺半群扫描），
  不是 P4——更早的“P4 主导”结论无代码证据。
- **P1 修复**：把 P2 的 `pk_compose64` 槽宽从固定 16 收窄到 `total_states`，并把
  `byte_t`/`rep_of`/`masked` 暂存到 **shared memory**（原来从 kernel 参数按发散地址
  读取，代价高）。效果：`p2` JSON 5.98→2.11、TOML 7.94→2.72、clike 5.61→2.48（~2.8×）；
  N1a：JSON 7.1→11.8、TOML 7.1→15.3、clike 9.8→18.1。
- 修复后 **P4 成为 JSON 的新瓶颈**（7.10 ms）→ 已按 profiling 换用 **`thrust::sort_by_key`（int 键，radix-backed）** 替代 `stable_sort_by_key`，并 clamp 负 depth；JSON `p4` 7.10→6.65 ms，TOML 1.87→1.84，全部校验仍绿。P4 仍受 O(k) 的 `inclusive_scan`（10M）+ 排序主导，与 cuJSON 同样用 GPU 排序，量级相当。
- 旧前缀转义快路径（`escaped_scan`/`ComposeF2`）与 desc 语义不等价，已删除死分支；
  前缀转义目前走**与 desc 同表的收窄幺半群扫描**（语义必然一致）。

### 与 simdcsv 对比（CSV，多 scale）

| scale | pars N1a（stage-1，device） | pars N3（host 边界） | pars N3c（chunk overlap） | simdcsv |
|---|---|---|---|---|
| 16 MB | 37.4 | 3.03 | 7.00 | 10.8 |
| 64 MB | 42.3 | 3.75 | **9.95** | 9.54 |
| 256 MB | 34.8 | 3.35 | **10.0** | 9.45 |

- **device-resident（各自读本命内存层）**：pars N1a ~35–42 vs simdcsv ~9.5 → **~4×**。
- **host 边界（含 PCIe）**：非重叠 N3 仅 ~3.5（≈2.7× 慢）；**chunk carry + pinned 双缓冲 overlap
  后 N3c 在 64 MB 及以上达到/超过 simdcsv（~10.0 vs 9.5）**。16 MB 受启动/开销限制（7.0）。
- 结论：端到端“追平 simdcsv”在 ≥64 MB 已实现（overlap 后）；小规模受 fixed overhead 限制。

> P3 说明：`scan_chunk` 目前只支持 **parity（no-escape/doubled）路径**（CSV）；
> JSON/TOML/clike 的通用 carry 尚未实现（`static_assert` 拦截）。stream 已贯通
> `scan_device`；标量同步从 9 次降到约 2 次（P3 counts 1 + P4 1）。

> 待办：JSON/TOML/clike 的通用 chunk carry（table/多 mode）尚未实现（`scan_chunk` 仅 parity）；P4 仍有 1 次计数同步。
