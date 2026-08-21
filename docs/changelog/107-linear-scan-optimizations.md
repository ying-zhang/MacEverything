# 107 — 结构化查询线性扫描优化

## 背景

Feature 104 基准测试揭示，SEGMENTS 查询在名称模式较短或过于常见时会回退到对全部约 450 万条记录的线性扫描，耗时 400-660ms。该线性扫描存在以下低效问题：

1. 单线程 — 未在多核机器上利用并行
2. 逐记录路径段匹配 — 每条记录都调用 `pathSegmentsMatch()`，但大多数记录共享相同的约 10 万条唯一目录路径
3. 冗余小写转换 — `pathSegmentsMatch()` 内部手动做小写转换，但来自 `lowerPathPool_` 的输入已经是小写
4. 堆分配 — `lowerPathPool_.str()` 每条记录返回 `std::string`（堆分配），而非零拷贝的 `string_view`

## 变更内容

### 优化 1：`dispatch_apply` (GCD) 并行化
- 将单线程循环替换为使用 `hardware_concurrency()` 个线程（上限 32）的 `dispatch_apply`
- 每个线程将一批记录处理到本地 `vector<Match>` 中，完成后合并
- 与 `queryLinearScanPath()` 和 `queryLinearScan()` 中的现有模式保持一致

### 优化 2：`pathMatchCache` 预计算
- 对约 10 万条唯一路径预先计算路径段匹配（O(100K)）
- 结果存入按 pathIdx 索引的 `vector<bool> pathMatchCache`
- 每条记录的查找从调用 `pathSegmentsMatch()` 降为 O(1) 的 `pathMatchCache[pathIndices_[i]]`

### 优化 3：移除冗余小写转换
- 将 `pathSegmentsMatch()` 参数从 `const std::string&` 改为 `std::string_view`
- 移除函数内部的手动小写转换循环
- 来自 `lowerPathPool_` 的输入已经是小写，原先的转换纯属浪费

### 优化 4：用 `string_view` 替代 `std::string`
- `queryStructuredNameAnchor()` 和 `queryStructuredPathAnchor()` 现在直接将 `lowerPathPool_.view()` 传给 `pathSegmentsMatch()`，不再构造临时 `std::string`
- 消除每条候选记录的堆分配

## 修改文件

- `MacEverything/Core/SearchEngine.h` — 更新 `pathSegmentsMatch` 声明以接受 `string_view`
- `MacEverything/Core/SearchEngineStructuredQuery.cpp` — 全部 4 项优化应用于 `queryStructured()`、`queryStructuredNameAnchor()`、`queryStructuredPathAnchor()` 和 `pathSegmentsMatch()`
- `tests/test_structured_query.h` — 新增测试 26-28，覆盖并行扫描、pathMatchCache 和小写路径匹配

## 测试

- 测试 26：并行线性扫描一致性 — 验证 `dispatch_apply` 产生正确结果
- 测试 27：pathMatchCache 去重 — 50 个文件共享同一目录，正确过滤
- 测试 28：已为小写的路径匹配 — 验证无需冗余小写转换
- 全部 11419 个测试通过

## 预期性能提升

这些优化针对线性扫描回退路径，该路径在没有任何段具有良好 trigram 选择性时激活。综合效果：
- 多线程：在 N 核机器上约 Nx 加速
- pathMatchCache：每条记录的开销从 O(path_components) 降为 O(1)
- 消除小写转换 + string_view：移除每条记录的堆分配和冗余计算
