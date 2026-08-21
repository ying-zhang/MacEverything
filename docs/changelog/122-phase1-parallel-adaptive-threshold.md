# 122 — Phase 1 多线程化 + 预取 + 自适应阈值

## 概述

对 trigram 候选验证（Phase 1）进行三项性能优化，使高候选数查询（如 "test"）从 linear scan 回退路径切换到 trigram 路径，查询耗时从 ~111ms 降至 ~30ms。

## 背景

Phase 1 验证是对 trigram 索引返回的候选列表逐一检查 name 是否包含查询关键字的过程。原实现存在两个问题：

1. **单线程**：候选数多时（如 "test" 有 91K 候选）验证耗时长
2. **阈值过保守**：`totalSize / 67`（~1.5%）基于旧测量数据（6.1μs/candidate），实际热缓存仅 ~0.26μs/candidate，导致高候选数查询不必要地回退到 linear scan

## 方案 1：Phase 1 多线程化

### 修改文件
- `MacEverything/Core/SearchEngineAdvancedQuery.cpp`

### 实现
将 Phase 1 单线程 for 循环改为 `dispatch_apply` 并行（GCD），复用已有的 `queryLinearScan` 并行模式：

- 并行阈值 `PARALLEL_THRESHOLD = 10000`（低于此值单线程更快，dispatch 开销不值得）
- 线程局部 `threadResults` 避免锁竞争
- 每 1024 个候选检查一次 `queryGeneration_` 支持取消
- `namePriority` 是 static 方法，线程安全
- 所有读操作在 `shared_lock` 保护下

## 方案 2：Software Prefetch

### 修改文件
- `MacEverything/Core/SearchEngineAdvancedQuery.cpp`

### 实现
在 Phase 1 内层循环中预取后续候选的 `FileRecord` 和 `StringPool::Entry`：

```cpp
constexpr size_t PREFETCH_DIST = 8;
if (ci + PREFETCH_DIST < end) {
    uint32_t futureIdx = candidatesData[ci + PREFETCH_DIST];
    __builtin_prefetch(&records[futureIdx], 0, 0);
    __builtin_prefetch(nameEntries + futureIdx, 0, 0);
}
```

仅预取第一级（entries/records），不做 dependent prefetch（需先读 entry 才知 buffer offset）。

## 方案 3：自适应阈值

### 修改文件
- `MacEverything/Core/SearchEngineAdvancedQuery.cpp`（2 处）
- `MacEverything/Core/SearchEngineQuery.cpp`（1 处）
- `MacEverything/Core/SearchEngineStructuredQuery.cpp`（1 处）
- `tests/test_trigram_index.h`（Test 11）
- `tests/test_slash_query.h`（Test 13 注释）

### 计算过程

基于实测数据计算 trigram vs linear 的 breakeven point：

| 指标 | 值 |
|------|-----|
| Trigram per-candidate cost (hot cache) | ~0.14 μs |
| Linear scan per-record cost (GCD parallel) | ~0.036 μs |
| Breakeven | candidates × 0.14 = totalSize × 0.036 → candidates ≈ totalSize / 3.9 |
| 保守阈值 | **totalSize / 4**（25%） |

将所有 4 处 `totalSize / 67` 统一改为 `totalSize / 4`。

## 测试

- 11,718 项单元测试全部通过
- Test 11（trigram 阈值测试）：更新 commonCount 从 200 → 3000（30% > 25% 阈值触发降级）
- Test 13（slash query trigram 降级）：更新注释反映新阈值

## 验证结果

5.3M 记录索引上的 HTTP API 测试：

| 查询 | 旧路径 | 新路径 | 旧耗时 | 新耗时 |
|------|--------|--------|--------|--------|
| test | advanced-linear-gcd | **advanced-trigram** | ~111ms | **~30ms** |
| readme | advanced-trigram | advanced-trigram | ~3ms | ~22ms (首次) |
| package.json | advanced-trigram | advanced-trigram | ~5ms | ~32ms (首次) |
| searchengine | advanced-trigram | advanced-trigram | ~3ms | ~3.6ms |

"test" 查询性能提升 **~3.7x**，其他查询无退化。

## Git 历史

```
803b26f perf: Parallelize Phase 1 trigram candidate verification with prefetch
3633591 perf: Raise trigram fallback threshold from totalSize/67 to totalSize/4
```
