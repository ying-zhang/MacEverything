# 010 - 搜索延迟：recentIndices 增量缓存

- **类型**: bugfix
- **严重级别**: P1-HIGH
- **日期**: 2026-04-14
- **Commit**: `a5f6f1c`

## 问题描述

用户清空搜索框后重新输入时感知到明显延迟。根因：`recentIndices()` 每次调用执行 O(N) 全量扫描 + `partial_sort`，持有 `shared_lock` 阻塞 `query()`。当用户清空搜索文本时，`loadRecentFiles()` 调用 `recentIndices(100)` 与新的 `query()` 竞争 CPU 和锁时间。

## 修复方案

1. **增量缓存**：维护 `std::set<RecentEntry>` 缓存（按 modTime 排序，保留 top 200），在所有 6 个数据变更点增量更新：
   - `loadRecords`
   - `addRecord`
   - `removeByPath`
   - `updateByPath`
   - `removeByPathPrefix`
   - `compactRecords`
2. **recentIndices() 优化**：直接返回缓存结果，时间复杂度从 O(N) 降至 O(count)
3. **搜索去抖优化**：debounce 时间从 150ms 缩短至 80ms，提升响应速度

## 影响的文件

- `MacEverything/Core/SearchEngine.cpp` / `.h`

## 测试覆盖

新增 recentIndices 缓存一致性测试。
