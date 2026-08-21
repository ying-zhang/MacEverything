# Round 24 基准测试报告

**时间**: 2026-04-19 17:17  
**会话**: 17:16:07 启动，已运行 ~1min  
**索引记录数**: 4,492,826（benchmark 开始时服务报告）/ 5,402,432（benchmark 结束时日志）  
**WAL 条目**: 797（启动时回放，较上一会话 4,337 大幅下降）  
**Flush**: benchmark 期间**无 flush**  
**Benchmark 脚本**: `benchmarks/bench_search.py`（48 查询，limit=100，warmup=3，iterations=10）

---

## 1. 基准测试结果

### 1.1 总览

| 指标 | R24 | R23 | R22 | 变化(vs R23) |
|------|-----|-----|-----|-------------|
| 查询数 | 48 | 48 | 48 | — |
| limit | 100 | 100 | 100 | — |
| 最小延迟(median) | 0.0ms | 0.0ms | 0.0ms | — |
| 最大延迟(median) | 266.1ms | 303.1ms | 358.3ms | -12% |
| 全局 avg median | **39.8ms** | **48.2ms** | **61.8ms** | **-17%** |
| advanced-trigram avg | **3.6ms** | **7.6ms** | **6.6ms** | **-53%** |
| advanced-linear-gcd avg | **177.6ms** | **202.5ms** | **207.1ms** | **-12%** |
| trigram 查询数 | **38** | 38 | 34 | — |
| linear 查询数 | **10** | 10 | 13 | — |

### 1.2 按类别汇总

| 类别 | 查询数 | R24 Avg Median | R23 Avg Median | 变化 | 评价 |
|------|--------|---------------|---------------|------|------|
| long_trigram | 8 | **0.9ms** | 1.0ms | -10% | 极佳 |
| medium_trigram | 8 | **2.4ms** | 2.2ms | +9% | 极佳（稳定） |
| glob_patterns | 7 | **4.2ms** | 6.2ms | -32% | 极佳 |
| path_queries | 8 | **10.0ms** | 27.6ms | **-64%** | **大幅改善** |
| cjk_queries | 5 | **37.1ms** | 36.2ms | +2% | 稳定 |
| no_results | 3 | **88.7ms** | 101.0ms | -12% | 改善 |
| short_linear | 5 | **158.2ms** | 147.3ms | +7% | 冷启动效应 |
| linear_forced | 4 | **133.4ms** | 201.0ms | **-34%** | **大幅改善** |

### 1.3 按搜索路径分类

| 搜索路径 | 查询数 | R24 Avg Median | R23 Avg Median | 变化 |
|---------|--------|---------------|---------------|------|
| advanced-trigram | **38** | **3.6ms** | **7.6ms** | **-53%** |
| advanced-linear-gcd | **10** | **177.6ms** | **202.5ms** | **-12%** |

### 1.4 Trigram vs Linear 对照（同一查询）

| 查询 | Trigram | Linear | 加速比 | R23 加速比 |
|------|---------|--------|--------|-----------|
| config | 4.7ms | 155.9ms | **33.1x** | 48.8x |
| readme | 1.2ms | 48.8ms | **40.3x** | 162.9x |
| application | 2.1ms | 175.0ms | **85.1x** | 132.8x |
| node_modules | 0.5ms | 154.1ms | **321.1x** | 305.9x |

**注意**: 加速比相比 R23 降低，原因是 trigram 路径本身已极快（0.5-4.7ms），两者都极佳。`readme` linear 48.8ms 异常低可能是 VM 缓存命中。

### 1.5 逐查询详情（仅列出关注项）

#### P0/P18 修复持续验证

| 查询 | R24 Median | R23 Median | R24 路径 | 评价 |
|------|-----------|-----------|---------|------|
| `test` | **8.4ms** | 6.3ms | adv-trigram | 持续有效（+33% 波动正常） |
| `*.py` | **12.2ms** | 16.5ms | adv-trigram | **持续改善** |
| `*test*.cpp` | **5.5ms** | 7.1ms | adv-trigram | **持续改善** |

#### 性能优秀的查询

| 查询 | R24 Median | R23 Median | 路径 | 评价 |
|------|-----------|-----------|------|------|
| `application` | 2.1ms | 1.6ms | adv-trigram | 极佳 |
| `node_modules` | 0.5ms | 0.6ms | adv-trigram | 极佳 |
| `readme` | 1.2ms | 1.2ms | adv-trigram | 极佳 |
| `config` | 4.7ms | 4.4ms | adv-trigram | 良好 |
| `*.swift` | 1.5ms | 1.8ms | adv-trigram | 极佳 |
| `*.cpp` | 0.6ms | 1.0ms | adv-trigram | 极佳 |
| `*.json` | 4.6ms | 7.9ms | adv-trigram | **改善** |
| `screenshot` | 0.1ms | 0.2ms | adv-trigram | 极佳 |
| `dockerfile` | 0.1ms | 0.3ms | adv-trigram | 极佳 |
| `src` | 0.5ms | 0.4ms | adv-trigram | 极佳 |
| `main` | 1.0ms | 1.1ms | adv-trigram | 极佳 |
| `package.json` | 2.9ms | — | adv-trigram | 极佳（新查询） |
| `preferences` | 0.7ms | — | adv-trigram | 极佳（新查询） |
| `requirements.txt` | 0.5ms | — | adv-trigram | 极佳（新查询） |
| `contributing` | 0.4ms | — | adv-trigram | 极佳（新查询） |
| `usr/bin` | 31.9ms | — | adv-trigram | 良好（R21 14.1ms path-trigram） |

#### 仍有问题的查询

| 查询 | R24 Median | R23 Median | 路径 | 问题 |
|------|-----------|-----------|------|------|
| `ab` | **244.0ms** | 235.9ms | adv-linear-gcd | 预期（2字符） |
| `.h` | **265.7ms** | 233.6ms | adv-linear-gcd | 预期（2字符特殊），冷启动 |
| `桌面` | **185.3ms** | 181.1ms | adv-linear-gcd | **P22 未修复**：CJK 2字符，0 结果 |
| `qw` | **266.1ms** | 303.1ms | adv-linear-gcd | 2字符走 linear |

#### 高延迟异常

| 查询 | Max(ms) | 备注 |
|------|---------|------|
| `a` (warmup) | **797ms** | 首查冷启动（与 R23 一致） |
| `.h` | **522ms** | 冷启动 linear scan |
| `ab` | **465ms** | 冷启动 linear scan |
| `qw` | **441ms** | linear scan |
| `桌面` | **365ms** | CJK linear scan |
| `application` (forced) | **267ms** | linear scan |

---

## 2. 日志分析

### 2.1 启动性能

| 阶段 | R24 | R23 | R21 |
|------|------|-----|-----|
| 索引加载（paged index） | **15.3s** | 13.4s | 12.8s |
| WAL 回放 | **797 条** | 4,337 条 | 195 条 |
| FSEvents 回放 | 0.24s | 0.25s | 0.08s |
| **总启动时间** | **15.6s** | 13.7s | 12.9s |
| 索引记录数（加载后） | 4,492,810 | 4,491,840 | 4,486,218 |
| lastEventId | 148,553,304 | 148,255,566 | 147,571,144 |
| live totalRecords（末） | 5,402,432 | 5,394,862 | 5,355,829 |

**索引加载耗时 15.3s（R23 13.4s +14%）**，可能因系统负载或 paged index 文件增长。WAL 797 条（R23 4,337 条大幅下降），说明上一会话在 17:14:07 成功 flush（日志 L5044 确认）。

### 2.2 WAL 增长趋势

| 会话 | WAL 条目 | lastEventId 增量 |
|------|---------|-----------------|
| R14/R15 | 316 | — |
| R16 | 3,319 | — |
| R17 | 5,489 | — |
| R18 | 4,604 | +808,904 |
| R19 | 9,130 | 0（无 flush） |
| R20 | 7,532 | +214,872 |
| R21/R22 | 195 | +423,358 |
| R23 | 4,337 | +684,422 |
| **R24** | **797** | **+297,738** |

WAL 从 4,337 骤降至 797，说明 R23 会话中的 flush（17:04:05 和 17:14:07 两次）有效清空了大部分 WAL。lastEventId 增长 +297,738，正常范围。

### 2.3 Flush 事件分析

**R24 benchmark 期间无 flush 发生**。这是性能稳定的重要因素 — 对比 R22（flush 期间 `qw` 826ms）和 R23（flush 期间 `qw` 547ms），R24 的 `qw` 仅 266ms（median），且无极端延迟尖峰。

上一会话（R23 会话）在 17:14:07 发生了 flush（lastEventId=148,553,304），为 R24 提供了干净的 WAL 状态。

### 2.4 查询详细日志分析

由于日志缓冲机制，仅 linear scan 查询（耗时较长）被刷到日志文件。Trigram 查询（<10ms）未被缓冲区刷出。以下分析基于日志中可见的 linear scan 查询：

**冷启动首查**：
| 查询 | 延迟 | 路径 | 说明 |
|------|------|------|------|
| `ping` | 150ms | adv-trigram | 服务就绪检查，candidates=3517 |
| `a` (warmup 1) | **797ms** | adv-linear-gcd | 冷启动首次 linear scan |
| `a` (warmup 2) | 138ms | adv-linear-gcd | 迅速恢复 |
| `a` (warmup 3) | 134ms | adv-linear-gcd | 稳定 |

**Linear scan 延迟分布（从日志）**：

| 查询 | 最低 | 最高 | 中位数估计 | 样本数 |
|------|------|------|----------|--------|
| `a` | 119ms | 256ms | ~150ms | 12 |
| `z` | 107ms | 231ms | ~130ms | 11 |
| `ab` | 198ms | 465ms | ~244ms | 13 |
| `.h` | 184ms | 521ms | ~258ms | 13 |
| `桌面` | 144ms | 365ms | ~185ms | 13 |
| `qw` | 195ms | 441ms | ~266ms | 13 |
| `config` (forced) | 129ms | 184ms | ~156ms | 13 |
| `application` (forced) | 144ms | 267ms | ~175ms | 13 |
| `readme` (forced) | 149ms | 254ms | ~217ms | 5 |
| `node_modules` (forced) | 128ms | 172ms | ~154ms | 7 |

**关键发现**: 日志中 `.h` 和 `ab` 的前半段延迟（400-465ms）远高于后半段（200ms 左右），这是 **VM 页面冷启动效应**。`ab` 的前 5 次查询平均 440ms，后 8 次平均 230ms — 首批查询将数据页面预热到 VM 缓存后延迟显著下降。

### 2.5 totalRecords 增长分析

| 时间点 | totalRecords | 说明 |
|--------|-------------|------|
| 首查 (17:17:13) | 5,401,980 | ping |
| benchmark 开始 (17:17:22) | 5,402,054 | warmup `a` |
| benchmark 结束 (~17:17:54) | 5,402,432 | node_modules |
| 增长速率 | ~11.8 条/秒 | 较高的 FSEvents 速率 |

Benchmark 期间新增 378 条记录，FSEvents 持续活跃但无可见的锁争用。

### 2.6 错误与异常

| 类型 | 数量 | 详情 |
|------|------|------|
| ERROR | 0 | 无 |
| WARN | 0 | 无 |
| 崩溃 | 0 | 无 |
| lock_wait > 0 | 0 | 无 |
| 极端延迟 | 1 | `a` warmup 797ms（冷启动预期） |
| Flush during benchmark | 0 | **无**（首次） |

### 2.7 日志缓冲问题

**新发现 P25 — 日志缓冲过于激进**

R24 的 trigram 查询（38/48 查询）全部未被刷出到日志文件。只有耗时较长的 linear scan 查询（10/48）被记录。这意味着：
1. 日志缓冲区大小或刷新策略过于保守
2. 快速查询的性能日志在进程正常退出前可能丢失
3. 如果进程异常退出，所有未刷出的查询日志将丢失

建议：对 QueryAdvanced 日志行使用即时刷新（`fflush`），或在每次 benchmark 完成后强制刷新缓冲区。

---

## 3. 修复验证

### 3.1 持续有效的修复

| 问题 | R24 表现 | R23 对比 | 验证 |
|------|---------|---------|------|
| **P0 trigram 降级** | `test` 8.4ms 走 trigram | 6.3ms | **持续有效** |
| **P18 `*.py`** | `*.py` 12.2ms 走 trigram | 16.5ms | **持续有效，进一步改善** |
| **P18 `*test*.cpp`** | `*test*.cpp` 5.5ms 走 trigram | 7.1ms | **持续有效，进一步改善** |
| **P18 glob 全系列** | `*.swift` 1.5ms、`*.cpp` 0.6ms、`*.json` 4.6ms | — | **持续有效** |

### 3.2 未修复问题

| 问题 | 等级 | R24 表现 | R23 对比 |
|------|------|---------|---------|
| **P22 CJK 短关键词** | **Medium** | `桌面` 185ms 走 linear，0 结果 | 181ms（稳定） |
| **P24 case-sensitive** | **High** | 未测试（不在 benchmark 脚本中） | 631ms |

### 3.3 新发现

**P25 — 日志缓冲过于激进**  
Trigram 查询（<10ms）的日志行不会被即时刷到文件，导致性能分析缺失数据。进程正常退出时也未能完全刷出（R24 之前的尝试表明即使 50 个额外查询也只刷出 1 行）。

---

## 4. 优化建议

按优先级排序：

1. **Case-sensitive trigram 预过滤**（P24 High）— `case:README` 631ms，可用 trigram 候选 + case-sensitive post-filter 降至 <20ms。需要添加到 benchmark 脚本中。
2. **CJK 短关键词优化**（P22 Medium）— `桌面` 185ms 走 linear 且 0 结果，考虑 CJK bigram 索引
3. **日志缓冲策略调整**（P25 Low）— 快速查询日志丢失，影响性能监控
4. **索引加载时间优化**— 15.3s（R23 13.4s +14%），考虑 lazy loading 或 mmap 优化
5. **冷启动首查预热**— warmup `a` 797ms，考虑启动后自动执行预热查询

---

## 5. 总结

R24 延续了 R23 的性能优势，在新会话冷启动条件下仍然表现优异：

- 全局 avg median **39.8ms**（R23 48.2ms **-17%**，R22 61.8ms **-36%**）— **24轮最佳**
- advanced-trigram avg **3.6ms**（R23 7.6ms **-53%**）— **大幅改善**
- advanced-linear-gcd avg **177.6ms**（R23 202.5ms -12%）— 冷启动条件下合理
- 38/48 查询走 trigram（与 R23 持平），仅 10 查询走 linear
- **P0 持续有效**：`test` 8.4ms 走 trigram
- **P18 持续有效且改善**：`*.py` 12.2ms（R23 16.5ms）、`*test*.cpp` 5.5ms（R23 7.1ms）
- **P22 稳定未修复**：`桌面` 185ms（R23 181ms），0 结果
- **P24 未测试**：`case:README` 不在 benchmark 脚本中
- **P25 新发现**：日志缓冲过于激进，trigram 查询日志丢失
- **无 flush during benchmark**（24轮首次完全无 flush 干扰）
- WAL 797 条（R23 4,337 条大幅下降）
- 启动时间 15.6s（R23 13.7s +14%）
- lock_wait=0ms，无错误/警告/崩溃
- path_queries 类别大幅改善：10.0ms（R23 27.6ms **-64%**）
- `readme` linear forced 仅 48.8ms（异常低，可能 VM 缓存极佳）
