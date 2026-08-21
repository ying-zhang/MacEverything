# 094 — HTTP 搜索性能基准测试工具

## 变更概述

新增一套基于 Python 的 HTTP 搜索性能基准测试工具，可对运行中的 MacEverything 实例进行自动化性能评测。

## 规划

目标：构建一套可重复、可对比的搜索性能测试方案，覆盖不同类型的搜索查询，通过多次迭代和统计分析消除机器负载波动对结果的影响。

### 设计决策

- **选择 HTTP API 测试**而非 C++ 单元测试：测试端到端延迟，包含网络开销，更贴近用户真实体验
- **JSON 测试数据与脚本分离**：方便后续扩展查询集而无需修改脚本
- **多次迭代 + warmup**：默认 3 次 warmup + 10 次计时迭代，消除冷启动和缓存效应
- **统计指标丰富**：mean、median、min、max、P95、P99、stddev、CV（变异系数）

## 实施内容

### 新增文件

| 文件 | 说明 |
|------|------|
| `benchmarks/test_queries.json` | 44 条测试查询，覆盖 8 个类别 |
| `benchmarks/bench_search.py` | 基准测试脚本，支持多种输出格式 |

### 测试查询类别（8 类，44 条查询）

| 类别 | 查询数 | 说明 |
|------|--------|------|
| `short_linear` | 5 | 1-2 字符查询，强制线性扫描 |
| `medium_trigram` | 8 | 3-8 字符查询，trigram 加速 |
| `long_trigram` | 8 | 9+ 字符查询，高选择性 trigram |
| `glob_patterns` | 6 | glob 通配模式（`*.swift`, `test_*` 等）|
| `path_queries` | 5 | 斜杠分隔路径查询（`usr/local` 等）|
| `cjk_queries` | 5 | 中文/CJK 字符查询 |
| `no_results` | 3 | 预期无匹配结果的查询 |
| `linear_forced` | 4 | trigram 可用但强制线性扫描（用于对比）|

### 脚本功能

- `--iterations N`：每个查询的计时迭代次数（默认 10）
- `--warmup N`：预热轮数（默认 3）
- `--limit N`：每查询最大结果数（默认 100）
- `--category CAT`：仅运行指定类别（可重复）
- `--output FILE`：JSON 格式报告输出
- `--csv FILE`：CSV 格式报告输出
- `--quiet`：仅输出汇总

### 输出内容

1. **逐查询详情表**：每个查询的 label、搜索策略、结果数、mean/median/min/max/P95/stddev/CV
2. **分类汇总**：每个类别的 median 范围和均值
3. **总体统计**：全局最快/最慢/平均
4. **策略对比**：按搜索策略（trigram vs linear）分组统计
5. **Trigram vs Linear 对比**：同一查询在两种策略下的加速比

## 基准测试结果（4,444,888 条记录）

| 策略 | 平均 Median | 范围 |
|------|------------|------|
| trigram | 1.0ms | 0.0ms - 5.1ms |
| linear | 272.8ms | 59.2ms - 717.7ms |

### Trigram 加速比

| 查询 | Trigram | Linear | 加速比 |
|------|---------|--------|--------|
| node_modules | 0.4ms | 118.6ms | 266.5x |
| application | 1.9ms | 119.8ms | 64.2x |
| readme | 1.9ms | 116.4ms | 62.2x |
| config | 5.1ms | 113.0ms | 22.2x |

## 使用方法

```bash
# 确保 MacEverything 正在运行
# 基本用法
python3 benchmarks/bench_search.py

# 更多迭代、输出报告
python3 benchmarks/bench_search.py --iterations 30 --output report.json --csv report.csv

# 仅测试特定类别
python3 benchmarks/bench_search.py --category medium_trigram --category long_trigram
```
