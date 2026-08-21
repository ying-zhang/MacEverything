# 104 - 结构化查询性能报告

## 概述

对以节点为中心的结构化查询系统（Feature 103）在真实 445 万条记录索引上进行的性能基准测试。测试覆盖全部四种查询模式：PLAIN、SEGMENTS、DIR_EXACT、DIR_LIST。

## 测试环境

- **记录数**：4,457,162 存活 / 5,027,055 总计
- **机器**：macOS (Darwin 24.3.0)
- **构建**：Release (-O2), clang++/c++20
- **日期**：2026-04-19

## 结果汇总

### 快速查询（< 5ms）— 表现良好

| 查询 | 模式 | 平均耗时(ms) | 结果数 | 备注 |
|---|---|---|---|---|
| `SearchEngine` | PLAIN/trigram | 0.55 | 100 | trigram 高选择性 |
| `StructuredQueryParser` | PLAIN/trigram | 0.17 | 1 | 稀有关键词，近乎瞬时 |
| `qzxwvuts_nonexist` | PLAIN/trigram | 0.03 | 0 | trigram 提前退出 |
| `/brew` | SEGMENTS | 0.24 | 100 | 4 字符名称，trigram 有效 |
| `local/brew` | SEGMENTS | 0.72 | 100 | 名称 trigram + 路径约束 |
| `/usr/bin/zsh` | SEGMENTS | 0.95 | 0 | 3 字符名称，trigram 有效（高选择性） |
| `Core/SearchEngine` | SEGMENTS | 3.20 | 100 | 长名称，trigram + 路径 |
| `/etc/*` | DIR_LIST | 0.22 | 100 | O(1) 子项查找 |
| `/usr/local/*` | DIR_LIST | 1.69 | 31 | O(1) 查找，子项较少 |
| `/etc/` | DIR_EXACT | 0.21 | 100 | 精确匹配，快速 |
| `/usr/local/` | DIR_EXACT | 1.79 | 8 | 精确匹配 |

### 慢查询（> 100ms）— 已识别性能问题

| 查询 | 模式 | 平均耗时(ms) | 结果数 | 根因 |
|---|---|---|---|---|
| `test` | PLAIN/linear | 119.77 | 100 | trigram 候选过多，回退到线性扫描 |
| `ls` | PLAIN/linear | 152.22 | 100 | 2 字符，需线性扫描 |
| `bin/ls` | SEGMENTS | 457.22 | 100 | 名称 "ls" 仅 2 字符，全量线性扫描 + 逐记录 pathSegmentsMatch |
| `local/python` | SEGMENTS | 413.13 | 100 | 名称 "python" trigram 候选过多，回退到线性扫描 |
| `/usr/local/bin` | SEGMENTS | 623.55 | 100 | 名称 "bin" 虽有 3 字符但过于常见，线性扫描 + 路径检查 |
| `usr/bin` | SEGMENTS | 658.91 | 100 | 同上："bin" 对 trigram 来说过于常见 |
| `/usr/bin/*` | DIR_LIST | 66.26 | 100 | "bin" 是常见目录名，需检查多个匹配目录 |

## 根因分析

### 主要问题：结构化查询中的线性扫描回退

结构化查询路径（`queryStructured()`）在以下情况下回退到线性扫描：
1. `namePattern` 不足 3 个字符（无法使用 trigram 索引）
2. trigram 候选数超过 `totalSize / 67` 阈值（445 万条记录时约 ~67K）

**为什么结构化线性扫描比 PLAIN 线性扫描更慢：**

在 PLAIN 线性扫描中，每条记录的开销主要是 `simdContains(name)`（约数纳秒）。

在结构化线性扫描中，每条记录还额外需要：
- `lowerPathPool_.str(pathIndices_[i])` — 从路径池构造 `std::string`（堆内存分配！）
- `pathSegmentsMatch()` — 从右到左遍历路径分量进行字符串操作

这使得结构化线性扫描比相同数据集上的 PLAIN 线性扫描**慢 3-4 倍**：
- PLAIN `ls`（2 字符）：~150ms
- SEGMENTS `bin/ls`（名称 "ls"）：~457ms

### 次要问题：PLAIN trigram-split 路径中 phase1Ms 为负值

当 PLAIN 查询降级到 `trigram-split`（trigram 交集运行但候选数超阈值）时，`phase1Ms` 报告负值（如 -2.84ms）。Feature 103 的修复仅覆盖了结构化查询，未覆盖此 PLAIN 模式路径。

## 各模式性能特征

### PLAIN 模式
- **trigram（高选择性关键词）**：< 1ms — 优秀
- **线性扫描回退（常见关键词/短查询）**：100-250ms — 可接受的交互延迟
- **无匹配**：< 0.1ms — trigram 提前退出

### SEGMENTS 模式
- **使用 trigram**：< 5ms — 优秀
- **线性扫描回退**：400-700ms — **对交互使用来说过慢**
- **无匹配**：< 0.1ms

### DIR_LIST 模式
- **小目录**：< 2ms — 优秀（O(1) pathIdxToRecords_ 查找）
- **常见目录名（如 "bin"）**：~66ms — 中等，需枚举多个目录
- **无匹配**：< 0.1ms

### DIR_EXACT 模式
- **所有测试场景**：< 2ms — 优秀（精确名称匹配选择性很高）

## 优化方向

1. **结构化查询的路径 trigram 索引**：当名称 trigram 失效时，使用 `pathTrigramIndex_` 在线性扫描前缩小候选范围。对于 `usr/bin`，无需扫描全部 450 万条记录，而是先对路径段（如 "usr"）求 trigram 交集得到更小的候选集。

2. **避免 pathSegmentsMatch 中的字符串分配**：当前 `lowerPathPool_.str()` 会分配新的 `std::string`。可改用基于 `std::string_view` 的方式或直接在池缓冲区上匹配。

3. **达到 maxResults 时提前终止**：找到足够结果（如 100 条）后停止扫描。当前线性扫描即使已找到足够匹配也会继续遍历所有记录。

4. **名称+路径 trigram 联合交集**：对于 `usr/bin` 这类查询，将 "usr" 的路径 trigram 与 "bin" 的名称 trigram 求交集，得到非常小的候选集。

## 结论

以节点为中心的查询系统在 trigram 加速有效时表现优异（SEGMENTS < 5ms，DIR_LIST/DIR_EXACT < 2ms）。主要性能差距在于短或常见名称模式的线性扫描回退，此时结构化查询比 PLAIN 模式慢 3-4 倍，原因是逐记录的路径字符串构造和段匹配开销。这是已知的局限性，也是未来优化的潜在方向。
