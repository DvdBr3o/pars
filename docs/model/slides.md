# 𝔖′ PPT 草稿（8 页）

> 每页 `#` 是一页。配图可用 `paper/public/json_case/*.png`（例子）与
> `paper/src/figures/framework.tex`（管道图）。

---

# Slide 1 — 标题

## 从文法到并行解析：一个同态

**Parsing is one monoid homomorphism.**

- 旧版：R1–R7 七个原语拼接
- 新版 𝔖′：**一个同态 + 一个可压缩性判据 + 四个幺半群实例**
- 结果：JSON 与 cuJSON 持平；grammar 覆盖面扩展到 PEG 的可压缩片段

---

# Slide 2 — 核心对象

## 解析 = 前缀积

```
σ(i,k) = σ(i,j) ⊗ σ(j,k)          （同态：段摘要可结合合并）

σ(0,n) = τ₀ ⊗ τ₁ ⊗ … ⊗ τ_{n-1}    （整段 = 逐字节元素的前缀积）

O(n) work / O(log n) depth  ⟺  前缀扫描
```

**唯一的原语**：`prefix_reduce`（Hillis–Steele 前缀积）。
所有"快速路径"都是它在某个幺半群 `S` 上的实例。

---

# Slide 3 — 唯一的判据

## 摘要可压缩

> `σ` 可压缩 ⟺ 存在有限/有界幺半群 `M`，使 `σ` 经 `M` 因子化。

```
可压缩  → prefix_reduce → O(n) work, O(log n) depth, O(n/8) 元数据
不可压缩 → 区间不动点   → O(n²)（兜底，不承诺 SOTA）
```

"文法能不能并行、有多快" = "摘要落在哪个幺半群"。**没有模式匹配。**

---

# Slide 4 — 四个 schema（同一原语）

| schema | 摘要 `S` | 覆盖 | 例子 |
| --- | --- | --- | --- |
| finite | 有限变换幺半群（DFA） | 正则词法 | JSON 字符串 `"…"` |
| stack | 栈/差列表幺半群 | visibly-pushdown | 括号 `(())` |
| tree | 结合幺半群 | 单自递归 | `1+2+3`、`e op e` |
| choice | 优先级 `min` 幺半群 | PEG `/` | `A / B` |
| interval | —（不可压缩） | 无界回溯 | 兜底 |

四行都由**同一个** `prefix_reduce` 计算。

---

# Slide 5 — PEG vs CFG

## ordered choice 是幺半群

- PEG 的 `/`：结合、**不分配**（commit，无回溯）→ 不是半环
- 但"取第一个成功" = **对成功者取 min(rank)** → 幺半群！
- CFG 的无序并 = **集合值半环**（更重）

> 决定快慢的不是 PEG vs CFG，而是**摘要可压缩性**。
> 判别性 choice（首字符集在有界窗口内不相交）⇒ finite/stack ⇒ O(n)。
> **PEG 不但能保留，而且摘要更小。**

---

# Slide 6 — 分级编译 + 代价函子

```
cost(G) = (schema, |M_G|, work, depth, mem)

PEG 文法
  │ 前端：引用图 + FIRST_W + 递归分类 + 幺半群检测
  ├─ T0 finite/stack/tree/choice → prefix_reduce（SOTA）
  ├─ T1 多类型 stack / call-return
  ├─ T2 interval 兜底（O(n²)，仅正确性）
  └─ T3 投机 + 验证
  ▼
core::plan = { cell(schema, …) } → prefix_reduce
```

---

# Slide 7 — 评测（A100-40GB，256 MB）

| 指标 | 𝔖′ | 对照 |
| --- | --- | --- |
| JSON 索引（R1–R5, device） | 39.8 GB/s | cuJSON ~8.3（no-D2H） |
| JSON + 配对（R1–R6） | 19.5 GB/s | — |
| tree 归约（device, R7） | **48.2 GB/s** | — |
| 显存工作集 | ≈2.9× 输入 | 旧版 7–9× |
| 两个 consumer 变体 | ondemand 1.14 / dom 0.62 GB/s | simdjson 1.01 / 0.53 |

测试：model / algebra / core / meta / meta.gpu 全绿。

---

# Slide 8 — 边界（诚实）

- **通用 PEG 近线性并行不成立**：CFG 并行最好 `O(n³)/O(n^ω)` work。
- `interval` 兜底正确但不承诺 SOTA；cuJSON 也不处理这类文法。
- 语义动作需结合语义；无界前瞻/左递归/非良嵌套 → `interval`。
- 端到端仍受 PCIe 限制（放置/融合/流式是独立的代价层）。

> **一句话**：解析是一个同态；并行是"摘要可压缩"；R1–R7 是同态在四个幺半群上的实例。
