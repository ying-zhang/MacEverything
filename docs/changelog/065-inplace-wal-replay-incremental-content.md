# 065 - In-place WAL Replay + Incremental Content Indexing

## 背景

启动时存在两个性能瓶颈：

1. **WAL 回放 (P0)**：`IndexPersistence::load()` 使用 O(N_total) 的 export→pathMap→remove_if→loadRecords 流水线回放 WAL。对于 4.7M 条记录 + 9K-44K WAL 条目，耗时 10-28 秒。
2. **内容索引全量扫描 (P1)**：`startContentIndexing()` 每次启动都遍历全部记录（4.7M），即使只有 ~139 个文件被内容索引。

## 规划

- P0：新增 `SearchEngine::replayWALEntries()` 方法，利用现有 `pathIndex_` 实现 O(1) 路径查找的原地 WAL 回放
- P1：`ContentIndex::getIndexedFileIndices()` + 两阶段 `startContentIndexing()`，已索引文件存在时仅检查这些文件（增量模式）

## 实施

### P0: SearchEngine::replayWALEntries()

**文件**: `MacEverything/Core/SearchEngine.h`, `MacEverything/Core/SearchEngine.cpp`

- 新增 `replayWALEntries(std::vector<WALEntry>&& entries)` 方法
- 单次 `unique_lock` 下遍历所有 WAL 条目
- Add 操作：检查 `pathIndex_` 是否已存在同路径记录，若存在先 tombstone（保证幂等），然后追加新记录
- Remove 操作：通过 `pathIndex_` O(1) 定位，tombstone 并清理
- Update 操作：tombstone 旧 + 追加新（合并 Remove+Add）
- 每条操作同步维护 pathIndex_、trigram 索引、dirty pages、liveCount
- 最后一次性 `rebuildRecentCache()`

**文件**: `MacEverything/Core/IndexPersistence.cpp`

- 原有 ~57 行 batch replay 代码替换为 5 行调用
- 移除未使用的 includes

### P1: 增量内容索引

**文件**: `MacEverything/Core/ContentIndex.h`, `MacEverything/Core/ContentIndex.cpp`

- 新增 `getIndexedFileIndices()` 方法，线程安全返回已索引文件 ID 列表

**文件**: `MacEverything/Core/ServiceEngine+Content.cpp`

- `startContentIndexing()` 改为两阶段：
  - 增量模式：`getIndexedFileIndices()` 非空时仅检查已索引文件
  - 全量模式：首次运行，无持久化内容索引时全量扫描

## 测试

**新增**: `tests/test_wal_inplace_replay.h` (Part 42)

| 测试 | 内容 |
|------|------|
| T1 | Add 操作插入记录并更新 pathIndex_ |
| T2 | Remove 操作 tombstone 记录 |
| T3 | Update 操作替换旧记录 |
| T4 | 混合操作 (Add + Update + Remove) |
| T5 | 空条目列表为 no-op |
| T6 | Trigram 索引在回放后正确更新 |
| T7 | Remove 不存在路径静默忽略 |

**更新**: `tests/test_content_modtime.h`

| 测试 | 内容 |
|------|------|
| T9 | getIndexedFileIndices 返回正确键集 |
| T10 | 空索引返回空向量 |

全部 10765 测试通过。

## 验证结果

从生产日志对比（4.7M 记录）：

| 指标 | 优化前 | 优化后 | 提升 |
|------|--------|--------|------|
| WAL 回放 (7660 条) | ~11s (batch mode, 同量级) | 0.39s (in-place mode) | **28x** |
| 内容索引 (108 文件) | 1.5-2.0s (全量扫描) | 0.0001s (增量模式) | **>10000x** |

## 修改文件

| 类型 | 文件 |
|------|------|
| 修改 | `MacEverything/Core/SearchEngine.h` |
| 修改 | `MacEverything/Core/SearchEngine.cpp` (+98 行) |
| 修改 | `MacEverything/Core/IndexPersistence.cpp` (-57/+5 行) |
| 修改 | `MacEverything/Core/ContentIndex.h` |
| 修改 | `MacEverything/Core/ContentIndex.cpp` (+10 行) |
| 修改 | `MacEverything/Core/ServiceEngine+Content.cpp` |
| 新增 | `tests/test_wal_inplace_replay.h` |
| 修改 | `tests/test_content_modtime.h` |
| 修改 | `test_all.cpp` |
