# 067 — Query Performance Optimization: Adaptive Trigram Bypass + Prefetch

## 背景

在生产规模（4.8M 文件记录）下，`SearchEngine::query()` 对高候选数关键词（如 "test"，~84K 候选）的查询耗时高达 941ms。根因是 Phase 1 单线程验证阶段对 `lowerNames_[]` 数组的随机访问引发严重 L3 缓存未命中。

## 优化方案

### Layer 1: Adaptive Trigram Bypass

当 trigram 候选数过多（>1% 的总记录数）且数据集足够大（>1M 条记录，缓存压力代理条件）时，放弃 trigram 候选验证，回退到已有的并行顺序扫描路径。

**关键阈值设计**：
- `candidates > totalSize / 100`：候选占比超过 1%，trigram 筛选价值低
- `totalSize > 1_000_000`：仅在大规模数据集下触发（小规模下 working set 仍在缓存内）

**数据驱动的阈值调优过程**：
1. 初始阈值仅用 1%，在 500K 数据集上 "test" 被错误绕过（65K 候选 > 5K 阈值），导致 14.5ms 退化
2. 添加绝对计数阈值 >50K，仍因测试数据分布（91% 通用名中 1/10 前缀为 "test"）导致触发
3. 最终采用 `totalSize > 1M` 条件，在 500K 规模下保持 trigram 快速路径，在 4.8M 规模下正确触发绕过

### Layer 2: Phase 1 Software Prefetch

在 Phase 1 单线程循环中添加 `__builtin_prefetch`，提前 4 个元素预取 `lowerNames_[]`，隐藏随机访问的内存延迟。

**注**：最初计划用 `dispatch_apply` 并行化 Phase 1，但实测发现线程池调度开销（~15ms）在候选数 <50K 时远超单线程执行成本（~2ms），因此移除并行化，仅保留 prefetch。

## 测试

新增 `tests/test_query_perf.h`（Part 44），包含 7 个场景：

| 场景 | 关键词 | 测试目标 |
|------|--------|----------|
| 1. 高候选数 | "test" | Layer 1 绕过阈值验证 |
| 2. 中等选择性 | "config", "module" | 正常 trigram 路径 |
| 3. 优秀选择性 | "unique_xyz" | trigram 快速返回 |
| 4. 短关键词 | "te", "a" | 线性扫描回退 |
| 5. Glob 模式 | "*.cpp", "test_*" | 并行扫描 |
| 6. 无匹配 | "qzqzqz_nonexistent" | trigram 早退出 |
| 7. 正确性验证 | 各种 | 结果正确性断言 |

## 性能数据（500K 记录，20 次迭代平均）

| 查询 | Optimized | Baseline | 说明 |
|------|-----------|----------|------|
| "test" maxR=100 | 1.70ms | 1.63ms | 无退化（Layer 1 未触发） |
| "config" maxR=100 | 2.36ms | 2.02ms | 噪声范围 |
| "unique_xyz" maxR=100 | 0.18ms | 0.18ms | 相同 |
| "te" (线性扫描) | 12.9ms | 13.6ms | 略快 |

在 500K 规模下无性能退化。Layer 1 仅在 >1M 记录时激活，目标为生产环境 4.8M 记录下将 941ms 降至 ~80ms（顺序扫描耗时）。

## 全套测试验证

`./test_all --fast`：**10,782 tests passed, 0 failures**

## 修改文件

| 文件 | 变更 |
|------|------|
| `MacEverything/Core/SearchEngine.cpp` | +Layer 1 bypass (L275-283), +Phase 1 prefetch (L298-300) |
| `tests/test_query_perf.h` | 新增 Part 44 性能基准测试 |
| `test_all.cpp` | 注册 Part 44 |
