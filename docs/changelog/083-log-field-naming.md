# 083 — 统一日志字段命名 & 增加 tombstone 回收日志

## 问题

日志中 `records=` 字段在不同上下文含义不同，造成分析困难：

- **SearchEngine 查询日志**：`records=` 表示 `recordCount()`（总记录数，含 tombstone）
- **IndexPersistence flush/load 日志**：`records=` 表示 `liveRecordCount()`（仅活跃记录）

例如查询日志显示 `records=4637942`，flush 日志显示 `records=4434302`，差值 ~206K 即为 tombstone 数量，但需要人工推算。

此外，compaction 回收 tombstone 时没有显式日志，难以追踪回收效果。

## 修复方案

### 变更 1：SearchEngine.cpp — 查询日志字段重命名

`records=` → `totalRecords=`

两处日志（trigram 路径和 linear 路径）均已更新。

### 变更 2：IndexPersistence.cpp — 持久化日志字段重命名

`records=` → `liveRecords=`

涉及 5 处日志：
- `Loaded paged index`
- `Loaded legacy index`
- `WAL replay done`
- `Flushed paged index`
- `Full compaction done`

### 变更 3：IndexPersistence.cpp — 增加 tombstone 回收日志

在 `fullCompact()` 中，`compactRecords()` 调用前后记录 totalCount 和 liveCount，输出：

```
Reclaimed N tombstones (beforeTotal -> beforeLive records)
```

## 变更文件

| 文件 | 变更 |
|------|------|
| `MacEverything/Core/SearchEngine.cpp` | `records=` → `totalRecords=`（2 处） |
| `MacEverything/Core/IndexPersistence.cpp` | `records=` → `liveRecords=`（5 处）+ 新增 tombstone 回收日志 |

## 测试结果

- `./test_all --fast`：10867/10867 PASS
- xcodebuild Release 构建通过
