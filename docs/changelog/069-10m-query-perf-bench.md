# 069 - 10M-Record Query Performance Benchmark

## 背景

为了数据驱动地优化搜索性能，需要一个大规模基准测试来量化不同查询路径在真实规模下的表现。
现有测试（Part 44）覆盖了中等规模场景，但无法暴露千万级数据量下的性能瓶颈。

## 规划

### 目标
- 构建 1000 万条确定性 FileRecord 数据集（种子 12345，可复现）
- 覆盖 9 类查询场景，全面测试搜索引擎各条路径
- 提供 p50/p95/min/max 统计和表格化输出
- 注册为 `--bench` 标志，与 Part 44 并列

### 数据分布
| 类别 | 比例 | 说明 |
|------|------|------|
| test_* | 2% | 触发 Layer1 bypass |
| *_test_* | 2% | 路径中含 test |
| SearchEngine* | 1% | 中等 trigram 选择性 |
| *.cpp | 5% | glob 模式测试 |
| *.swift | 1% | glob 模式测试 |
| unique_xyz* | 0.5% | 高选择性 trigram |
| EXACT_MATCH* | 0.5% | 精确匹配测试 |
| README.md | 1% | 重名文件测试 |
| index.js | 1% | 重名文件测试 |
| deep paths | 1% | 深层路径匹配 |
| generic | ~85% | 通用文件 |

### 9 个测试场景
1. **Trigram 选择性** — test (Layer1 bypass), SearchEngine, unique_xyz
2. **短关键词（线性扫描）** — 1-2 字符关键词
3. **Glob 模式** — *.cpp, *.swift, test_*, *.nonexistent
4. **无匹配** — 确认无匹配时的扫描代价
5. **仅路径匹配** — homebrew, DerivedData, deep path
6. **重名文件** — README.md, index.js
7. **精确匹配与排序** — EXACT_MATCH
8. **maxResults 缩放** — limit=1 到 unlimited
9. **写入竞争** — 并发写入下的查询性能

## 实施

### 新增文件
- `tests/test_query_perf_10m.h` — 497 行，Part 46 完整实现

### 修改文件
- `test_all.cpp`：
  - 新增 `#include "tests/test_query_perf_10m.h"`
  - 新增 `--bench` CLI 标志（选择 Part 44 + 46）
  - 新增 Part 46 调度入口
  - Part 46 不包含在 `--fast` 和默认全量测试集中（太重，~12s 构建 + ~2min 运行）

### 关键设计决策
- 数据集构建使用 `std::mt19937` 确定性种子，保证可复现
- 每个场景运行 5 次取统计值（avg/min/max/p50/p95）
- 包含 7 个正确性检查确保数据分布符合预期
- "test" 查询匹配包含路径中的 /tests/ 目录，实际匹配约 116 万条（非仅文件名的 40 万）

## 基准测试结果（首次运行）

| 查询 | 路径 | Avg | 结果数 |
|------|------|-----|--------|
| "test" L=100 | Layer1 bypass | 306ms | 100 |
| "SearchEngine" L=100 | Trigram | 6ms | 100 |
| "unique_xyz" L=100 | Trigram | 3ms | 100 |
| "te" L=100 | Linear | 406ms | 100 |
| "*.cpp" L=100 | Glob | 409ms | 100 |
| "README.md" L=100 | Trigram | 5ms | 100 |

### 关键发现
- **maxResults 对线性扫描无效**：limit=1 (314ms) vs unlimited (331ms)，差异 <6%
- **Trigram 路径高效**：3-6ms vs 线性扫描 300-400ms（60-100x 差距）
- **Layer1 bypass 回退到线性扫描**：与直接线性扫描速度一致
- **写入竞争影响**：trigram +7ms，线性扫描 +39ms

## 验收
- [x] 所有 7 个正确性检查通过
- [x] `--fast` 全量测试通过
- [x] 已合并到 master
