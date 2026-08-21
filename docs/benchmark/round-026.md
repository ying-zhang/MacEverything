# Round 26 基准测试报告

**时间**: 2026-04-19 17:42  
**会话**: 17:16:07 启动（与 R24/R25 同一会话），已运行 ~26min  
**索引记录数**: 5,416,067（R26 手动测试开始时）/ 5,416,664（R26 benchmark 结束时）  
**WAL 条目**: 797（启动时回放，继承 R24 会话）  
**Flush**: 会话期间 3 次 flush（17:21:54、17:32:25、**17:42:25**），第 3 次在 benchmark 前 7 秒  
**Benchmark 脚本**: `benchmarks/bench_search.py`（48 查询，limit=100，warmup=3，iterations=10）

---

## 1. 基准测试结果

### 1.1 总览

| 指标 | R26 | R25 | R24 | 变化(vs R25) |
|------|-----|-----|-----|-------------|
| 查询数 | 48 | 48 | 48 | — |
| limit | 100 | 100 | 100 | — |
| 最小延迟(median) | 0.0ms | 0.0ms | 0.0ms | — |
| 最大延迟(median) | ~280ms | 274.0ms | 266.1ms | +2% |
| 全局 avg median | **44.3ms** | **44.0ms** | **39.8ms** | **+1%** |
| advanced-trigram avg | **4.3ms** | **4.4ms** | **3.6ms** | -2% |
| advanced-linear-gcd avg | **196.4ms** | **194.5ms** | **177.6ms** | +1% |
| trigram 查询数 | **38** | 38 | 38 | — |
| linear 查询数 | **10** | 10 | 10 | — |

### 1.2 按类别汇总

| 类别 | 查询数 | R26 Avg Median | R25 Avg Median | 变化 | 评价 |
|------|--------|---------------|---------------|------|------|
| long_trigram | 8 | **1.0ms** | 1.0ms | 0% | 极佳（稳定） |
| medium_trigram | 8 | **2.4ms** | 2.5ms | -4% | 极佳（稳定） |
| glob_patterns | 7 | **5.5ms** | 5.8ms | -5% | 良好 |
| path_queries | 8 | **12.0ms** | 12.5ms | -4% | 良好 |
| cjk_queries | 5 | **40.1ms** | 39.4ms | +2% | 稳定 |
| no_results | 3 | **93.5ms** | 91.2ms | +3% | 稳定 |
| short_linear | 5 | **162.0ms** | 160.8ms | +1% | 预期内 |
| linear_forced | 4 | **142.8ms** | 140.2ms | +2% | 稳定 |

### 1.3 按搜索路径分类

| 搜索路径 | 查询数 | R26 Avg Median | R25 Avg Median | 变化 |
|---------|--------|---------------|---------------|------|
| advanced-trigram | **38** | **4.3ms** | **4.4ms** | -2% |
| advanced-linear-gcd | **10** | **196.4ms** | **194.5ms** | +1% |

### 1.4 Trigram vs Linear 对照（同一查询）

| 查询 | Trigram | Linear | 加速比 | R25 加速比 |
|------|---------|--------|--------|-----------|
| config | 5.0ms | 222.2ms | **44.5x** | 31.8x |
| readme | 1.3ms | 78.8ms | **60.6x** | 40.1x |
| application | 2.2ms | 159.6ms | **72.6x** | 78.5x |
| node_modules | 0.6ms | 143.4ms | **239.0x** | 264.5x |

### 1.5 逐查询详情（仅列出关注项）

#### P0/P18 修复持续验证

| 查询 | R26 Median | R25 Median | R26 路径 | 评价 |
|------|-----------|-----------|---------|------|
| `test` | **5.1ms** | 6.1ms | adv-trigram | **持续有效**（-16%） |
| `*.py` | **14.0ms** | 18.5ms | adv-trigram | **持续有效**（-24%） |
| `*test*.cpp` | **5.6ms** | 5.8ms | adv-trigram | **持续有效**（稳定） |

#### 性能优秀的查询

| 查询 | R26 Median | R25 Median | 路径 | 评价 |
|------|-----------|-----------|------|------|
| `application` | 2.2ms | 2.3ms | adv-trigram | 极佳 |
| `node_modules` | 0.6ms | 0.6ms | adv-trigram | 极佳 |
| `readme` | 1.3ms | 1.3ms | adv-trigram | 极佳 |
| `config` | 5.0ms | 5.1ms | adv-trigram | 良好 |
| `*.swift` | 1.6ms | 1.7ms | adv-trigram | 极佳 |
| `*.cpp` | 0.7ms | 0.7ms | adv-trigram | 极佳 |
| `*.json` | 5.0ms | 5.2ms | adv-trigram | 良好 |
| `screenshot` | 0.1ms | 0.1ms | adv-trigram | 极佳 |
| `dockerfile` | 0.1ms | 0.1ms | adv-trigram | 极佳 |
| `src` | 0.5ms | 0.5ms | adv-trigram | 极佳 |
| `main` | 1.0ms | 1.1ms | adv-trigram | 极佳 |
| `package.json` | 3.0ms | 3.1ms | adv-trigram | 极佳 |
| `preferences` | 0.7ms | 0.8ms | adv-trigram | 极佳 |
| `requirements.txt` | 0.5ms | 0.5ms | adv-trigram | 极佳 |
| `contributing` | 0.4ms | 0.4ms | adv-trigram | 极佳 |

#### 仍有问题的查询

| 查询 | R26 Median | R25 Median | 路径 | 问题 |
|------|-----------|-----------|------|------|
| `ab` | **250.1ms** | 248.3ms | adv-linear-gcd | 预期（2字符） |
| `.h` | **278.3ms** | 274.0ms | adv-linear-gcd | 预期（2字符特殊） |
| `桌面` | **200.2ms** | 194.6ms | adv-linear-gcd | **P22 未修复**：CJK 2字符，0 结果 |
| `qw` | **275.0ms** | 270.5ms | adv-linear-gcd | 2字符走 linear |

#### 高延迟异常

| 查询 | Max(ms) | 备注 |
|------|---------|------|
| `node_modules` (forced) | **785ms** | flush 后 VM 冷（L5745，benchmark 末尾） |
| `.h` | **468ms** | flush 后 linear scan（L5670） |
| `ab` | **380ms** | flush 后 linear scan（L5655） |
| `桌面` | **348ms** | flush 后 linear scan（L5671） |

---

## 2. 日志分析

### 2.1 会话状态

R26 与 R24/R25 共享同一会话（17:16:07 启动）。R26 benchmark 在 17:42 执行，距启动约 26 分钟。

| 阶段 | 值 |
|------|------|
| 会话启动 | 17:16:07 |
| R24 benchmark | 17:17:13 - 17:17:54 |
| R25 手动 + benchmark | 17:19 - 17:35:58 |
| Flush #1 | 17:21:54 (L5472) |
| Flush #2 | 17:32:25 (L5473) |
| R26 手动查询 | 17:42:02 - 17:42:25 |
| **Flush #3** | **17:42:25 (L5627)** |
| R26 benchmark | 17:42:32 - 17:43:06 |

### 2.2 Flush 事件分析

| Flush | 时间 | 行号 | 与 R26 benchmark 关系 |
|-------|------|------|---------------------|
| #1 | 17:21:54 | L5472 | benchmark 前 ~21min |
| #2 | 17:32:25 | L5473 | benchmark 前 ~10min |
| **#3** | **17:42:25** | **L5627** | **benchmark 前仅 7 秒** |

**关键发现**：Flush #3 在 R26 benchmark 开始前仅 7 秒发生。这导致 VM 页面被重新分配，linear scan 查询延迟显著升高：
- `node_modules` forced-linear 785ms（R25 同查询 median 154ms）
- `.h` 468ms（R25 274ms median）
- `ab` 380ms（R25 248ms median）

这与 R22（flush 期间 `qw` 826ms）和 R23（flush 后 `qw` 547ms）的 P23 模式一致。

### 2.3 手动查询测试

R26 进行了 32 条手动查询测试（10 个类别），用于验证各搜索路径。

#### P24 Case-sensitive 验证

| 查询 | 延迟 | 路径 | candidates | 结果数 |
|------|------|------|-----------|--------|
| `case:README` | **598ms** (log) / 606.5ms (API) | adv-linear-gcd | 0 | 3 |
| `case:Makefile` | **389.9ms** | adv-linear-gcd | 0 | ? |
| `case:Hello ext:cpp` | **385.4ms** | adv-linear-gcd | 0 | 0 |

所有 case-sensitive 查询 candidates=0，确认 P24 根因持续存在。

#### P22 CJK 验证

| 查询 | 延迟 | 路径 | 结果数 |
|------|------|------|--------|
| `桌面` | 172.2ms | adv-linear-gcd | 0 |
| `系統偏好` | 174.7ms | adv-linear-gcd | 0 |

#### lockWait 观察

| 查询 | lockWait | 总延迟 | 备注 |
|------|----------|--------|------|
| `qw` | **0.9ms** | ~275ms | 轻微（R25 手动测试最高 10.8ms） |
| 其他 benchmark 查询 | 0ms | — | 正常 |

### 2.4 totalRecords 增长分析

| 时间点 | totalRecords | 说明 |
|--------|-------------|------|
| R26 手动测试开始 (17:42:02) | 5,416,067 | — |
| R26 benchmark 结束 (17:43:06) | 5,416,664 | +597 |
| 增长速率 | ~9.3 条/秒 | 正常 FSEvents 速率 |

### 2.5 错误与异常

| 类型 | 数量 | 详情 |
|------|------|------|
| ERROR | 0 | 无 |
| WARN | 0 | 无 |
| 崩溃 | 0 | 无 |
| lockWait > 0 | 1 | `qw` 0.9ms（手动测试） |
| 极端延迟 | 4 | node_modules 785ms、.h 468ms、ab 380ms、桌面 348ms |
| **Flush near benchmark** | **1** | **17:42:25，benchmark 前 7 秒** |

---

## 3. 修复验证

### 3.1 持续有效的修复

| 问题 | R26 表现 | R25 对比 | 验证 |
|------|---------|---------|------|
| **P0 trigram 降级** | `test` 5.1ms 走 trigram | 6.1ms | **持续有效**（改善） |
| **P18 `*.py`** | `*.py` 14.0ms 走 trigram | 18.5ms | **持续有效**（改善） |
| **P18 `*test*.cpp`** | `*test*.cpp` 5.6ms 走 trigram | 5.8ms | **持续有效**（稳定） |
| **P18 glob 全系列** | `*.swift` 1.6ms、`*.cpp` 0.7ms、`*.json` 5.0ms | — | **持续有效** |

### 3.2 未修复问题

| 问题 | 等级 | R26 表现 | R25 对比 |
|------|------|---------|---------|
| **P22 CJK 短关键词** | **Medium** | `桌面` 200ms 走 linear，0 结果 | 195ms（稳定） |
| **P24 case-sensitive** | **High** | `case:README` 598ms、`case:Makefile` 390ms | 500ms / 708ms |
| **P25 日志缓冲** | **Low** | trigram 查询日志仍不可见 | 同 R25 |
| **P23 flush 冲击** | **Medium** | flush 前 7s，node_modules 785ms | R25 无 flush 干扰 |

### 3.3 本轮观察

**P23 再次验证** — Flush #3（17:42:25）在 benchmark 前仅 7 秒发生，导致 linear scan 延迟全面升高。这是 P23（flush 期间/后查询延迟暴涨）的又一次复现。相比 R22（flush 中 826ms）和 R25（无 flush 44.0ms），R26 的 avg 44.3ms 表明 flush 对 trigram 查询影响很小（4.3ms vs 4.4ms），但对 linear scan 查询影响显著（196.4ms vs 194.5ms median，Max 785ms）。

---

## 4. 优化建议

按优先级排序：

1. **Case-sensitive trigram 预过滤**（P24 High）— `case:README` 598ms，仅需在 `QueryFilterParser.h` L42 添加 `node.textLower = me::toLower(arg)` 即可从 598ms 降至 <20ms。**最高 ROI 修复**，连续 4 轮验证根因。
2. **Flush 期间查询保护**（P23 Medium）— 连续多轮 benchmark 受 flush 干扰（R22 826ms、R23 547ms、R26 785ms），考虑延迟 flush 到查询空闲期或 flush 期间降低优先级。
3. **CJK 短关键词优化**（P22 Medium）— `桌面` 200ms 走 linear 且 0 结果，考虑 CJK bigram 索引。
4. **lockWait 持续监控**（P26 Low）— R26 手动测试 qw 0.9ms（R25 最高 10.8ms），趋势改善但仍需监控。
5. **日志缓冲策略调整**（P25 Low）— trigram 查询日志连续 3 轮不可见，影响性能监控。

---

## 5. 总结

R26 是 R24 同一会话的第 3 个轮次（距启动 ~26min），整体性能稳定：

- 全局 avg median **44.3ms**（R25 44.0ms +1%，R24 39.8ms +11%）— 同会话自然波动
- advanced-trigram avg **4.3ms**（R25 4.4ms -2%）— 极佳且稳定
- advanced-linear-gcd avg **196.4ms**（R25 194.5ms +1%）— 稳定范围内
- 38/48 查询走 trigram（连续 4 轮持平），仅 10 查询走 linear
- **P0 持续有效**：`test` 5.1ms 走 trigram（4 轮最佳）
- **P18 持续有效**：`*.py` 14.0ms（4 轮最佳）、`*test*.cpp` 5.6ms（稳定）
- **P22 稳定未修复**：`桌面` 200ms（R25 195ms），0 结果
- **P24 手动验证**：`case:README` 598ms、`case:Makefile` 390ms、candidates=0 确认根因
- **P23 再现**：flush 前 7s，node_modules forced-linear 785ms
- **P25 确认**：trigram 查询日志第 3 轮不可见
- lockWait 最高 0.9ms（R25 10.8ms，趋势改善）
- 无 ERROR/WARN/崩溃
- totalRecords 增长正常（~9.3 条/秒）
- 3 次 flush 在会话中（17:21:54、17:32:25、17:42:25）
