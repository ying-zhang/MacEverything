# 062 - 内容索引 modTime 跳过优化

## 问题

每次启动后的内容索引阶段对所有 209 个已索引文件无条件读取磁盘内容（耗时 ~1.6s），即使 99% 的文件自上次索引以来未发生任何修改。

## 根因分析

ContentIndex::indexFile 已有完整的 modTime 早退逻辑（检查 `modTime > 0 && fileInfos_[idx].lastModTime == modTime` 时跳过 I/O），ContentFileInfo 已有 `lastModTime` 字段，WAL 和基础文件序列化也已支持 lastModTime。

但两个调用方从未传递 modTime：
- `startContentIndexing()` — 调用 `indexFile(idx, path)` 使用默认参数 0
- `updateContentForPath()` — 同上

## 修复方案

纯接线修复，无需修改任何数据结构或序列化格式：

### ServiceEngine+Content.cpp

1. **startContentIndexing**：
   - FileEntry 结构增加 `time_t modTime` 字段
   - 从 `FileRecord.modTime` 采集并传递给 `indexFile(idx, path, modTime)`
   - `walAppendAdd` 增加 `info.lastModTime` 参数
   - 新增 `skipped` 原子计数器，日志输出跳过数

2. **updateContentForPath**：
   - 通过 `forEachRecordWithPath` 获取 FileRecord.modTime
   - 传递给 `indexFile(idx, path, modTime)` 和 `walAppendAdd(..., info.lastModTime)`

### ContentIndex.cpp

3. **hash-match 路径更新 lastModTime**：
   - 当文件内容 hash 匹配但 `lastModTime` 不一致（如 v1 持久化数据 `lastModTime=0`），更新 `lastModTime` 并返回 `true` 以触发 WAL 持久化
   - 确保旧数据在首次全量索引后升级到带 modTime 的状态，后续启动可走 modTime 早退路径

4. **readFileIfText 失败路径更新 lastModTime**：
   - 已索引文件变得不可读（被删除、变为二进制、超出大小限制）时，`readFileIfText()` 返回空但 `lastModTime` 永远不被更新，导致每次启动都重试 I/O
   - 现在当 `readFileIfText()` 失败且文件已在索引中、`modTime > 0` 时，更新 `lastModTime` 以确保后续启动走 modTime 早退路径

## 测试

- `test_content_modtime.h`（Part 38）— 20 项测试全部通过（含 Test 5: hash-match 升级 lastModTime, Test 6: 不可读文件升级 lastModTime）
- `--fast` 全量单元测试 — 10727 项全部通过
- xcodebuild Release 构建通过

## 预期效果

- 首次启动：全量索引（行为不变）
- 后续启动：绝大多数文件 modTime 未变，跳过磁盘读取，耗时从 ~1.6s 降至 <0.1s
- 日志示例：`Content indexing completed: 209 files (197 skipped by modTime) in 0.05s`
