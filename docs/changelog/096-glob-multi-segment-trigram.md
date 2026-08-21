# 096 - 复合 Glob 多段 Trigram 预过滤优化

## 背景

changelog 094 引入了 glob 搜索优化，对 `*.cpp`（SUFFIX）、`test_*`（PREFIX）、`*test*`（CONTAINS）等简单模式实现了 trigram 预过滤。但对于复合模式如 `*test*.cpp`、`*.min.js`、`test_*.cpp`，`compileGlob()` 将其归为 GENERIC 类型，`fixed=""` 为空，无法触发 trigram 预过滤，回退到全量线性扫描（~1s / 486 万记录）。

## 问题根因

`compileGlob()` 的 GENERIC 分支直接返回空 `fixed`，而 trigram 预过滤路径检查 `cg.fixed.size() >= 3` → 条件不满足 → 回退 `queryLinearScan()`。

复合 glob 模式中其实包含多个字面片段（如 `*test*.cpp` 中的 `"test"` 和 `".cpp"`），每个片段都可以提取 trigram 用于缩小候选集。

## 实现方案

### 1. 新增 `extractLiteralSegments()`

在 `*` 和 `?` 位置切割 pattern，提取所有字面片段：
- `*test*.cpp` → `["test", ".cpp"]`
- `*.min.js` → `[".min.js"]`
- `test_*.cpp` → `["test_", ".cpp"]`
- `?ource*.cpp` → `["ource", ".cpp"]`

### 2. 扩展 `CompiledGlob` 结构体

新增 `segments` 字段，存储所有 >= 3 字符的字面片段。GENERIC 分支选最长片段作为 `fixed`（向后兼容），所有合格片段存入 `segments`。

### 3. 新增 `intersectPostingListsMulti()`

接受多个字面片段，从所有片段提取 trigram，去重后统一做 posting list 交集。多段 trigram 合并的优势：`*test*.cpp` 中 `test` 产生 {tes, est}，`.cpp` 产生 {.cp, cpp}，4 个 posting list 交集远小于任何单段。

### 4. 修改 `query()` glob 预过滤路径

当 `cg.segments.size() > 1` 时使用 `intersectPostingListsMulti()`，否则保持原逻辑使用单段 `intersectPostingLists()`。

## 修改文件

| 文件 | 变更 |
|------|------|
| `MacEverything/Core/SearchEngine.cpp` | 新增 `extractLiteralSegments()`；扩展 `CompiledGlob`；修改 `compileGlob()` GENERIC 分支；新增 `intersectPostingListsMulti()`；修改 `query()` glob 预过滤 |
| `MacEverything/Core/SearchEngine.h` | 新增 `intersectPostingListsMulti()` 声明 |
| `tests/test_trigram_index.h` | 新增 Test 13: 多段 glob trigram 预过滤测试 |

## 测试结果

### 单元测试
53/53 全部通过，包括新增的 Test 13（多段 glob + trigram 预过滤）。测试覆盖：
- `*source*.cpp` — 两段合并 trigram
- `*.cpp.*` — 单段 trigram
- `test_*.cpp` — 两段 trigram
- `*.ext.*` — 单段 trigram
- `?ource*.cpp` — `?` 切割后两段
- `*a*b*` — 全部 < 3 字符，正确回退线性扫描
- 正确性验证：trigram 路径 vs 线性扫描结果完全一致

### 生产验证（486 万记录）

| Pattern | Candidates | Query Time |
|---------|-----------|------------|
| `*test*.cpp` | 135 / 4.86M | 87ms |
| `test_*.cpp` | 28 / 4.86M | 5.5ms |
| `*.min.js` | 1,362 / 4.86M | 9.6ms |
| `?ource*.cpp` | 3 / 4.86M | 1.9ms |

优化前这些模式均需 ~1s（全量线性扫描），优化后通过多段 trigram 交集将候选集缩小到极小范围，查询时间降低 10-500 倍。

## 安全性

- **向后兼容**：简单模式（SUFFIX/PREFIX/CONTAINS/EXACT）走原有快速路径，完全不受影响
- **失败安全**：trigram 候选超 1.5% 阈值自动回退线性扫描
- **正确性**：trigram 仅缩小候选集，最终匹配仍用完整 `globMatchImpl()`
