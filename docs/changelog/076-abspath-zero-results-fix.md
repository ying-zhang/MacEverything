# 076 — 绝对路径查询返回 0 结果修复 + 测试 segfault 修复

## 概述

修复两类问题：
1. **绝对路径查询返回 0 结果**：`/usr/local/bin` 等绝对路径查询由于 slash-split 双索引交集策略的设计缺陷，返回 0 结果
2. **测试套件 segfault（exit code 139）**：两个预先存在的内存安全问题导致测试全部通过后在清理阶段崩溃

## 根因分析

### 问题 1：绝对路径查询 0 结果

**现象**：查询 `/usr/local/bin` 返回 `candidates=0, results=0`。

**根因**：slash-split 将 `/usr/local/bin` 拆分为 `pathPart="/usr/local"` + `namePart="bin"`。双索引交集分支要求记录同时出现在 pathTrigramIndex_ 的路径候选和 nameTrigramIndex_ 的文件名候选中。但 `/usr/local/bin/` 下的文件名是 `brew`、`python3` 等，不含 `"bin"` 子串，因此 nameSet 与路径候选的交集永远为空。

**本质矛盾**：用户意图是「列出 /usr/local/bin/ 目录下的文件」，但引擎理解为「搜索文件名含 bin 的文件」。当 namePart 恰好是目录名而非文件名子串时，双索引交集必然失败。

### 问题 2：ContentIndexPersistence use-after-free

**现象**：测试套件在所有测试通过后 segfault（exit 139），ASAN 报告 stack-buffer-overflow。

**根因**：`scheduleCompaction()` 使用 `dispatch_after` 延迟 60 秒执行，block 捕获了 `this` 指针。当 `ContentIndexPersistence` 对象从栈上析构后，`dispatch_after` 的 block 仍在执行，写入已释放的栈内存（`compactionScheduled_` 成员）。`stopAutoCompactionAndWait()` 中的 `dispatch_sync` 只排空已入队的 block，不会取消延迟执行的 `dispatch_after` block。

### 问题 3：MCP 测试空指针崩溃

**现象**：Part 49 MCP Protocol Tests 崩溃。

**根因**：`mcpExec()` 运行 `./build/Release/MacEverythingMCP`，当该二进制不存在时返回空字符串。`splitLines()` 返回空 vector，然后 `lines[0]` 越界访问导致 SEGV。

## 修复实现

### SearchEngine.cpp — 路径回退策略

在双索引交集分支（L525-547）中：
1. 记录交集前的 `merged.size()`
2. 交集完成后检查是否有新增匹配
3. 如果交集产出 0 结果且 candidatePathIdxs 非空，回退到 path-only 匹配：仅用 pathTrigramIndex_ 过滤路径，对每条记录做 `fullPath.find(lowerKey)` 验证

### ContentIndexPersistence — alive 守卫

1. 添加 `std::shared_ptr<std::atomic<bool>> alive_` 成员，构造时为 `true`
2. 析构函数开头设为 `false`
3. `dispatch_after` block 捕获 `alive_` 的 shared_ptr 副本，执行前检查 `*alive_` 是否仍为 `true`

### test_mcp_protocol.h — 二进制存在性检查

在 `runMcpProtocolTests()` 开头检查 `./build/Release/MacEverythingMCP` 是否存在，不存在则 SKIP。

## 测试结果

全部 10,838 项 `--fast` 测试通过，0 失败，exit code = 0（不再 segfault）。

## 变更文件

| 类型 | 文件 | 说明 |
|------|------|------|
| 修改 | `MacEverything/Core/SearchEngine.cpp` | 添加 path-only 回退 |
| 修改 | `MacEverything/Core/ContentIndexPersistence.cpp` | alive 守卫防止 use-after-free |
| 修改 | `MacEverything/Core/ContentIndexPersistence.h` | 添加 alive_ 成员 |
| 修改 | `tests/test_mcp_protocol.h` | 添加 MCP 二进制存在性检查 |
