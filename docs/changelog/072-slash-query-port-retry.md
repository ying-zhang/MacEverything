# 072 — 含 `/` 查询 Trigram 加速 + HTTP 端口绑定重试

## 概述

两项优化：
1. **优化 3**：含 `/` 的查询（如 `"tests/test_query_perf"`）从 O(N) 线性扫描优化为 trigram 拆分查找，查询时间从 276-414ms 降至 ~28ms
2. **优化 4**：HttpServer 端口绑定失败时自动重试，避免前一实例未完全退出时新实例 HTTP 服务不可用

## 动机

### 优化 3：含 `/` 查询

`pathTrigramIndex_` 只索引目录路径，`nameTrigramIndex_` 只索引文件名。当关键词含 `/` 时（如 `"tests/test_query_perf"`），跨路径/文件名边界的 trigram（如 `"ts/"`, `"s/t"`, `"/te"`）在两个索引中都不存在，导致代码排除这类查询使用 trigram 索引，回退到 O(N) 线性扫描。

### 优化 4：端口绑定

`HttpServer::start()` 在 `bind()` 失败时只 log 并返回，不重试。前一实例 shutdown 过程中的 compact 操作可能耗时数秒，新实例的 bind 失败后 HTTP 服务不可用。

## 实现

### 优化 3：Slash-Split 策略

将含 `/` 的关键词按最后一个 `/` 拆分为 `pathPart` 和 `namePart`：

1. `pathPart >= 3 chars` → 用 `pathTrigramIndex_` 查找候选 pathIdx
2. `namePart >= 3 chars` → 用 `nameTrigramIndex_` 查找候选 record indices
3. 两者都有候选 → 将 pathIdx 展开为 records，与 name 候选取交集
4. 仅有一个索引可用 → 展开候选，逐一验证另一半
5. 两者都 < 3 chars → 回退线性扫描
6. 对所有候选做 fullPath.find(lowerKey) substring 验证

**边界处理**：以 `/` 开头的绝对路径查询（如 `/etc`、`/usr/local`）不走 slash-split，保留原有线性扫描行为。

### 优化 4：Bind 重试

- `HttpServer::start()` 返回 `bool`
- `bind()` 失败且 `errno == EADDRINUSE` 时重试最多 5 次，间隔 1 秒
- `ServiceEngine::startHttpServer()` 检查返回值，失败时 LOG_ERROR

## 性能验证

| 查询 | 优化前 | 优化后 |
|------|--------|--------|
| `tests/test_query_perf` | 276-414ms | ~28ms |
| `/usr/local` (绝对路径) | ~845ms | ~845ms (不变) |
| `homebrew` (无 `/`) | ~29ms | ~29ms (不变) |

## 测试

新增 `tests/test_slash_query.h`（Part 48），6 个测试、12 个断言：

1. 基本 slash 查询（`"dir/file"`）
2. 深层路径 slash 查询（`"local/bin"`, `"usr/local"`）
3. 长文件名 slash 查询（原始性能问题场景）
4. 无匹配结果
5. 短部分回退线性扫描（`"a/b"`）
6. Phase 1 交互无重复计数

全部 10,834 项 `--fast` 测试通过，0 失败。

## 变更文件

| 类型 | 文件 |
|------|------|
| 修改 | `MacEverything/Core/SearchEngine.cpp` |
| 修改 | `MacEverything/Core/HttpServer.h` |
| 修改 | `MacEverything/Core/HttpServer.cpp` |
| 修改 | `MacEverything/Core/ServiceEngine.cpp` |
| 新增 | `tests/test_slash_query.h` |
| 修改 | `test_all.cpp` |
