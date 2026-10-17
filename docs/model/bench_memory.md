# 内存 / 显存 benchmark 口径

`fig_memory` 的数据来自 `paper/plots/data/memory.csv`，由 `xmake build pars.paper.data`
自动生成（**不再手填**）。

## 为什么重写

旧版 `bench_memory.cu` 在同一进程里跑全部 6 种文法，并把
`cudaMemGetInfo` 的一次性 `free_before - free_after` 当作占用：

- 每个文法的 `thread_local` 设备缓存整进程存活，后一个文法会继承前一个的
  分配与 CUDA caching allocator 的池状态，`cudaMemGetInfo` 的差值失去意义；
- 在共享 GPU 上同一文法会从 1.0× 漂到 80×（观测值），数字挂到哪个格式全凭时序；
- 旧 CSV 里 JSON 的 1.03× 是把 **16 分块**扫描的 per-chunk 缓存（28 MiB）
  当成整段工作集，真实 `scan_device` 工作集是 5.9×。该值当时是在 CSV 里
  手改的，提交未改任何测量代码。

## 现在的口径

`bench/bench_memory.cu <lang> <scale_mb>` — 一个进程只测一种语言：

1. 先做 2 次 warmup，让持久缓存达到高水位；
2. 记录 warmup **前**的 free VRAM 与 warmup **后**的 free VRAM，
   差值即**稳态工作集**（随后再多跑几次并断言不再增长）；
3. 报告两行：
   - `placement=device`：输入常驻显存、索引留显存（`scan_device`，与 cuJSON 同 interval，无 D2H）；
   - `placement=host`：公开 `scan()` 的流式边界（分块 H2D 重叠 + 索引 D2H）。

`bench/bench_mem_sota.cpp <lang> <scale_mb>` — host 基线（simdjson / toml++ /
pugixml / inih / flex），构建一次 DOM 后按 `/proc/self/statm` 采样**存活 DOM 的 RSS
增长**；结果经 `volatile` sink 消费以防被优化掉。

`bench/bench_mem_simdcsv.cpp <scale_mb>` — CSV 基线。simdcsv 的程序按最坏情况
预分配 `uint32_t[p.size()]`（4× 输入），那不是工作集；本探针链接它真正的
`find_indexes`，报告**索引实际保留的大小** `n_indexes × 4`。

cuJSON 没有内置显存输出，由 `bench/measure_child_gpu.py` 采样 `nvidia-smi`
峰值，`paper_data` 取 5 次中位数，并采样 `CUDA_VISIBLE_DEVICES` 实际用的那张卡。

## 解读

- **只有同 placement 可比**：`pars·device` vs `cuJSON`；`pars·host` 与 CPU 解析器
  都会把 DOM/索引放进 RAM，但不含 GPU 输入驻留。
- `device` 工作集主要由两部分组成：按 `n/32` 字分配的 mask/scan 缓存，和
  括号密集格式的 `openclose` 临时缓冲（`n_oc × 25 B`）。JSON 括号密度高，
  所以 `device` 比例最大（~5.9×），与 cuJSON（~7.8×）同量级。
- `host` 比例小（<1×）是因为分块扫描只需 per-chunk 缓存；它是吞吐与占用
  的权衡，不是"省显存"的免费午餐。
