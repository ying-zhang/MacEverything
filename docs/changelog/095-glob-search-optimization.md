# 095 - Glob 搜索优化：预编译模式 + Trigram 预过滤

## 背景

Glob 搜索（如 `*.cpp`）是最慢的查询类型，在 4.8M 记录下耗时约 1001ms。

根因分析：
1. `globMatch()` 是逐字符迭代匹配算法，每次调用约 200ns，而 `simdContains`/`memcmp` 仅 1-5ns
2. Glob 查询完全跳过 trigram 索引，强制走全量 linear scan
3. 每条记录最多调用 2 次 `globMatch`（name + fullpath），4.8M 记录开销巨大

## 优化方案

### 1. 预编译 Glob 模式 (CompiledGlob)

将 glob 模式分类为 5 种类型，使用对应的快速匹配算法：

| 类型 | 模式示例 | 匹配方式 | 性能 |
|------|----------|----------|------|
| SUFFIX | `*.cpp` | `memcmp` 后缀比较 | ~1ns |
| PREFIX | `source_*` | `memcmp` 前缀比较 | ~1ns |
| CONTAINS | `*test*` | `simdContains` NEON | ~3-5ns |
| EXACT | `hello.cpp` | `memcmp` 全匹配 | ~1ns |
| GENERIC | `*?test*` | 原始 `globMatch` 回退 | ~200ns |

### 2. Trigram 预过滤

从 glob 模式的固定文字部分提取 trigram，用现有的 `intersectPostingLists()` 缩小候选集：
- `*.cpp` → 固定部分 `.cpp`（4 字符）→ 提取 trigram → 候选 2,882 条（vs 4.8M 全量）
- 候选数量超过阈值（totalSize/67 ≈ 1.5%）时回退到 linear scan
- 固定部分 < 3 字符时（如 `*.h`）无法提取 trigram，回退到 linear scan

## 变更文件

| 文件 | 变更内容 |
|------|----------|
| `MacEverything/Core/SearchEngine.cpp` | 新增 `CompiledGlob`/`compileGlob()`/`compiledGlobMatch()`；`query()` 增加 glob trigram 预过滤分支；`queryLinearScan()` 改用 `compiledGlobMatch` |
| `tests/test_trigram_index.h` | 更新 Test 5 glob 测试；新增 Test 12 编译 glob + trigram 预过滤测试（含 benchmark） |

## 测试结果

### 单元测试
- 11044 tests passed, 0 failed

### Benchmark (10K 记录)
- `*.cpp` trigram glob: 0.001ms/query vs linear glob: 0.08ms/query → **67x 加速**

### 生产验证 (4.8M 记录)

| 查询 | 优化前 | 优化后 | 路径 | 加速比 |
|------|--------|--------|------|--------|
| `*.cpp` | ~1001ms | 14ms | glob-trigram | ~70x |
| `*.hpp` | ~1001ms | 1.7ms | glob-trigram | ~590x |
| `hello.*` | ~1001ms | 1.3ms | glob-trigram | ~770x |
| `*.h` | ~1001ms | 226ms | linear (compiledGlobMatch) | ~4.4x |
| `*test*` | ~1001ms | 314ms | linear (simdContains) | ~3.2x |

固定部分 >= 3 字符的 glob 模式受益最大，直接走 trigram 预过滤。
固定部分 < 3 字符的模式仍走 linear scan，但使用 `compiledGlobMatch` 替代 `globMatch` 也有显著提升。
