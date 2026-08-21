# Trigram SIMD 微基准（Apple M1）

**日期**：2026-07-24  
**环境**：Apple M1、arm64、Apple clang 21.0.0、`-O2`  
**命令**：`make benchmark-trigram-simd`

## 测试范围

基准分别使用 8 MiB ASCII 和中文 UTF-8 字节流，测量连续三字节打包为
`uint32_t` 的吞吐量。每项执行 9 组并取中位数。

- `scalar`：显式关闭编译器循环向量化。
- `compiler auto-vectorized`：普通 C++ 循环，由 clang 自动优化。
- `explicit NEON`：每轮加载三个错位的 128-bit 向量，生成 16 个 trigram。
- `Current extractTrigrams algorithm`：复刻当前实现的逐字节小写、24-bit bitmap
  去重、dirty bitmap 清理和结果分配，用 100,000 个模拟文件名测试。
- `Optimized production extractTrigrams`：实际生产实现。使用滚动三字节窗口，
  每个输入字节只做一次 ASCII 小写转换；短文件名用局部 `sort + unique` 去重，
  长输入继续使用 thread-local 24-bit bitmap。

打包测试不包含去重、哈希索引访问和 posting-list 操作，因此只表示优化打包步骤的上限。

## 结果

| 数据 | 实现 | 中位耗时 | 吞吐量 | 相对强制标量 |
|------|------|---------:|-------:|-------------:|
| ASCII | 强制标量 | 157.765 ms | 1.58 GiB/s | 1.00x |
| ASCII | clang 自动向量化 | 39.361 ms | 6.35 GiB/s | 4.01x |
| ASCII | 显式 NEON | 44.764 ms | 5.58 GiB/s | 3.52x |
| 中文 UTF-8 | 强制标量 | 161.712 ms | 1.55 GiB/s | 1.00x |
| 中文 UTF-8 | clang 自动向量化 | 39.652 ms | 6.30 GiB/s | 4.08x |
| 中文 UTF-8 | 显式 NEON | 47.133 ms | 5.30 GiB/s | 3.43x |

完整提取算法：

| 数据 | 旧实现 | 优化后的生产实现 | 加速比 |
|------|-------:|-----------------:|-------:|
| ASCII（100,000 个文件名） | 39.067 ms | 24.318 ms | 1.61x |
| 中文 UTF-8（100,000 个文件名） | 30.705 ms | 17.174 ms | 1.79x |

## 结论

1. 中文 UTF-8 与 ASCII 的纯 byte-trigram 打包吞吐基本相同，字符语义不影响这一步。
2. clang 优化报告确认简单打包循环以 16-wide 向量化；本次手写 NEON 比自动向量化慢约 12%–16%。
3. 完整提取吞吐比纯打包低约两个数量级。滚动窗口和短输入局部去重已经进入
   `TrigramExtraction.h`，ASCII 延迟降低约 38%，中文 UTF-8 延迟降低约 44%。
4. 中文仍按 UTF-8 字节生成 trigram；滚动窗口恰好复用相邻 trigram 的两个字节，
   但不要求一个中文字符或一个 trigram 恰好装满一条 SIMD 指令。
5. 当前不采用手写 NEON：clang 对纯打包循环的自动向量化更快，而完整算法的
   主要成本在去重、分配和索引访问。
6. 这些数据不能说明 posting-list 求交是否适合 SIMD；那是独立的数据结构基准。
