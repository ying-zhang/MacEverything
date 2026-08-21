# 084 - Contiguous Memory Layout + NEON SIMD Search

## 概述

将 SearchEngine 的文件名和路径存储从 `vector<string>` 独立堆分配改为连续内存池（StringPool），并引入 ARM NEON SIMD 搜索函数，显著减少 cache miss 和提升搜索吞吐。

## 动机

基准测试显示 NEON first-last byte 2x 展开在连续内存上可达 11.56 GB/s（单线程）/ 74.30 GB/s（多线程），是 `std::string::find` 的 9.5x。但原有 `vector<string>` 每个字符串独立堆分配，指针追踪导致 cache miss，无法接近内存带宽极限。

## 架构设计

### StringPool（新建）

`MacEverything/Core/StringPool.h` — header-only 连续内存字符串池：
- `vector<char> buffer_`：所有字符串首尾相连的连续内存
- `vector<Entry{offset, length}> entries_`：每个字符串的偏移和长度
- 支持 append（O(1) 摊销）、tombstone（length=0）、compact、loadBulk、reserve
- `CompactResult compact(liveMask)` 返回压缩后的新池和 old→new 索引映射

### SIMDSearch（新建）

`MacEverything/Core/SIMDSearch.h` — header-only NEON SIMD 搜索函数：
- `simdFind` — NEON first-last byte 2x 展开，单次搜索
- `simdFindAll` — 连续 buffer 全匹配，返回所有偏移
- `simdContains` — bool 包装
- `simdToLowerAscii` — NEON 向量化 ASCII 转小写
- 非 ARM 平台自动 fallback 到 memmem/标量

### SearchEngine 重构

| 旧 | 新 |
|---|---|
| `vector<string> lowerNames_` | `StringPool namePool_` |
| `PathTable pathTable_` | `StringPool pathPool_` + `unordered_map<string,uint32_t> pathLookup_` |

所有涉及文件名和路径访问的方法均已改写：
- `loadRecords()` — 批量 toLower + `namePool_.loadBulk()`
- `query()` — 新增 `bool useTrigram` 参数；trigram 候选用 `simdContains` 验证
- `addRecord()` — `namePool_.append()` + pathLookup_ 去重
- `removeByPath()` — `namePool_.tombstone()`
- `compactRecords()` — StringPool COW 拷贝 + compact
- `replayWALEntries()` — 三种 WALOp 全部适配
- trigram 方法通过 `namePool_.data/length` 访问

### HTTP API 扩展

`query()` 新增 `useTrigram` 参数，HTTP 接口支持 `&trigram=0` 强制 NEON 全表扫描，便于性能对比。

### StringUtils 优化

`me::toLower()` ASCII 快速路径改用 `simdToLowerAscii`，16 字节向量化处理。

### PathTable 兼容

保留 `PathTable` 类定义供外部使用（PagedIndexWriter）。`pathTableSnapshot()` 从 pathPool_ 重建 PathTable 返回。

## 修改文件

| 文件 | 操作 | 说明 |
|------|------|------|
| `MacEverything/Core/StringPool.h` | 新建 | 连续内存字符串池 |
| `MacEverything/Core/SIMDSearch.h` | 新建 | NEON SIMD 搜索函数 |
| `MacEverything/Core/SearchEngine.h` | 修改 | 成员变量替换、query 签名更新 |
| `MacEverything/Core/SearchEngine.cpp` | 修改 | 50+ 处适配新内存布局 |
| `MacEverything/Core/SearchEnginePersistence.cpp` | 修改 | pathPool_ 读取路径 |
| `MacEverything/Core/StringUtils.cpp` | 修改 | SIMD toLower |
| `MacEverything/Core/HttpServer.cpp` | 修改 | trigram toggle 参数 |
| `tests/test_helpers.h` | 修改 | 新增 CHECK 宏 |
| `tests/test_string_pool.h` | 新建 | StringPool 单元测试（10 组） |
| `tests/test_simd_search.h` | 新建 | SIMDSearch 单元测试（19 组） |
| `test_all.cpp` | 修改 | 引入 Part 50/51 |

## 测试

- Part 50: StringPool — append、tombstone、compact、loadBulk、空字符串、大批量、连续性验证、clear、拷贝/移动语义
- Part 51: SIMDSearch — simdFind 基础/边界/跨 NEON 边界、simdContains、simdFindAll 多匹配/无匹配/空 needle、simdToLowerAscii、随机一致性校验
- 全量 `--fast` 测试：10,943 通过，0 失败

## 预期性能提升

| 场景 | 原因 |
|------|------|
| 线性扫描（短关键词） | 连续内存消除指针追踪 cache miss + NEON 向量化 |
| Trigram 候选验证 | simdContains 替代 string::find |
| 路径搜索 | pathPool 连续 NEON 扫描 |
| toLower | SIMD 16 字节批量处理 |
