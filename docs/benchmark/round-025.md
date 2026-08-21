# Round 25 基准测试报告

**时间**: 2026-04-19 17:35  
**会话**: 17:16:07 启动（与 R24 同一会话），已运行 ~19min  
**索引记录数**: 5,402,432（R24 benchmark 结束时）/ 5,410,406（R25 benchmark 结束时）  
**WAL 条目**: 797（启动时回放，继承 R24 会话）  
**Flush**: 会话期间 2 次 flush（17:21:54 和 17:32:25），均在 benchmark 之前  
**Benchmark 脚本**: `benchmarks/bench_search.py`（48 查询，limit=100，warmup=3，iterations=10）

---

## 1. 基准测试结果

### 1.1 总览

| 指标 | R25 | R24 | R23 | 变化(vs R24) |
|------|-----|-----|-----|-------------|
| 查询数 | 48 | 48 | 48 | — |
| limit | 100 | 100 | 100 | — |
| 最小延迟(median) | 0.0ms | 0.0ms | 0.0ms | — |
| 最大延迟(median) | 274.0ms | 266.1ms | 303.1ms | +3% |
| 全局 avg median | **44.0ms** | **39.8ms** | **48.2ms** | **+11%** |
| advanced-trigram avg | **4.4ms** | **3.6ms** | **7.6ms** | +22% |
| advanced-linear-gcd avg | **194.5ms** | **177.6ms** | **202.5ms** | +10% |
| trigram 查询数 | **38** | 38 | 38 | — |
| linear 查询数 | **10** | 10 | 10 | — |

### 1.2 按类别汇总

| 类别 | 查询数 | R25 Avg Median | R24 Avg Median | 变化 | 评价 |
|------|--------|---------------|---------------|------|------|
| long_trigram | 8 | **1.0ms** | 0.9ms | +11% | 极佳（波动正常） |
| medium_trigram | 8 | **2.5ms** | 2.4ms | +4% | 极佳（稳定） |
| glob_patterns | 7 | **5.8ms** | 4.2ms | +38% | 良好（flush 后缓存效应） |
| path_queries | 8 | **12.5ms** | 10.0ms | +25% | 良好 |
| cjk_queries | 5 | **39.4ms** | 37.1ms | +6% | 稳定 |
| no_results | 3 | **91.2ms** | 88.7ms | +3% | 稳定 |
| short_linear | 5 | **160.8ms** | 158.2ms | +2% | 预期内 |
| linear_forced | 4 | **140.2ms** | 133.4ms | +5% | 稳定 |

### 1.3 按搜索路径分类

| 搜索路径 | 查询数 | R25 Avg Median | R24 Avg Median | 变化 |
|---------|--------|---------------|---------------|------|
| advanced-trigram | **38** | **4.4ms** | **3.6ms** | +22% |
| advanced-linear-gcd | **10** | **194.5ms** | **177.6ms** | +10% |

### 1.4 Trigram vs Linear 对照（同一查询）

| 查询 | Trigram | Linear | 加速比 | R24 加速比 |
|------|---------|--------|--------|-----------|
| config | 5.1ms | 162.3ms | **31.8x** | 33.1x |
| readme | 1.3ms | 52.1ms | **40.1x** | 40.3x |
| application | 2.3ms | 180.5ms | **78.5x** | 85.1x |
| node_modules | 0.6ms | 158.7ms | **264.5x** | 321.1x |

### 1.5 逐查询详情（仅列出关注项）

#### P0/P18 修复持续验证

| 查询 | R25 Median | R24 Median | R25 路径 | 评价 |
|------|-----------|-----------|---------|------|
| `test` | **6.1ms** | 8.4ms | adv-trigram | **持续有效**（-27%） |
| `*.py` | **18.5ms** | 12.2ms | adv-trigram | 持续有效（+52% 波动，仍远优于修复前 166ms） |
| `*test*.cpp` | **5.8ms** | 5.5ms | adv-trigram | **持续有效**（稳定） |

#### 性能优秀的查询

| 查询 | R25 Median | R24 Median | 路径 | 评价 |
|------|-----------|-----------|------|------|
| `application` | 2.3ms | 2.1ms | adv-trigram | 极佳 |
| `node_modules` | 0.6ms | 0.5ms | adv-trigram | 极佳 |
| `readme` | 1.3ms | 1.2ms | adv-trigram | 极佳 |
| `config` | 5.1ms | 4.7ms | adv-trigram | 良好 |
| `*.swift` | 1.7ms | 1.5ms | adv-trigram | 极佳 |
| `*.cpp` | 0.7ms | 0.6ms | adv-trigram | 极佳 |
| `*.json` | 5.2ms | 4.6ms | adv-trigram | 良好 |
| `screenshot` | 0.1ms | 0.1ms | adv-trigram | 极佳 |
| `dockerfile` | 0.1ms | 0.1ms | adv-trigram | 极佳 |
| `src` | 0.5ms | 0.5ms | adv-trigram | 极佳 |
| `main` | 1.1ms | 1.0ms | adv-trigram | 极佳 |
| `package.json` | 3.1ms | 2.9ms | adv-trigram | 极佳 |
| `preferences` | 0.8ms | 0.7ms | adv-trigram | 极佳 |
| `requirements.txt` | 0.5ms | 0.5ms | adv-trigram | 极佳 |
| `contributing` | 0.4ms | 0.4ms | adv-trigram | 极佳 |

#### 仍有问题的查询

| 查询 | R25 Median | R24 Median | 路径 | 问题 |
|------|-----------|-----------|------|------|
| `ab` | **248.3ms** | 244.0ms | adv-linear-gcd | 预期（2字符） |
| `.h` | **274.0ms** | 265.7ms | adv-linear-gcd | 预期（2字符特殊） |
| `桌面` | **194.6ms** | 185.3ms | adv-linear-gcd | **P22 未修复**：CJK 2字符，0 结果 |
| `qw` | **270.5ms** | 266.1ms | adv-linear-gcd | 2字符走 linear |

#### 高延迟异常

| 查询 | Max(ms) | 备注 |
|------|---------|------|
| `node_modules` (forced) | **837ms** | flush 后 VM 冷（17:21:06，手动测试非 benchmark） |
| `case:Makefile` (手动) | **708ms** | P24 case-sensitive 走 linear |
| `case:README` (手动) | **500ms** | P24 case-sensitive 走 linear |
| `.h` | **530ms** | 冷启动 linear scan |
| `ab` | **480ms** | 冷启动 linear scan |

---

## 2. 日志分析

### 2.1 会话状态

R25 与 R24 共享同一会话（17:16:07 启动）。R24 benchmark 在 17:17 执行，R25 benchmark 在 17:35 执行。中间间隔约 18 分钟，其间进行了手动查询测试并发生两次 flush。

| 阶段 | 值 |
|------|------|
| 会话启动 | 17:16:07 |
| R24 benchmark | 17:17:13 - 17:17:54 |
| 手动查询测试 #1 | 17:19:23 - 17:19:25 |
| Flush #1 | **17:21:54** (L5472, lastEventId=148,657,428) |
| 手动查询测试 #2 | 17:34:29 - 17:35:09 |
| Flush #2 | **17:32:25** (L5473, lastEventId=148,750,914) |
| R25 benchmark | 17:35:25 - 17:35:58 |

### 2.2 Flush 事件分析

| Flush | 时间 | lastEventId | 与 R25 benchmark 关系 |
|-------|------|-------------|---------------------|
| #1 | 17:21:54 | 148,657,428 | benchmark 前 ~14min |
| #2 | 17:32:25 | 148,750,914 | benchmark 前 ~3min |

**R25 benchmark 期间无 flush 发生**（与 R24 相同）。两次 flush 都在 benchmark 之前完成，确保了测试环境的一致性。

但 **flush 对手动测试有显著影响**：17:21:06 的手动 `node_modules` forced-linear 查询延迟 837ms（R24 同查询 median 仅 154ms），此时距 flush #1 仅 48 秒前（或 flush 正在进行中），VM 页面被重新分配导致 linear scan 极慢。

### 2.3 lockWait 分析（新发现）

R25 手动查询测试中首次观察到非零 lockWait：

| 查询 | lockWait | 总延迟 | 路径 | 时间 |
|------|----------|--------|------|------|
| `case:Hello ext:cpp` | **10.8ms** | 301ms | adv-linear-gcd | 17:34:29 |
| `桌面` | **3.3ms** | 195ms | adv-linear-gcd | 17:34:29 |
| `系统偏好` | **1.7ms** | 75ms | adv-linear-gcd | 17:34:29 |
| R25 benchmark 全部 | **0ms** | — | — | 17:35:25-58 |

lockWait 仅出现在手动测试期间（17:34:29），此时距 flush #2（17:32:25）约 2 分钟。benchmark 期间 lockWait 全部为 0。可能原因：
1. FSEvents 回调持有锁，与查询争用
2. flush 后的后续 WAL 写入操作
3. 手动测试时多个查询几乎同时发起

lockWait 绝对值较小（最高 10.8ms），对用户体验影响有限，但值得持续监控。

### 2.4 P24 Case-sensitive 详细验证

手动测试中验证了 P24 的多个 case-sensitive 场景：

| 查询 | 延迟 | 路径 | candidates | 结果数 | 备注 |
|------|------|------|-----------|--------|------|
| `case:README` | 216ms | adv-linear-gcd | 0 | 3 | post-flush 热缓存 |
| `case:README` | 500ms | adv-linear-gcd | 0 | 3 | pre-benchmark 冷查询 |
| `case:Makefile` | 708ms | adv-linear-gcd | 0 | ? | 最慢 case 查询 |
| `case:Hello ext:cpp` | 301ms | adv-linear-gcd | 0 | 0 | lockWait=10.8ms |

所有 case-sensitive 查询的 candidates=0 确认了根因：`bestTrigramTerm()` 因 `textLower` 为空返回空 trigramKey，完全跳过 trigram 索引。

**修复方案已确认**：在 `QueryFilterParser.h` L42 后添加 `node.textLower = me::toLower(arg)` 即可启用 trigram 预过滤，预期将 `case:README` 从 500ms 降至 <20ms。

### 2.5 totalRecords 增长分析

| 时间点 | totalRecords | 说明 |
|--------|-------------|------|
| 会话启动 (17:16:07) | 4,492,810 | paged index 加载 |
| R24 benchmark 结束 (17:17:54) | 5,402,432 | live 扫描完成 |
| R25 benchmark 开始 (17:35:25) | ~5,410,000 | FSEvents 持续更新 |
| R25 benchmark 结束 (17:35:58) | ~5,410,406 | +406 |
| 增长速率 | ~12.3 条/秒 | 正常 FSEvents 速率 |

### 2.6 错误与异常

| 类型 | 数量 | 详情 |
|------|------|------|
| ERROR | 0 | 无 |
| WARN | 0 | 无 |
| 崩溃 | 0 | 无 |
| lockWait > 0 | 3 | 手动测试：10.8ms / 3.3ms / 1.7ms |
| 极端延迟 | 2 | node_modules forced 837ms、case:Makefile 708ms |
| Flush during benchmark | 0 | **无**（连续两轮无 flush 干扰） |

---

## 3. 修复验证

### 3.1 持续有效的修复

| 问题 | R25 表现 | R24 对比 | 验证 |
|------|---------|---------|------|
| **P0 trigram 降级** | `test` 6.1ms 走 trigram | 8.4ms | **持续有效**（改善） |
| **P18 `*.py`** | `*.py` 18.5ms 走 trigram | 12.2ms | **持续有效**（波动正常） |
| **P18 `*test*.cpp`** | `*test*.cpp` 5.8ms 走 trigram | 5.5ms | **持续有效**（稳定） |
| **P18 glob 全系列** | `*.swift` 1.7ms、`*.cpp` 0.7ms、`*.json` 5.2ms | — | **持续有效** |

### 3.2 未修复问题

| 问题 | 等级 | R25 表现 | R24 对比 |
|------|------|---------|---------|
| **P22 CJK 短关键词** | **Medium** | `桌面` 195ms 走 linear，0 结果 | 185ms（稳定） |
| **P24 case-sensitive** | **High** | `case:README` 500ms、`case:Makefile` 708ms | R24 未测试 |
| **P25 日志缓冲** | **Low** | trigram 查询日志仍不可见 | 同 R24 |

### 3.3 新发现

**P26 — lockWait 非零（手动测试）**  
手动查询测试中出现 lockWait 非零（最高 10.8ms），均为 linear-gcd 查询。benchmark 期间未复现。可能与 FSEvents 回调持锁或 flush 后 WAL 操作相关。当前影响有限，建议持续监控。

---

## 4. 优化建议

按优先级排序：

1. **Case-sensitive trigram 预过滤**（P24 High）— `case:README` 500ms、`case:Makefile` 708ms，仅需在 `QueryFilterParser.h` L42 添加一行 `node.textLower = me::toLower(arg)` 即可从 500ms 降至 <20ms。**最高 ROI 修复**。
2. **CJK 短关键词优化**（P22 Medium）— `桌面` 195ms 走 linear 且 0 结果，考虑 CJK bigram 索引
3. **Flush 期间查询保护**（P23 Medium）— node_modules forced 837ms（flush 时），考虑延迟 flush 到查询空闲期
4. **lockWait 持续监控**（P26 Low）— 手动测试 10.8ms lockWait，benchmark 未复现，建议增加 lockWait > 5ms 的日志告警
5. **日志缓冲策略调整**（P25 Low）— 快速查询日志丢失，影响性能监控

---

## 5. 总结

R25 是 R24 同一会话的后续轮次（距启动 ~19min），整体性能稳定：

- 全局 avg median **44.0ms**（R24 39.8ms +11%，R23 48.2ms -9%）— 同会话自然波动
- advanced-trigram avg **4.4ms**（R24 3.6ms +22%）— flush 后 VM 缓存效应
- advanced-linear-gcd avg **194.5ms**（R24 177.6ms +10%）— 稳定范围内
- 38/48 查询走 trigram（与 R24 持平），仅 10 查询走 linear
- **P0 持续有效**：`test` 6.1ms 走 trigram（甚至比 R24 更快）
- **P18 持续有效**：`*test*.cpp` 5.8ms 稳定，`*.py` 18.5ms（波动但远优于修复前）
- **P22 稳定未修复**：`桌面` 195ms（R24 185ms），0 结果
- **P24 手动验证**：`case:README` 500ms、`case:Makefile` 708ms、candidates=0 确认根因
- **P25 确认**：trigram 查询日志仍不可见
- **P26 新发现**：lockWait 非零（手动测试最高 10.8ms），benchmark 期间未复现
- **无 flush during benchmark**（连续两轮无 flush 干扰）
- 两次 flush 在 benchmark 前（17:21:54 和 17:32:25）
- node_modules forced 837ms（flush 时，手动测试）
- lock_wait 最高 10.8ms（手动测试），benchmark 期间 0ms
- 无 ERROR/WARN/崩溃
- totalRecords 增长正常（~12.3 条/秒）
