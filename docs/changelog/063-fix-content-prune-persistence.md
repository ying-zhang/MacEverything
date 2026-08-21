# 063 - 修复内容索引 prune 后状态未持久化

## 问题

每次应用启动时，内容索引从 base 文件加载 209 条记录，其中 70 条的 fileIndex 指向非普通文件记录（目录、符号链接、tombstone），需要每次启动都执行 prune。

根因：`setupContentPersistence()` 中 prune 完成后调用 `compact(true)` 写回 base 文件，但此时 WAL 尚未 attach，`compact()` 内部判断 `!wal_` 直接 skip 返回。pruned 后的状态从未写入磁盘。

## 修复

将 `newContentPersistence->compact(true)` 替换为 `contentIndex->saveToFile(basePath)`，直接将内存中已 prune 的内容索引序列化到 base 文件，绕过 WAL 依赖。

## 涉及文件

| 文件 | 变更 |
|------|------|
| `MacEverything/Core/ServiceEngine+Content.cpp` | `compact(true)` -> `saveToFile(basePath)` |

## 验证结果

| 启动次序 | 加载文件数 | Prune | Content indexing |
|----------|-----------|-------|------------------|
| 修复前（每次） | 209 | Pruned 70 | 139 files, 139 skipped, ~1.6s |
| 修复后第1次 | 209 | Pruned 70 + saveToFile | 139 files, 139 skipped |
| 修复后第2次 | **139** | **无 prune** | 139 files, 139 skipped, ~1.55s |

## 关联

- 062-content-modtime-skip: modTime 跳过优化（前置依赖）
- 061-startup-optimization: 两阶段启动优化
- ContentIndexPersistence::compact() 的 skip 逻辑是正确的安全保护，不应修改；调用方在 WAL attach 前需使用 saveToFile() 替代
