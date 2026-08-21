# 055 - 修复 Content Compaction 全量无效问题

## 背景

运行日志分析发现：Content Index 的自动压缩（compaction）在 7+ 小时运行期间执行了 26 次，但每次都写出相同的 files=209，从未触发 skip 逻辑。这意味着大量无效的 I/O：每次 compaction 都重写完整的基础索引文件，实际上没有任何内容变化。

## 根因分析

**根因在 `MacSearchBridge+Content.mm` 的 `updateContentIndexForPath` 方法的 remove 分支。**

文件系统删除事件到达时（约 450M 个文件中的任意文件），代码无条件调用 `walAppendRemove(fileIndex)`，即使该文件从未被内容索引过（只有 209 个文件实际被索引）。这导致：

1. WAL 的 `dirty_` 标志被持续污染
2. `compact()` 的 `isDirty()` 检查永远为 true
3. entryCount 持续增长，始终超过 `kCompactThreshold`
4. 每个 compaction 周期都执行完整的基础索引重写，产生大量无效 I/O

对比：add 分支正确地使用 `if (didIndex && contentPersistence)` 进行了守卫。

## 修复内容

### 1. [P0] 修复 Content Compaction 无效（根因修复）

**文件**: `MacSearchBridge+Content.mm`

在 remove 分支添加 `isFileIndexed()` 前置检查：

```diff
- if (fileIndex != UINT32_MAX) {
+ if (fileIndex != UINT32_MAX && contentIndex->isFileIndexed(fileIndex)) {
```

`isFileIndexed()` 已存在于 `ContentIndex.h:47`，无需新增方法。修复后，只有真正在内容索引中的文件删除才会写入 WAL，使 dirty/threshold 检测恢复正常工作。

### 2. [P1] 增加 compact 日志诊断信息

**文件**: `ContentIndexPersistence.cpp`

compact 成功日志从仅输出 `files=N` 扩展为同时输出 WAL 的 entryCount 和字节数：

```
Compacted content index, files=209, walEntries=3, walBytes=256
```

便于后续日志分析快速判断 compaction 的实际工作量。

### 3. [P2] 增加退出日志

**文件**: `MacSearchBridge.mm`

在 `prepareForTermination` 完成后、Logger shutdown 前，增加：

```
=== Log session ended ===
```

与已有的 `=== Log session started ===` 配对，方便日志分析工具精确分割会话。

### 4. 新增测试

**文件**: `tests/test_content_compaction_guard.h` (Part 36)

9 个测试用例覆盖：
- 非索引文件的 remove 不污染 WAL dirty 标志
- 已索引文件的 remove 正确写入 WAL
- ContentIndexPersistence compact 在无实际变更时正确跳过

## 验证

- `test_all --fast`: 10683 tests passed, 0 failed
- `xcodebuild Release`: BUILD SUCCEEDED
