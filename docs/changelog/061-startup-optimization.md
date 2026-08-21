# 061 — 启动优化：增量内容索引 + 启动完成日志 + HttpServer 延迟启动

## 背景

日志分析发现三个启动阶段效率问题：

1. **P3 — 内容索引每次全量读文件**：`ContentIndex::indexFile` 每次启动对所有文件无条件读取磁盘、计算 hash、提取 trigram，即使文件未修改。本可通过 modTime 检查跳过未变文件。
2. **P4 — 缺少启动完成标记**：无法从日志判断启动何时完全就绪，难以度量端到端启动时间。
3. **HttpServer 提前启动**：`AppDelegate.swift` 在 `applicationDidFinishLaunching` 中立即启动 HttpServer，此时 engine/contentIndex 为空。HttpServer 捕获的 shared_ptr 副本在后续 `setEngine:` 后变成 stale。

## 根因分析

### P3: 无条件磁盘 I/O

`indexFile` 执行路径：读文件 → hash → 提取 trigram → 检查 hash 是否变化。每个文件都需要 disk I/O，即使 99% 文件未修改。`FileRecord.modTime` 已在 SearchEngine 中可用但未被利用。

### P4: 缺少可观测性

无 "Startup complete" 日志，无法判断启动总耗时。

### HttpServer: shared_ptr 捕获时机错误

`HttpServer::start()` 内部复制 `shared_ptr<SearchEngine>` 和 `shared_ptr<ContentIndex>`。在 `applicationDidFinishLaunching` 时调用，捕获的是初始空指针。后续 `setEngine:` 替换了 bridge 的 `_engine`，但 HttpServer 持有的仍是旧副本。

## 实施

### Fix 1: 增量内容索引 (modTime 提前检查)

- `ContentFileInfo` 增加 `time_t lastModTime` 字段
- `indexFile` 增加 `time_t modTime = 0` 参数，在读文件之前检查 modTime 是否与已存储值相同，相同则跳过 (零 I/O)
- `modTime=0` 不触发跳过 (向后兼容 v1 持久化数据)
- `insertFileInfo` 增加 `lastModTime` 参数 (WAL replay 传递)
- 持久化格式版本从 1 升到 2，`saveToFile/loadFromFile` 增加 lastModTime 序列化
- WAL `appendAdd/readAll` 增加 lastModTime 序列化 (8 字节)
- Bridge 层 `startContentIndexing` 和 `walAppendAdd` 调用传递 modTime

**涉及文件**: `ContentIndex.h`, `ContentIndex.cpp`, `ContentIndexPersistence.h`, `ContentIndexPersistence.cpp`, `MacSearchBridge+Content.mm`

### Fix 2: 启动完成日志

- `MacSearchBridge_Internal.h` 增加 `_appStartTime` 和 `_startupReported` ivar
- `startIncrementalFrom:` 入口记录 `_appStartTime`
- `startContentIndexing` 完成后通过 `compare_exchange_strong` 一次性输出 "Startup complete: Xs" 日志
- `rebuildContentIndex` 触发的重新索引不会重复输出

**涉及文件**: `MacSearchBridge_Internal.h`, `MacSearchBridge.mm`, `MacSearchBridge+Content.mm`

### Fix 3: HttpServer 延迟启动

- 从 `AppDelegate.swift` 删除 `startHttpServer(19860)` 调用
- 在 `MacSearchBridge.mm` 的两个启动分支 (有缓存/无缓存) 中，engine 设置完毕后调用 `startHttpServer:`
- HttpServer 捕获的 shared_ptr 现在指向真实的、已加载的引擎和索引

**涉及文件**: `AppDelegate.swift`, `MacSearchBridge.mm`

## 测试

新增 `tests/test_content_modtime.h` (Part 38)，4 个测试场景 12 个断言：
1. 同一 modTime 不重复索引 (返回 false)
2. modTime 变化触发重新索引 (返回 true)
3. modTime=0 不跳过 (向后兼容)
4. `insertFileInfo` 正确保存 lastModTime

全量 fast 测试 10702 项全部通过。Xcode Release 构建通过。

## 预期效果

- 第二次启动内容索引耗时从 ~2.68s 降至近零 (大部分文件 modTime 未变)
- 日志中可见 "Startup complete: Xs" 标记
- HttpServer 在启动完成后才可用，搜索请求不再返回空结果
