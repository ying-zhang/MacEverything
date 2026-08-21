# Round 29 基准测试报告

**时间**: 2026-04-19 18:17  
**会话**: 同一会话，距启动 >2 小时  
**索引记录数**: 4,497,848  
**Flush**: 最近一次 flush 在 18:12:42（benchmark 前 ~5 分钟），VM 缓存已恢复  
**Benchmark 脚本**: `benchmarks/bench_search.py`（48 查询，limit=100，warmup=3，iterations=10）  
**变更**: SoA tombstone check 优化已合入（commit `0f84094`）

---

## 1. 基准测试结果

### 1.1 总览

| 指标 | R29 | R26 | 变化(vs R26) |
|------|-----|-----|-------------|
| 查询数 | 48 | 48 | — |
| limit | 100 | 100 | — |
| 索引记录数 | 4,497,848 | 5,416,664 | **-17%** |
| 全局 avg median | **10.5ms** | **44.3ms** | **-76%** |
| advanced-trigram avg | **1.6ms** | **4.3ms** | **-63%** |
| advanced-linear-gcd avg | **43.6ms** | **196.4ms** | **-78%** |
| trigram 查询数 | 38 | 38 | — |
| linear 查询数 | 10 | 10 | — |

> **注意**: 记录数差异（4.5M vs 5.4M）占 linear scan 延迟差异的一部分。标准化到每百万条记录：R29 linear = 9.7ms/M, R26 linear = 36.3ms/M。即使标准化后，R29 仍快 ~73%，但这也包含 VM 缓存状态等其他因素。

### 1.2 按类别汇总

| 类别 | 查询数 | R29 Avg Median | R26 Avg Median | 变化 | 评价 |
|------|--------|---------------|---------------|------|------|
| long_trigram | 8 | **0.9ms** | 1.0ms | -10% | 极佳 |
| medium_trigram | 8 | **1.4ms** | 2.4ms | -42% | 极佳 |
| glob_patterns | 7 | **2.3ms** | 5.5ms | -58% | 极佳 |
| path_queries | 8 | **3.5ms** | 12.0ms | -71% | 极佳 |
| cjk_queries | 5 | **8.6ms** | 40.1ms | -79% | 大幅改善 |
| no_results | 3 | **16.9ms** | 93.5ms | -82% | 大幅改善 |
| short_linear | 5 | **37.2ms** | 162.0ms | -77% | 大幅改善 |
| linear_forced | 4 | **40.6ms** | 142.8ms | -72% | 大幅改善 |

### 1.3 按搜索路径分类

| 搜索路径 | 查询数 | R29 Avg Median | R26 Avg Median | 变化 |
|---------|--------|---------------|---------------|------|
| advanced-trigram | 38 | **1.6ms** | **4.3ms** | -63% |
| advanced-linear-gcd | 10 | **43.6ms** | **196.4ms** | -78% |

### 1.4 关键查询对照

| 查询 | R29 Median | R26 Median | 路径 | 变化 |
|------|-----------|-----------|------|------|
| `test` | **3.3ms** | 5.1ms | adv-trigram | -35% |
| `*.py` | **6.1ms** | 14.0ms | adv-trigram | -56% |
| `*test*.cpp` | **1.5ms** | 5.6ms | adv-trigram | -73% |
| `config` | **1.9ms** | 5.0ms | adv-trigram | -62% |
| `application` | **1.6ms** | 2.2ms | adv-trigram | -27% |
| `node_modules` | **0.5ms** | 0.6ms | adv-trigram | -17% |
| `a` | **53.8ms** | — | adv-linear-gcd | — |
| `ab` | **53.0ms** | 250.1ms | adv-linear-gcd | -79% |
| `.h` | **55.4ms** | 278.3ms | adv-linear-gcd | -80% |
| `桌面` | **43.0ms** | 200.2ms | adv-linear-gcd | -79% |
| `qw` | **50.8ms** | 275.0ms | adv-linear-gcd | -82% |

### 1.5 Trigram vs Linear 对照

| 查询 | Trigram | Linear | 加速比 | R26 加速比 |
|------|---------|--------|--------|-----------|
| config | 1.9ms | 37.0ms | **19.6x** | 44.5x |
| readme | 1.1ms | 41.3ms | **39.0x** | 60.6x |
| application | 1.6ms | 43.7ms | **26.8x** | 72.6x |
| node_modules | 0.5ms | 40.5ms | **89.1x** | 239.0x |

> 加速比降低是因为 linear scan 本身变快了（记录数少 + SoA 优化），trigram 路径也变快。

---

## 2. 变更影响分析

### 2.1 SoA Tombstone Check 优化

本轮包含 commit `0f84094`（SoA tombstone check），将 tombstone 判断从 `records[idx].type == 0`（88 字节 AoS 访问）改为 `typesPtr[idx] == 0`（1 字节 SoA 访问）。

**定量影响估算困难**，原因：
1. 记录数差异（4.5M vs R26 的 5.4M）— 这是最大影响因素
2. VM 缓存状态差异（R26 受 flush 前 7s 影响）
3. 本次 benchmark 距 flush 5 分钟，缓存恢复较好

**最佳对照数据**：R28b（优化后，5.4M 记录，预 flush 热缓存）：
- linear-gcd avg: 158.1ms（R26: 196.4ms, **-20%**）
- linear_forced avg: 152.8ms（R26: 142.8ms, **+7%**）

R28b 的 linear-gcd 20% 降低部分归因于 SoA 优化，但同一轮 linear_forced 反而高 7%，说明波动范围较大。**保守估计 SoA 优化对 linear scan 的改善约 5-15%**。

### 2.2 P0/P18 持续验证

| 查询 | R29 Median | 路径 | 评价 |
|------|-----------|------|------|
| `test` | 3.3ms | adv-trigram | **持续有效** |
| `*.py` | 6.1ms | adv-trigram | **持续有效** |
| `*test*.cpp` | 1.5ms | adv-trigram | **持续有效** |

---

## 3. 仍存在的问题

| 问题 | 等级 | R29 表现 | 说明 |
|------|------|---------|------|
| **P22 CJK 短关键词** | Medium | `桌面` 43ms (4.5M) | 走 linear，0 结果 |
| **P24 case-sensitive** | High | 未测试 | 根因未修复 |
| **P23 flush 冲击** | Medium | 本轮未受影响 | 结构性问题 |

---

## 4. 总结

R29 是 SoA tombstone 优化合入后的首个干净基准测试（无 flush 干扰）：

- 全局 avg median **10.5ms**（历史最佳，但记录数仅 4.5M）
- advanced-trigram avg **1.6ms**（极佳）
- advanced-linear-gcd avg **43.6ms**（4.5M 记录下的优秀水平）
- 38/48 查询走 trigram
- P0/P18 修复持续有效
- 无 ERROR/WARN/崩溃
- 无 flush 干扰
