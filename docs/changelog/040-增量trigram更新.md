# 040 - 增量 Trigram 更新（batchRescanPrefix）

- **类型**: performance
- **日期**: 2026-04-15
- **Commit**: `93e64c0`

## 问题描述

`batchRescanPrefix` 在每次批量重扫时调用 `buildTrigramIndex()` 进行 O(N) 全量重建，期间持有 `unique_lock` 长达数秒，阻塞所有并发查询请求。随着索引规模增长，锁持有时间线性增加，严重影响搜索响应性。

## 修复/实现方案

将全量重建改为增量更新：对每条受影响的记录调用 `removeTrigramsForRecord` / `addTrigramsForRecord`，与 `addRecord` / `removeByPath` 已有模式保持一致。

- 移除 `batchRescanPrefix` 中的 `buildTrigramIndex()` 调用
- 逐记录增量维护 trigram 索引，避免全量重建
- 锁粒度从全量重建缩小到单记录级别操作

## 影响的文件

- `MacEverything/Core/SearchEngine.cpp` — batchRescanPrefix 增量更新逻辑

## 测试覆盖

- 已有 trigram 索引一致性测试验证增量更新后索引与全量重建结果一致
