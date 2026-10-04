# 跨格式 SOTA benchmark（多 scale）

## 调研结论：这些格式没有 SIMD/CUDA SOTA

| 格式 | 是否有 SIMD/CUDA SOTA | 采用的基线（本仓 `third_party/`） |
|---|---|---|
| JSON | 有：simdjson（CPU SIMD）、cuJSON（CUDA） | simdjson（sibling），cuJSON（sibling） |
| CSV | 有：simdcsv（CPU AVX2，作者同 simdjson） | simdcsv |
| TOML | **没有** | toml++ 3.4.0（最快的通用 TOML 解析器） |
| XML | **没有**（无主流的 SIMD XML） | pugixml（最快的通用 XML 解析器） |
| YAML | **没有**（rapidyaml 是 SOTA，但非 SIMD；因网络取回 1 MB 上限未 vendor） | —— |
| INI | **没有** | inih |

这本身支持项目的定位：**对没人优化过的语言，用同一套 IR 自动生成并行扫描器**。

## 口径说明（重要）

- `pars N1a` = **stage-1 结构索引**（分类 + 词法屏蔽 + 压缩；bracketed 格式含配对）。
- 基线（toml++/pugixml/inih/simdjson）= **完整解析 + DOM 构建**。两者工作不同，
  不能直接等同；`N1a` 显示的是生成器可达到的 stage-1 上限。
- 唯一**同类可比**的是 CSV：simdcsv 也是“引号外分隔符索引”。

## 数据（67 MB / 64 MB 为主，多 scale 见下）

机器：RTX 4060 Laptop + i9-13980HX；pinned H2D≈12.5、D2H≈13 GB/s。

| 格式 | pars N1a（stage-1, device） | pars N3c（chunk overlap, host） | 基线 | 基线吞吐 | N1a/基线 |
|---|---|---|---|---|---|
| JSON | 11.8 | ——（通用 carry 未做） | simdjson | 1.82 | ~6.5× |
| CSV | 42.2 | **9.98** | simdcsv | 9.54 | ~4.4×（N3c ≈1.05×） |
| TOML | 15.9 | —— | toml++ | 0.045 | ~350× |
| INI | 23.2 | —— | inih | 0.252 | ~92× |
| clike | 18.1 | —— | （无） | —— | —— |
| XML | **模型尚不支持**（标签配对需 VPL 扩展） | —— | pugixml | 0.42 | —— |
| YAML | **模型尚不支持**（缩进敏感） | —— | rapidyaml（未 vendor） | —— | —— |

## 多 scale（GB/s）

pars stage-1（N1a）与基线，随输入规模：

| 规模 | JSON pars / simdjson | CSV pars N1a / N3c / simdcsv | TOML pars / toml++ | INI pars / inih |
|---|---|---|---|---|
| 16 MB | 11.7 / 1.93 | 37.1 / 7.13 / 10.8 | 11.6 / 0.051 | 17.3 / 0.254 |
| 64 MB | 11.8 / 1.82 | 42.2 / 9.98 / 9.54 | 15.9 / 0.045 | 23.2 / 0.252 |
| 256 MB | 12.8 / 1.88 | 34.8 / 10.37 / 9.45 | 15.2 / 0.029 | 21.6 / 0.233 |

- CSV 端到端（N3c）在 64 MB 起与 simdcsv 打平/略胜；16 MB 受启动开销限制。
- 其余格式 par 的 N1a 随规模稳定；基线随规模略降（缓存/分配）。

## 复现

```bash
xmake build pars_bench_baselines && ./build/.../pars_bench_baselines <MB> <iters>
xmake build pars_bench_gpu       && ./build/.../pars_bench_gpu <MB> <iters>
# CSV 基线：third_party/simdcsv（见 README）
```

## 待办

- JSON/TOML/INI/clike 的**通用 chunk carry**（把 N3c overlap 从 CSV 扩展到全部格式）。
- XML 需要把 R6 从“单字符 Dyck”扩展为 **VPL call–return 字母表（带名标签）**；
  YAML 需要**缩进栈（ANSV）**原语。二者是模型扩展方向，也是最有说服力的 killer demo。
