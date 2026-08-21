# 082 - 5GB 内存字符串搜索性能基准测试

## 概述

创建独立 C++ 基准测试程序，对比 7 种字符串搜索算法在 5GB 内存数据中查找 query 所有出现位置的性能，包含单线程和多线程（12 线程）测试。

## 规划

- 目标：全面对比从朴素到 SIMD 的字符串搜索算法在 Apple M3 Pro 上的实际表现
- 平台：Apple M3 Pro (ARM64, NEON/AdvSIMD 128-bit)，12 核，18GB LPDDR5
- 数据规模：5GB 随机小写字母，预插入 1000 个 needle 副本
- 每种算法执行 3 次，统计 min/avg/max

## 实施

### 测试算法（7 种）

1. `std::string::find` — C++ 标准库
2. `memmem` — POSIX C 库
3. KMP — Knuth-Morris-Pratt
4. Boyer-Moore-Horspool — 坏字符跳转表
5. Rabin-Karp — 滚动哈希
6. NEON first-last byte — ARM NEON 128-bit SIMD
7. NEON first-last byte (2x unroll) — 手动 2x 循环展开

### 多线程支持

通用多线程包装器，将数据分割为重叠块，独立搜索后合并去重。覆盖除 Rabin-Karp 外的 6 种算法。

## 关键结果

| 排名 | 算法 | 吞吐量(GB/s) | vs Baseline |
|------|------|-------------|-------------|
| 1 | NEON 2x [MT] | 74.30 | 60.7x |
| 2 | NEON [MT] | 50.42 | 41.2x |
| 3 | std::find [MT] | 12.59 | 10.3x |
| 4 | NEON 2x | 11.56 | 9.4x |
| ... | ... | ... | ... |
| 13 | Rabin-Karp | 0.14 | 0.1x |

## 文件变更

| 文件 | 操作 |
|------|------|
| `string_search_bench.cpp` | 新建 — 基准测试程序 |
| `docs/string_search_benchmark_report.md` | 新建 — 详细性能测试报告 |
