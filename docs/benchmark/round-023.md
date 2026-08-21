# Round 23 基准测试报告

**时间**: 2026-04-19 17:03  
**会话**: 16:58:19 启动，已运行 ~5min  
**索引记录数**: 4,492,047（benchmark 开始时服务报告）/ 5,394,862（benchmark 结束时日志）  
**WAL 条目**: 4,337（启动时回放，较 R22 大幅增长）  
**Flush**: benchmark 期间发生 1 次 flush（17:04:05，L4980）  
**Benchmark 脚本**: `benchmarks/bench_search.py`（48 查询，limit=100，warmup=3，iterations=10）

---

## 1. 基准测试结果

### 1.1 总览

| 指标 | R23 | R22 | R21 | 变化(vs R22) |
|------|-----|-----|-----|-------------|
| 查询数 | 48 | 48 | 48 | — |
| limit | 100 | 100 | 100 | — |
| 最小延迟(median) | 0.0ms | 0.0ms | 0.0ms | — |
| 最大延迟(median) | 303.1ms | 358.3ms | 299.7ms | -15% |
| 全局 avg median | **48.2ms** | **61.8ms** | **50.4ms** | **-22%** |
| advanced-trigram avg | **7.6ms** | **6.6ms** | **5.8ms** | +15% |
| advanced-linear-gcd avg | **202.5ms** | **207.1ms** | **169.7ms** | -2% |
| trigram 查询数 | **38** | 34 | 34 | **+4** |
| linear 查询数 | **10** | 13 | 13 | **-3** |

### 1.2 按类别汇总

| 类别 | 查询数 | R23 Avg Median | R22 Avg Median | 变化 | 评价 |
|------|--------|---------------|---------------|------|------|
| long_trigram | 8 | **1.0ms** | 0.8ms | +25% | 极佳 |
| medium_trigram | 8 | **2.2ms** | 25.3ms | **-91%** | **P0 修复生效** |
| glob_patterns | 7 | **6.2ms** | 58.1ms | **-89%** | **P18 修复生效** |
| path_queries | 8 | **27.6ms** | 30.3ms | -9% | 良好 |
| cjk_queries | 5 | **36.2ms** | 45.0ms | -20% | 改善 |
| no_results | 3 | **101.0ms** | 119.4ms | -15% | 改善 |
| short_linear | 5 | **147.3ms** | 149.2ms | -1% | 预期内 |
| linear_forced | 4 | **201.0ms** | 194.6ms | +3% | 对照组稳定 |

### 1.3 按搜索路径分类

| 搜索路径 | 查询数 | R23 Avg Median | R22 Avg Median | 变化 |
|---------|--------|---------------|---------------|------|
| advanced-trigram | **38** | **7.6ms** | **6.6ms** (n=34) | +15%（含新增查询） |
| advanced-linear-gcd | **10** | **202.5ms** | **207.1ms** (n=13) | -2% |

### 1.4 Trigram vs Linear 对照（同一查询）

| 查询 | Trigram | Linear | 加速比 | R22 加速比 |
|------|---------|--------|--------|-----------|
| config | 4.4ms | 214.6ms | **48.8x** | 92.6x |
| readme | 1.2ms | 202.0ms | **162.9x** | 156.0x |
| application | 1.6ms | 214.5ms | **132.8x** | 133.8x |
| node_modules | 0.6ms | 172.8ms | **305.9x** | 387.2x |

### 1.5 逐查询详情（仅列出关注项）

#### P0/P18 修复验证 — 从 linear 迁移到 trigram

| 查询 | R23 Median | R22 Median | R23 路径 | R22 路径 | 加速 |
|------|-----------|-----------|---------|---------|------|
| `test` | **6.3ms** | 192.7ms | **adv-trigram** | adv-linear-gcd | **30.6x** |
| `*.py` | **16.5ms** | 166.3ms | **adv-trigram** | adv-linear-gcd | **10.1x** |
| `*test*.cpp` | **7.1ms** | 226.3ms | **adv-trigram** | adv-linear-gcd | **31.9x** |

#### 性能优秀的查询

| 查询 | R23 Median | R22 Median | 路径 | 评价 |
|------|-----------|-----------|------|------|
| `application` (trigram) | 1.6ms | 1.3ms | adv-trigram | 极佳 |
| `node_modules` (trigram) | 0.6ms | 0.5ms | adv-trigram | 极佳 |
| `readme` (trigram) | 1.2ms | 1.3ms | adv-trigram | 极佳 |
| `config` (trigram) | 4.4ms | 2.3ms | adv-trigram | 良好 |
| `*.swift` | 1.8ms | 2.6ms | adv-trigram | 极佳 |
| `*.cpp` | 1.0ms | 0.9ms | adv-trigram | 极佳 |
| `*.json` | 7.9ms | 4.6ms | adv-trigram | 良好 |
| `screenshot` | 0.2ms | 0.1ms | adv-trigram | 极佳 |
| `dockerfile` | 0.3ms | 0.1ms | adv-trigram | 极佳 |
| `src` | 0.4ms | — | adv-trigram | 极佳（新查询） |
| `main` | 1.1ms | — | adv-trigram | 极佳（新查询） |

#### 仍有问题的查询

| 查询 | R23 Median | R22 Median | 路径 | 问题 |
|------|-----------|-----------|------|------|
| `ab` | **235.9ms** | 263.7ms | adv-linear-gcd | 预期（2字符） |
| `.h` | **233.6ms** | 246.8ms | adv-linear-gcd | 预期（2字符特殊） |
| `桌面` | **181.1ms** | 224.9ms | adv-linear-gcd | **P22 未修复**：CJK 2字符，0 结果 |
| `qw` | **303.1ms** | 358.3ms | adv-linear-gcd | 2字符走 linear |

#### 高延迟异常

| 查询 | Max(ms) | 备注 |
|------|---------|------|
| `a` (warmup) | **796ms** | 首查冷启动 |
| `case:README` | **631ms** | case-sensitive 走 linear，**新发现** |
| `case:Hello ext:cpp` | **650ms** | case-sensitive 走 linear，**新发现** |
| `qw` | **547ms** | flush 后 VM 页面冷 |
| `.h` | **319ms** | linear scan |
| `application` (forced) | **328ms** | linear scan |
| `config` (forced) | **324ms** | linear scan |

---

## 2. 日志分析

### 2.1 启动性能

| 阶段 | R23 | R22 (同会话) | R21 |
|------|------|-------------|-----|
| 索引加载（paged index） | **13.4s** | — | 12.8s |
| WAL 回放 | **4,337 条** | 195 (继承) | 195 条 / 0.48s |
| FSEvents 回放 | 0.25s | — | 0.08s |
| **总启动时间** | **13.7s** | — | 12.9s |
| 索引记录数（加载后） | 4,491,734 | 4,488,943 | 4,486,218 |
| lastEventId | 148,255,566 | 147,571,144 | 147,571,144 |
| live totalRecords（末） | 5,394,862 | 5,367,464 | 5,355,829 |

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
| **R23** | **4,337** | **+684,422** |

WAL 从 195 增至 4,337，说明 R22 到 R23 之间有中间会话产生了 WAL 但未完全清空。lastEventId 大幅增长 +684,422，说明有多次成功 flush。

### 2.3 Flush 事件分析

| Flush | 时间 | lastEventId | liveRecords | 位置 |
|-------|------|-------------|-------------|------|
| #1 | 17:04:05 | 148,434,750 | 4,492,047 | **benchmark 期间（L4980）** |

**再次发生 benchmark 期间 flush**（与 R22 相同问题）。Flush 恰好在 `qw` 查询序列中间：

| 查询 | Flush 前 | Flush 后 | 影响 |
|------|---------|---------|------|
| `qw` | 229-291ms | 188-**546ms** | flush 后波动剧增 |

Flush 后 `qw` 出现 429ms、546ms、354ms 高延迟，与 R22 的 826ms 模式一致，但严重程度略低。

### 2.4 Pre-benchmark GUI 查询

| 时间 | 查询 | 延迟 | 路径 | 结果 |
|------|------|------|------|------|
| 16:59:08 | `case:README` | **631ms** | adv-linear-gcd | 3 |
| 16:59:27 | `case:Hello ext:cpp` | **650ms** | adv-linear-gcd | 0 |

**新发现 P24 — Case-sensitive 查询走 linear scan**：`case:` 前缀查询完全跳过 trigram 索引（candidates=0），即使关键词足够长（6+ 字符）。trigram 索引存储小写归一化的 trigram，因此 case-sensitive 查询无法利用。但完全可以用 trigram 做候选集预过滤，再用 case-sensitive 做精确匹配。

### 2.5 totalRecords 增长分析

| 时间点 | totalRecords | 说明 |
|--------|-------------|------|
| benchmark 开始 (17:03:34) | 5,394,614 | — |
| benchmark 结束 (~17:04:20) | 5,394,862 | +248 |
| 增长速率 | ~5.4 条/秒 | 正常 FSEvents 速率 |

### 2.6 `test` 查询路径变化分析

R23 最重要的变化：`test` 查询从 `advanced-linear-gcd` 迁移到 `advanced-trigram`。

| 轮次 | `test` Median | 路径 | candidates |
|------|-------------|------|-----------|
| R21 | 214.3ms | adv-linear-gcd | 0 |
| R22 | 192.7ms | adv-linear-gcd | 0 |
| **R23** | **6.3ms** | **adv-trigram** | **92,046** |

日志 L4918 确认：`"test" total=120ms | path=advanced-trigram candidates=92046 results=100`（warmup 首查 120ms，后续稳定至 4.6-11.5ms）。92,046 候选在 5.39M 记录中仅占 1.7%，trigram 预过滤极为有效。

### 2.7 错误与异常

| 类型 | 数量 | 详情 |
|------|------|------|
| ERROR | 0 | 无 |
| WARN | 0 | 无 |
| 崩溃 | 0 | 无 |
| lock_wait > 0 | 0 | 无 |
| 极端延迟 | 2 | `case:README` 631ms、`qw` 547ms |
| Flush during benchmark | 1 | 17:04:05，影响 `qw` 查询 |

---

## 3. 修复验证

### 3.1 本轮修复确认

| 问题 | R22 表现 | R23 表现 | 状态 |
|------|---------|---------|------|
| **P0 trigram 降级** | `test` 193ms 走 linear | `test` **6.3ms** 走 trigram | **已修复** |
| **P18 `*.py`** | `*.py` 166ms 走 linear | `*.py` **16.5ms** 走 trigram | **已修复** |
| **P18 `*test*.cpp`** | `*test*.cpp` 226ms 走 linear | `*test*.cpp` **7.1ms** 走 trigram | **已修复** |

### 3.2 持续有效的修复

| 问题 | R23 表现 | 验证 |
|------|---------|------|
| P18 glob-trigram（部分） | `*.swift` 1.8ms、`*.cpp` 1.0ms、`*.json` 7.9ms | **持续有效** |
| P20 WAL 管理 | WAL 4,337（合理范围） | **持续有效** |

### 3.3 未修复问题

| 问题 | 等级 | R23 表现 | R22 对比 |
|------|------|---------|---------|
| **P22 CJK 短关键词** | **Medium** | `桌面` 181ms 走 linear，0 结果 | 225→181ms 略改善 |
| **P23 flush 冲击** | **Medium** | `qw` 547ms（flush 后） | 826→547ms 改善 |
| **P24 case-sensitive** | **High (新)** | `case:README` 631ms 走 linear | 新发现 |

### 3.4 新发现

**P24 — Case-sensitive 查询完全跳过 trigram**  
`case:README` 631ms、`case:Hello ext:cpp` 650ms，均走 `advanced-linear-gcd` 且 candidates=0。Case-sensitive 查询应先用 trigram（小写）做候选集预过滤，再做 case-sensitive 精确匹配，可将延迟从 631ms 降至约 10-20ms。

---

## 4. 优化建议

按优先级排序：

1. **Case-sensitive trigram 预过滤**（P24 High）— `case:README` 631ms，可用 trigram 候选 + case-sensitive post-filter 降至 <20ms
2. **Flush 期间查询保护**（P23 Medium）— 连续两轮 benchmark 期间发生 flush，考虑延迟 flush 到查询空闲期
3. **CJK 短关键词优化**（P22 Medium）— `桌面` 181ms 走 linear 且 0 结果，考虑 CJK bigram 索引
4. **`*.py` trigram 进一步优化**— 16.5ms 虽已大幅改善但仍高于其他 glob（`*.cpp` 1.0ms、`*.swift` 1.8ms），可能候选集较大

---

## 5. 总结

R23 是 **里程碑式的性能改善轮次**，P0 和 P18 的核心问题得到修复：

- 全局 avg median **48.2ms**（R22 61.8ms **-22%**，R21 50.4ms **-4%**）
- **P0 已修复**：`test` 从 192.7ms → **6.3ms**（30.6x 加速），走 trigram，candidates=92,046
- **P18 进一步修复**：`*.py` 166ms → **16.5ms**（10x），`*test*.cpp` 226ms → **7.1ms**（32x）
- medium_trigram 类别：25.3ms → **2.2ms**（**-91%**）
- glob_patterns 类别：58.1ms → **6.2ms**（**-89%**）
- 38/48 查询走 trigram（R22: 34/48），仅 10 查询走 linear
- advanced-trigram avg **7.6ms**（R22 6.6ms +15%，含新增高候选查询）
- advanced-linear-gcd avg **202.5ms**（R22 207.1ms -2%）— 稳定
- **P22 未修复**：`桌面` 181ms（R22 225ms 改善），0 结果
- **P23 再现**：flush 期间 `qw` 547ms（R22 826ms 改善）
- **P24 新发现**：`case:README` 631ms，case-sensitive 查询跳过 trigram
- WAL 4,337 条（R21/R22 195 条增长，中间会话产生）
- 启动时间 13.7s，无错误/警告/崩溃
- lock_wait=0ms
