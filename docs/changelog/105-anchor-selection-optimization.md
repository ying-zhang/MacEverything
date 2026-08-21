# 105 — Anchor-Selection Optimization for Structured Queries

## 背景

Feature 104 的性能基准测试发现，包含短/常见 namePattern 的 SEGMENTS 查询比 PLAIN 模式慢 3-4 倍：

| 查询 | 优化前 (ms) | 原因 |
|---|---|---|
| `/usr/local/bin` | 624 | "bin" trigram 候选太多 → 线性扫描 |
| `bin/ls` | 457 | "ls" 只有 2 字符 → 无法用 trigram |
| `usr/bin` | 659 | "bin" 太常见 |

**根因**：`queryStructured()` 总是锚定 namePattern（最后一段）。当其 trigram 候选数超过 `totalSize/67`（~67K），回退到全量线性扫描 4.5M 条记录。

## 方案

### 核心思路：多段估价 + 最优锚点选择

不再只对 namePattern 做 trigram 估价，而是对所有段（pathSegments + namePattern）估价，选最便宜的段作为锚点：

1. **`estimateTrigramCost(text)`**：提取 trigrams，返回最小 posting list 大小作为候选数上界
2. 对所有段估价，选 cost 最小的段作为锚点
3. **锚点是 namePattern**（Case A）→ trigram 交集 + pathSegmentsMatch 验证（与原逻辑一致）
4. **锚点是 pathSegment[i]**（Case B）→ trigram 交集找到匹配该段的目录记录，然后通过 `lowerPathLookup_` + `pathIdxToRecords_` 做树形遍历到达 namePattern

### 关键修复

路径段 `estimateTrigramCost` 返回 0 时，含义是"该段文本不作为文件名出现在索引中"，而非"没有匹配结果"。只有 `nameCost == 0`（namePattern 的 trigram 不存在）才能确定零结果。修复了错误的 `bestCost == 0` 提前返回逻辑。

## 实现

### 新增文件
- **`MacEverything/Core/SearchEngineStructuredQuery.cpp`** (307 行)
  - 从 SearchEngineQuery.cpp 中提取（原文件已超 1000 行限制）
  - `pathSegmentsMatch()` — 右到左路径段匹配
  - `estimateTrigramCost()` — trigram 候选数上界估计
  - `treeWalkDown()` — 从锚点目录递归向下遍历到 namePattern
  - `queryStructured()` — 主调度：估价 → 选锚 → 分派
  - `queryStructuredNameAnchor()` — namePattern 锚点路径
  - `queryStructuredPathAnchor()` — pathSegment 锚点 + 树遍历

### 修改文件
- **`SearchEngine.h`** — 新增 `lowerPathLookup_` 成员和辅助方法声明
- **`SearchEngine.cpp`** — 在 `internPath()` 和 `compactRecords()` 中维护 `lowerPathLookup_`
- **`SearchEngineQuery.cpp`** — 移除已提取的函数（911 行 → 合规）
- **`project.pbxproj`** — 新增文件引用

### 新增测试 (tests/test_structured_query.h)
- Test 20: 路径段 trigram 候选比 namePattern 更少时使用路径锚点
- Test 21: 3 段相邻路径的树遍历
- Test 22: 非相邻段（带 *）正确回退
- Test 23: DIR_EXACT 模式 + 路径锚点
- Test 24: 短段 (< 3 字符) 正确回退到线性扫描
- Test 25: 锚点优化与线性扫描结果一致性

## 性能结果

4,458,130 条记录，20 次迭代取平均：

| 查询 | 优化前 (ms) | 优化后 (ms) | 加速比 |
|---|---|---|---|
| `/usr/local/bin` | 624 | **0.41** | 1522x |
| `/usr/bin/python` | ~600 | **0.20** | ~3000x |
| `/Users/username/data` | ~600 | **0.35** | ~1700x |
| `Core/SearchEngine` | ~400 | **3.45** | ~116x |
| `/usr/bin/` (DIR_EXACT) | N/A | **0.05** | — |

仍需线性扫描的查询（无法优化）：
- `bin/ls` (374ms) — "ls" 仅 2 字符，无法产生 trigram
- `/usr/*/bin` (667ms) — `*` 导致非相邻，无法做树遍历

## 验收

- [x] 全量快速测试通过 (11406 tests)
- [x] 应用构建成功 (BUILD SUCCEEDED)
- [x] DMG 打包成功
- [x] HTTP 功能验证通过
- [x] 性能基准测试确认改善
- [x] 代码已合并到 master
