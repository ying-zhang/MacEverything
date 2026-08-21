# 112 - 结构化查询回退的缓冲区扫描与路径 trigram 优化

## 问题

`bin/ls` 等结构化查询耗时约 440ms，原因如下：
- `namePattern` "ls" 仅 2 个字符，无法使用 trigram 索引
- `pathSegment` "bin" 在 `nameTrigramIndex_` 中候选过多（>66K），超出 trigram 阈值
- 回退到对全部 500 万条记录的线性扫描，涉及 4 路分散内存访问（records_、namePool_.entries_、namePool_.buffer_、pathIndices_）

## 根因分析

线性扫描的瓶颈是**缓存低效**：500 万次迭代中的 4 条独立内存流产生约 690 万次缓存行访问。CPU 预取器无法跟上 4 条不相关的内存流。

## 解决方案：三层优化

### 第一层：裸名称查询的缓冲区扫描（无路径段）
当不存在路径段时，使用 `simdFindAll()` 对连续的 `namePool_.buffer_`（约 75MB）进行 SIMD 扫描，然后通过前向游标遍历将命中字节偏移解析为记录索引。

- `namePool_.buffer_` 是单一连续的 `vector<char>` — 非常适合 SIMD 流式处理
- 命中偏移递增，条目按偏移排序 — 游标遍历为 O(hits + entries)，无需二分查找
- 通过验证 `hitOffset + patternLen <= entry.offset + entry.length` 来排除跨边界的误报

### 第二层：路径优先扫描（有路径段，无 trigram）
当存在路径段但没有任何段达到 3 个字符（无法使用 trigram）时，遍历 `lowerPathPool_` 中的全部约 10 万条路径，通过 `pathSegmentsMatch()` 过滤，然后仅遍历匹配路径下的记录（通过 `pathIdxToRecords_`）。

### 第三层：路径 trigram 加速扫描（路径段 >= 3 个字符）
使用现有的 `pathTrigramIndex_` 将候选路径从约 10 万缩小到约 1000 条，再应用 `pathSegmentsMatch()`。以 "bin/ls" 为例：
- `intersectPostingLists(pathTrigramIndex_, "bin")` 返回约 1000 个候选路径索引
- 仅对这些候选执行 `pathSegmentsMatch()` 并通过 `pathIdxToRecords_` 展开

## 性能结果

| 查询 | 优化前 | 优化后 | 加速比 |
|---|---|---|---|
| `bin/ls` | 440ms | **7-30ms** | 15-60x |
| `usr/lib` | ~400ms | **1.8ms** | ~220x |
| `etc/conf` | ~400ms | **18ms** | ~22x |
| `local/python` | 21ms | **3-4ms** | 无回归（路径锚点策略） |

## 修改文件

- `MacEverything/Core/SearchEngineStructuredQuery.cpp` — 将 `dispatch_apply` 线性扫描替换为：
  1. 通过 `pathTrigramIndex_` 进行路径 trigram 缩小范围
  2. 通过 `pathIdxToRecords_` 进行路径优先迭代
  3. 裸名称查询的缓冲区扫描 + 游标遍历
- `tests/test_structured_query.h` — 新增测试 29-32：缓冲区扫描正确性、跨边界排除、tombstone 排除、DIR_EXACT

## 测试

- 全部 11551 个单元测试通过
- 通过 HTTP API 进行集成测试（`curl http://localhost:19860/api/search?q=bin/ls`）
- 其他查询路径（trigram、路径锚点、树遍历）未观察到回归
