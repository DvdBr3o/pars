# 扫描代数 𝔖′：唯一同态 + 可压缩性

> 本文替换 `docs/model/model.md` 第 1–7 节的"R1–R7 原语拼接"表述。核心变化：
> 不再是七个原语的机械组合，而是**一个同态 + 一个判据 + 四种幺半群实例**。
> 旧的 R1–R7 被证明只是同一构造在不同幺半群上的实例（见 §6）。

## 0. 一句话

> **一个确定性解析器，就是自由词幺半群 `(Σ*, ++, ε)` 到某个"摘要幺半群"
> `(S, ⊗, e)` 的一个同态。** 解析一个词 `w` 就是计算它在 `S` 里的**前缀积**；
> 而前缀积是并行的前缀扫描。它能以 `O(n)` work、`O(log n)` depth 数据并行的
> **充要条件**是这段摘要"可压缩"（§2）。旧 R1–R7 是它的四个实例（§3、§6）。

## 1. 唯一的核心对象：span summary 与同态

对输入字节流 `w`，记区间 `[i,j)` 的含义为

```
σ(i, j) ∈ S          // 段 [i,j) 的"摘要"，S 是某个幺半群
```

解析的结构由一条同态律给出：

```
σ(i, k) = σ(i, j) ⊗ σ(j, k)        （⊗ 结合，e = σ(i, i)）
```

于是整个输入的摘要就是逐字节元素的前缀积：

```
σ(0, n) = τ_0 ⊗ τ_1 ⊗ … ⊗ τ_{n-1}
```

这正是**一次并行前缀扫描**。整个框架只有**一个原语**：

```cpp
template<Summary S>
void prefix_reduce(S* p, std::size_t n);   // Hillis–Steele, O(n) work, O(log n) depth
```

（见 `include/pars/algebra/summary.hpp`。）所有"快速路径"都是这个函数在某个 `S` 上的实例化。

## 2. 并行性判据：可压缩性

> `σ` 可**压缩** ⟺ 存在一个（有限或有界规范的）幺半群 `M` 和满同态 `π : Σ* ↠ M`，
> 使 `σ` 经过 `M` 因子化，且 `M` 的元素大小有界。

- 可压缩 ⇒ 解析退化成 `prefix_reduce` ⇒ `O(n)` work、`O(log n)` depth、`O(n/8)` 级元数据；
- 不可压缩 ⇒ 摘要无界（栈内容/回溯状态不可压缩）⇒ 只能做区间不动点，`O(n²)`。

**这是唯一的判据。** 一个文法"能不能并行、有多快"，由它的摘要落在哪个幺半群决定，
而不是由任何模式匹配决定。

## 3. 四种 schema：可压缩的四种方式

| schema | 幺半群 `S` | 摘要是什么 | 覆盖的文法构造 | 原语 |
| --- | --- | --- | --- | --- |
| `finite` | 有限变换幺半群 `S→S`（DFA） | "入口状态→出口状态" | 正则词法（引号、转义、注释、数字） | `prefix_reduce` |
| `stack` | 差列表/栈幺半群 (Levien) | 一段消费/产出的栈串（单括号类型时=未配对数对） | visibly-pushdown 递归（括号、call/return） | `prefix_reduce` |
| `tree` | 自由树 / 结合幺半群 | `reduce(op, leaves)` | 单自递归且算子可结合（`e op e`、`A (op A)*`） | `prefix_reduce` |
| `choice` | 优先级 `min` 幺半群 | 第一个成功的备选（rank） | PEG 的 ordered choice `/`（判别性） | `prefix_reduce` |
| `interval` | —（不可压缩） | 段上的解析集合/不动点 | 无界回溯、非良嵌套、无界前瞻 | 区间 DP / Valiant（`O(n²)`） |

四个可压缩 schema **全部**由同一个 `prefix_reduce` 计算；`interval` 是唯一的兜底，
也是唯一不承诺 SOTA 的路径。

## 4. PEG 与 CFG：ordered choice 为什么是幺半群

关键事实（§3 表的 `choice` 行）：

- PEG 的 `/` **是结合的**（`(A/B)/C = A/(B/C)`），但**不分配于连接**
  （`(A/B)C ≠ AC/BC`，因为 commit 禁止回溯）；
- 因此 PEG 不是普通半环，但仍是幺半群：**"取第一个成功"= 对成功者按优先级取 `min`**，
  这是结合、可交换、幂等的运算：

```
ranked_choice.compose(a, b) = a.matched ? (b.matched ? (a.rank<=b.rank?a:b) : a) : b
```

- 而 CFG 的**无序并**是**集合值半环**：它分配于连接（`(A∪B)C = AC ∪ BC`），
  所以能上 CYK/Valiant/闭包——但代价是"记住所有解析"，更重。

**结论**：PEG 的 ordered choice 不比 unordered 难代数化；相反，确定性使摘要更小。
真正决定快慢的不是 PEG vs CFG，而是**摘要是否可压缩**：
判别性 choice（首字符集在有界窗口 `W` 内不相交）⇒ 确定性下推 + 有界前瞻 ⇒ `finite`/`stack`
schema ⇒ `O(n)`；非判别性 ⇒ `interval`。

## 5. 代价函子与分级编译

给文法 `G` 定义代价函子：

```
cost(G) = ( schema, |M_G|, work, depth, mem )
```

编译器对每条规则选"可精确表示且代价最低"的 schema：

```
PEG 文法
  │ [前端] 引用图 + FIRST_W(判别性) + 递归分类 + 幺半群模式检测
  │        → 每条规则标记 tier
  ├─ T0  finite/stack/tree/choice  → prefix_reduce（与 cuJSON 同量级）
  ├─ T1  多类型 stack / call-return → prefix_reduce
  ├─ T2  interval 兜底             → O(n²)，只保证正确性
  └─ T3  投机 + 验证
  ▼
core::plan = { cell(schema, atom-table, mask) } → prefix_reduce
```

## 6. 与旧 R1–R7 的严格对应

| 旧 | 在 𝔖′ 中的身份 |
| --- | --- |
| R1 分类 | `finite` 语义元素的一个字节原子（`byte → class`），不再是独立原语 |
| R2 状态→掩码 | `finite` 的"处理前状态"读法（前缀积在字节 i 的取值） |
| R3 分支→选择 | 掩码 blend：`x = (m&f) \| (~m&g)`，是 `finite` 的逐字节构造，不是并行原语 |
| R4 前缀状态→幺半群扫描 | **就是** `finite` 上的 `prefix_reduce` |
| R5 过滤→压缩 | 与 schema 无关的**输出算子**（popcount+scan+scatter），仍在 |
| R6 栈→良嵌套归约 | **就是** `stack` schema |
| R7 结合递归→幺半群归约 | **就是** `tree` schema |
| （新）ordered choice | `choice` schema（PEG `/`；判别性时） |

即：R1–R3 是"把有界状态程序写成语义元素"，R4/R6/R7 是三种幺半群的 `prefix_reduce`，
R5 是与 schema 无关的输出阶段。**没有独立的七个原语，只有 `prefix_reduce`。**

## 7. 边界（必须诚实）

- **通用 PEG 的近线性并行不成立**：通用 CFG 并行解析已知最好 `O(n³)`/`O(n^ω)` work，
  PEG ⊋ CFG 只会更难；`interval` 兜底因此不承诺 SOTA。
- **语义动作**：构值需用户提供结合语义才能进 `tree`；否则只能顺序/投机。
- **无界前瞻 / 非良嵌套 / 左递归**：落 `interval`。
- **移动/放置**：即便计算 `O(n)`，端到端仍受 PCIe 限制（见 §8 的 N2/N3）。

## 8. 实现与评测（NVIDIA A100-PCIE-40GB，256 MB，CUDA 12.6）

代码布局（𝔖′ 版本）：

```
include/pars/algebra/summary.hpp   Summary 概念 + prefix_reduce + 四个幺半群实例
include/pars/core/plan.hpp         新 IR：plan = { cell(schema, ...) }
include/pars/core/lower.hpp        文法 -> plan（含 T0 schema 检测）
include/pars/core/host.hpp         host 执行器（prefix_reduce 驱动）
include/pars/core/tree.hpp         tree schema（host reduce）
include/pars/core/cuda.hpp         GPU：finite/stack 扫描 + tree 归约
```

测试（`xmake build pars.test && xmake run ...`）全部通过：
`model` / `algebra` / `core` / `meta` / `meta.gpu`。

| 指标 | 数值（256 MB JSON，A100） |
| --- | --- |
| JSON 结构索引（R1–R5，device） | 39.8 GB/s |
| JSON + 括号配对（+R6） | 19.5 GB/s |
| tree schema 归约（device） | **48.2 GB/s** |
| 显存工作集 | 从 7–9× 降到 ≈2.9× 输入（−380 MiB） |
| vs cuJSON（同 interval，no-D2H） | 持平/略优 |

> 状态：`finite` / `stack` / `tree` / `choice`(判别性) 已实现并有 GPU 后端；
> `interval` 兜底与多模式 fusion/carry 为待办（不影响 §0–§6 的代数正确性）。
