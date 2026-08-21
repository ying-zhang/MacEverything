# 091 - lowerPathPool_ 预计算小写路径 + 去重路径匹配

## 背景

对 4.7M 记录进行 2 字符查询（如 `"py"`）需要线性扫描，耗时 ~164ms。瓶颈在于：对每条记录都需要从 `pathPool_` 读取原始大小写路径，通过 `simdToLowerAscii` 转小写后再 `simdContains` 搜索。而 `pathPool_` 是去重的（~100K 条 vs 4.7M 记录），相同路径被重复转小写了数十次。

## 方案

### 1. 新增 `lowerPathPool_` 成员
- `StringPool lowerPathPool_` 与 `pathPool_` 逐条目并行，存储预计算好的小写路径
- 所有搜索路径直接从 `lowerPathPool_` 读取，消除运行时 `simdToLowerAscii` 调用

### 2. 引入 `internPath()` 私有方法
- 统一 6 处重复的 path intern 代码（loadRecords, addRecord, updateByPath, batchRescanPrefix, replayWALEntries x2）
- 原子地向 `pathPool_` 和 `lowerPathPool_` 双写，通过 `pathLookup_` 去重

### 3. 去重路径预匹配
- 线性扫描前预扫描 ~100K 唯一小写路径，结果存入 `vector<bool> pathMatchCache`
- 4.7M 次循环中 O(1) 位查表替代每条记录的 SIMD 路径搜索
- 特殊处理 slash 边界：当关键词含 `/` 时，即使 pathMatchCache 为 false 也需构建全路径验证

## 修改文件

| 文件 | 变更 |
|------|------|
| `MacEverything/Core/SearchEngine.h` | 添加 `lowerPathPool_` 成员、`internPath()` 声明、更新 `buildPathTrigramIndexFromData` 参数名 |
| `MacEverything/Core/SearchEngine.cpp` | internPath() 实现；6 处 mutation site 替换；7 处搜索 site 替换 pathPool_→lowerPathPool_；trigram 构建去掉 toLower；compactRecords COW 并行构建；去重路径预匹配 |
| `test_all.cpp` | 注册 Part 52 测试 |
| `tests/test_path_search.h` | 新增 22 个测试：大小写路径搜索、addRecord/updateByPath 后搜索、compaction 后搜索、slash 边界去重、原始大小写保留 |

## 性能对比（4.79M 记录，warm median）

| 查询类型 | 查询 | 优化前 | 优化后 | 加速比 |
|---------|------|--------|--------|--------|
| 2字符线性扫描 | `py` | 65.0ms | 36.5ms | **1.8x** |
| 2字符线性扫描 | `de` | 300.6ms | 45.0ms | **6.7x** |
| 2字符线性扫描 | `ab` | 473.8ms | 37.9ms | **12.5x** |
| 2字符线性扫描 | `js` | 216.1ms | 34.0ms | **6.4x** |
| 2字符线性扫描 | `go` | 269.1ms | 33.5ms | **8.0x** |
| 2字符线性扫描 | `md` | 240.4ms | 31.0ms | **7.8x** |
| 路径查询 | `/users` | 654.7ms | 154.1ms | **4.2x** |
| 路径查询 | `/usr/local` | 38.2ms | 11.3ms | **3.4x** |
| Trigram | `config.json` | 1.1ms | 1.4ms | 0.8x |
| Trigram | `readme` | 2.4ms | 2.0ms | 1.2x |

**2字符线性扫描平均：260.8ms → 36.3ms（7.2x 加速）**

> 说明：Trigram 查询几乎无变化（~1ms 级别），因为其主要开销在索引查找而非路径匹配。`py` 加速比偏低是因为其路径匹配命中率本身较低，去重预匹配节省的重复计算较少。

## 验证结果

- 全部 10,969 个 fast 测试通过
- Release 构建成功
- HTTP 验证：`q=py` 查询正常返回结果，`q=/users` 路径搜索正确
- `q=config.json` trigram 查询无退化
