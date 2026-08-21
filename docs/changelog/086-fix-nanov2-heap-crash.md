# 086 - Fix nanov2 Heap Corruption Crash in dispatch_apply Search

## 现象

使用 `trigram=0` 参数（强制 NEON 全表扫描）进行搜索时，app 崩溃。
崩溃栈指向 `nanov2_find_zone_and_free` — Apple nanov2 小对象分配器的堆元数据损坏。

## 根因分析

`dispatch_apply` 创建 ~10 个工作线程，每个线程遍历数百万条记录。每次迭代中产生 2-3 个临时 `std::string` 分配：

1. `me::toLower(makeFullPath(rPath, records_[idx].name))` — 创建 CoreFoundation CFString + std::string
2. `records_[idx].name.size()` — 访问 std::string 成员（非必要，namePool_ 已有数据）
3. 路径拼接中的临时 string

~10 线程 x 数百万记录 x 2-3 alloc/dealloc = 数千万次跨线程小对象分配/释放，超过 nanov2 allocator 的 magazine 传输能力，导致堆元数据损坏。

## 修复方案

**核心原则**：消除 dispatch_apply 热循环中的所有 per-iteration 堆分配。

### dispatch_apply 块内（崩溃路径）

| 修改前 | 修改后 | 原因 |
|--------|--------|------|
| `me::toLower(makeFullPath(...))` | 线程局部 `vector<char> pathBuf` + `memcpy` + `simdToLowerAscii` | 避免 CF 和 string 分配 |
| `records_[idx].name.size()` | `namePool_.length(idx)` | 避免触碰 std::string 成员 |
| `string::find` 验证 | `me::simdContains` 直接操作 buffer | 零分配 NEON 搜索 |
| per-iteration `std::string lowerName` | 循环外声明，`.assign()` 复用容量 | 仅 glob 模式需要 |

### 单线程 trigram 路径（一致性修复）

4 处 `me::toLower(makeFullPath(rPath, records_[idx].name))` 替换为 `namePool_` 直接访问 + `simdToLowerAscii`。
6 处 `records_[idx].name.size()` 替换为 `namePool_.length()`。

### 测试文件修复

`tests/test_string_pool.h` 和 `tests/test_simd_search.h` 的 include 路径从 `"tests/test_helpers.h"` 和 `"MacEverything/Core/XXX.h"` 修正为 `"test_helpers.h"`（相对路径），解决 Makefile 编译失败。

## 修改文件

| 文件 | 变更 |
|------|------|
| `MacEverything/Core/SearchEngine.cpp` | 消除 dispatch_apply 内堆分配；修复 trigram 路径一致性 |
| `tests/test_string_pool.h` | 修正 include 路径 |
| `tests/test_simd_search.h` | 修正 include 路径 |

## 验证

1. Release 构建通过
2. 全部快速测试通过（10947 pass）
3. DMG 打包 + app 启动正常
4. `curl "http://localhost:19860/api/search?q=hello&trigram=0"` — 无崩溃，结果正确
5. 连续 10 次 `trigram=0` 请求全部成功
6. `.cpp` 短关键词 + `/usr/local` 路径搜索均正常
