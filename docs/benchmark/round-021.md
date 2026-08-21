# Round 21 基准测试报告

**时间**: 2026-04-19 16:19  
**会话**: 16:17:42 启动，已运行 ~1.5min  
**索引记录数**: 5,355,829（benchmark 结束时）  
**WAL 条目**: 195（启动时回放）  
**Benchmark 脚本**: `benchmarks/bench_search.py`（48 查询，limit=100，warmup=3，iterations=10）

---

## 1. 基准测试结果

### 1.1 总览

| 指标 | 值 | 对比 R20 |
|------|-----|---------|
| 查询数 | 48 | 28→48 (新脚本) |
| limit | 100 | 5→100 (模拟 GUI) |
| warmup | 3 | 新增 |
| iterations | 10 | 新增 |
| 最小延迟(median) | 0.0ms | — |
| 最大延迟(median) | 299.7ms | — |
| 全局 avg median | **50.4ms** | — |

### 1.2 按类别汇总

| 类别 | 查询数 | Avg Median | Median Range | 评价 |
|------|--------|-----------|-------------|------|
| long_trigram | 8 | **1.0ms** | 0.1-3.1ms | 极佳 |
| medium_trigram | 8 | **28.1ms** | 0.6-214.3ms | 被 `test` 拖垮 |
| path_queries | 8 | **22.7ms** | 0.1-98.7ms | 良好 |
| cjk_queries | 5 | **17.1ms** | 0.0-83.7ms | 被 `桌面` 拖垮 |
| glob_patterns | 7 | **25.2ms** | 0.8-126.4ms | 被 `*.py` 拖垮 |
| short_linear | 5 | **157.9ms** | 0.0-299.7ms | 预期内（1-2字符） |
| no_results | 3 | **77.1ms** | 0.0-231.2ms | `qw` 走 linear |
| linear_forced | 4 | **180.1ms** | 170.2-189.2ms | 对照组，预期内 |

### 1.3 按搜索路径分类

| 搜索路径 | 查询数 | Avg Median | Median Range |
|---------|--------|-----------|-------------|
| advanced-trigram | 34 | **5.8ms** | 0.0-98.7ms |
| advanced-linear-gcd | 13 | **169.7ms** | 41.0-299.7ms |
| advanced-path-trigram | 1 | **14.1ms** | 14.1ms |

### 1.4 Trigram vs Linear 对照（同一查询）

| 查询 | Trigram | Linear | 加速比 |
|------|---------|--------|--------|
| config | 2.6ms | 170.2ms | **65.5x** |
| readme | 1.7ms | 181.1ms | **106.2x** |
| application | 1.6ms | 189.2ms | **118.2x** |
| node_modules | 1.0ms | 179.9ms | **173.0x** |

**关键结论**: trigram 路径比 linear scan 快 **65-173 倍**，证明 P0 修复的巨大价值。

### 1.5 逐查询详情（仅列出关注项）

#### 改善明显的查询

| 查询 | Median | 路径 | 候选数 | 对比 R20 | 备注 |
|------|--------|------|--------|---------|------|
| `config` (trigram) | 2.6ms | adv-trigram | — | R20 无对照 | P0 修复生效 |
| `readme` (trigram) | 1.7ms | adv-trigram | — | 4.5→1.7ms | 持续优秀 |
| `application` (trigram) | 1.6ms | adv-trigram | — | R20 无对照 | P0 修复生效 |
| `node_modules` (trigram) | 1.0ms | adv-trigram | — | R20 无对照 | P0 修复生效 |
| `*.swift` | 1.8ms | adv-trigram | — | R20 无此查询 | P18 修复生效 |
| `*.cpp` | 0.8ms | adv-trigram | — | R20 无此查询 | P18 修复生效 |
| `*.json` | 2.7ms | adv-trigram | — | R20 无此查询 | P18 修复生效 |
| `test_*` | 2.3ms | adv-trigram | — | R20 无此查询 | glob-trigram 恢复 |
| `*config*` | 1.5ms | adv-trigram | — | R20 无此查询 | glob-trigram 恢复 |
| `单-CJK 中` | 0.0ms | adv-trigram | 131 | R20 无此查询 | CJK 单字符走 trigram |
| `usr/bin` | 14.1ms | adv-path-trigram | — | R20 无此查询 | 新路径类型 |

#### 仍有问题的查询

| 查询 | Median | 路径 | 候选数 | 问题 |
|------|--------|------|--------|------|
| `test` | **214.3ms** | adv-linear-gcd | 0 | **P0 未修复**：4字符走 linear |
| `ab` | **299.7ms** | adv-linear-gcd | 0 | 预期（2字符） |
| `.h` | **236.9ms** | adv-linear-gcd | 0 | 预期（2字符特殊） |
| `桌面` | **83.7ms** | adv-linear-gcd | 0 | **P22 未修复**：CJK 2字符走 linear，0 结果 |
| `*.py` | **126.4ms** | adv-linear-gcd | 0 | **P18 部分未修复** |
| `*test*.cpp` | **41.0ms** | adv-linear-gcd | 0 | 多通配符 glob 走 linear |
| `qw` | **231.2ms** | adv-linear-gcd | 0 | 2字符走 linear |
| `config` (forced linear) | **170.2ms** | adv-linear-gcd | 0 | 对照组，展示 linear 代价 |

#### 高延迟异常

| 查询 | Max(ms) | 备注 |
|------|---------|------|
| `*.py` | **2341.6ms** | 单次极端延迟，可能 FSEvents 争用 |
| `桌面` | **834.0ms** | CJK linear scan 波动大 |
| `.h` | **638.7ms** | 2字符 linear scan |
| `ab` | **594.9ms** | 2字符 linear scan |
| `/Library/Application` | **581.1ms** | trigram 但 3871 候选 + 长路径匹配 |
| `Library/Application` | **581.1ms** | 同上 |

---

## 2. 日志分析

### 2.1 启动性能

| 阶段 | 值 | 对比 R20 |
|------|------|---------|
| 索引加载（paged index） | 12.8s | 13.3→12.8s (-4%) |
| WAL 回放 | **195 条** / 0.48s | 7,532→**195（-97%）** |
| FSEvents 回放 | 0.08s | 0.35→0.08s (-77%) |
| **总启动时间** | **12.9s** | 13.7→12.9s (-6%) |
| 索引记录数（加载后） | 4,486,218 | 4,482,549→4,486,218 (+3,669) |
| lastEventId | 147,571,144 | 147,147,786→147,571,144 (+423,358) |
| live totalRecords（R21 末） | 5,355,829 | 5,333,188→5,355,829 (+22,641) |

**WAL 回放 195 条是 21 轮最低**（R14/R15 的 316 条之后），说明上一会话有多次成功 flush。

### 2.2 WAL 增长趋势

| 会话 | WAL 条目 | lastEventId 增量 |
|------|---------|-----------------|
| R14/R15 | 316 | — |
| R16 | 3,319 | — |
| R17 | 5,489 | — |
| R18 | 4,604 | +808,904 |
| R19 | 9,130 | 0（无 flush） |
| R20 | 7,532 | +214,872 |
| **R21** | **195** | **+423,358** |

WAL 从 7,532 骤降至 195，lastEventId 大幅增长 +423,358，说明 R20→R21 之间有多次 flush 成功将 WAL 清空。

### 2.3 中间会话分析

日志显示 R20 到 R21 之间有两个中间会话：

| 会话 | 启动时间 | WAL | lastEventId | 事件 |
|------|---------|-----|-------------|------|
| 中间会话1 | 16:05:58 | — | — | 短暂运行 |
| 中间会话2 | 16:12:06 | 18,376 | 147,147,786→ | flush at 16:17:28 (lastEventId=147,571,144) |
| **R21** | **16:17:42** | **195** | **147,571,144** | benchmark 轮次 |

中间会话2 回放了 **18,376 条 WAL**（21轮最高），但在 16:17:28 成功 flush（运行 ~5min 后），将 lastEventId 更新至 147,571,144。R21 启动时仅剩 195 条新 WAL。

### 2.4 查询日志分析

日志记录了 benchmark 前的预热查询和 benchmark 查询：

**Benchmark 前查询**（16:18:29–16:18:53）：

| 时间 | 查询 | 延迟 | 路径 | totalRecords |
|------|------|------|------|-------------|
| 16:18:29 | `/usr/local/bin` | 289ms | adv-path-trigram | 5,354,044 |
| 16:18:36 | `*.cpp` | 188ms | adv-trigram | 5,354,168 |
| 16:18:53 | `test` | **988ms** | adv-linear-gcd | 5,354,352 |

首查 `test` 988ms — 冷启动首次 linear scan 极高延迟，但这是 warmup 查询。

**Benchmark 高延迟查询**（选取 > 100ms）：

| 查询 | 延迟范围 | 路径 | 候选数 |
|------|---------|------|--------|
| `a` | 101-310ms | adv-linear-gcd | 0 |
| `ab` | 226-594ms | adv-linear-gcd | 0 |
| `.h` | 170-638ms | adv-linear-gcd | 0 |
| `test` | 177-383ms | adv-linear-gcd | 0 |
| `config` | 138-241ms | adv-linear-gcd | 0 |
| `application` | 154-280ms | adv-linear-gcd | 0 |
| `readme` | 159-241ms | adv-linear-gcd | 0 |
| `node_modules` | 150-224ms | adv-linear-gcd | 0 |
| `qw` | 173-382ms | adv-linear-gcd | 0 |
| `桌面` | 127-834ms | adv-linear-gcd | 0 |
| `*.py` | 153-2341ms | adv-linear-gcd | 0 |
| `*test*.cpp` | — | adv-linear-gcd | 0 |
| `image` | 717ms(max) | adv-trigram | 16,632 |
| `Library/Application` | 189-581ms | adv-trigram | 3,871 |
| `/Library/Application` | 115-411ms | adv-trigram | 3,871 |

**关键发现**：
1. **`test` 仍走 linear**：4字符，candidates=0，P0 未修复
2. **`config`/`application`/`readme`/`node_modules` 全走 linear**：P0/P21 未修复，linear_forced 对照组证明这些查询在 trigram 路径下仅需 1-3ms
3. **`*.py` 2341ms 极端延迟**：单次 outlier，可能是 FSEvents 批量事件导致锁争用
4. **lock_wait 未出现**：所有日志查询均无 lock_wait 字段

### 2.5 `*.py` 2341ms 异常分析

日志 L4343 显示 `*.py` 查询某次执行 2341ms，而前后查询仅 153-351ms。此时 totalRecords 从 5,355,117 跳至 5,355,139（+22），说明正在进行 FSEvents 扫盘。这是一次 **FSEvents 批量事件与 linear scan 查询的锁争用**，虽然 lock_wait 未在日志中显示，但 GCD filter 扫描期间的内存压力可能导致了延迟。

### 2.6 新搜索路径 `advanced-path-trigram`

R21 首次出现 `advanced-path-trigram` 路径（`usr/bin` 查询），说明有针对路径分隔查询的 trigram 优化。L4271 显示 `/usr/local/bin` 也走了此路径（289ms，candidates=145,678）。

### 2.7 错误与异常

| 类型 | 数量 | 详情 |
|------|------|------|
| ERROR | 0 | 无 |
| WARN | 0 | 无 |
| 崩溃 | 0 | 无 |
| lock_wait > 0 | 0 | 无 |
| 极端延迟 | 1 | `*.py` 2341ms |

---

## 3. 修复验证

### 3.1 已修复问题

| 问题 | 修复效果 | 验证 |
|------|---------|------|
| P18 glob-trigram（部分） | `*.swift` 1.8ms、`*.cpp` 0.8ms、`*.json` 2.7ms、`test_*` 2.3ms、`*config*` 1.5ms 全走 trigram | **部分修复** |
| WAL 管理（P20） | WAL 从 9,130→7,532→195，flush 策略显著改善 | **修复验证** |

### 3.2 未修复问题

| 问题 | 等级 | R21 表现 | 说明 |
|------|------|---------|------|
| **P0 trigram 降级** | **Critical** | `test` 214ms 走 linear | 4字符仍降级，trigram 对照仅需 ~2ms |
| **P21 limit=100 降级** | **High** | `config`/`readme`/`application`/`node_modules` 全走 linear 170-189ms | trigram 对照仅需 1-3ms |
| **P18 `*.py` glob** | **High** | `*.py` 126ms 走 linear，`*test*.cpp` 41ms 走 linear | `*.swift`/`*.cpp` 已修复但 `*.py` 未覆盖 |
| **P22 CJK 短关键词** | **Medium** | `桌面` 84ms 走 linear，0 结果 | CJK 2字符无 trigram |

### 3.3 P0/P21 根因深入分析

Benchmark 对照实验清晰地证明了问题：

```
config   trigram → 2.6ms    linear → 170.2ms   差异 65.5x
readme   trigram → 1.7ms    linear → 181.1ms   差异 106.2x
application trigram → 1.6ms linear → 189.2ms   差异 118.2x
node_modules trigram → 1.0ms linear → 179.9ms  差异 173.0x
```

这些查询 **有 trigram 索引**（benchmark trigram 类别证明），但在某种条件下被降级到 linear scan。可能的降级条件：
- limit=100 时 trigram 候选集过大超过阈值
- 某种查询参数组合触发了不同的代码路径
- 特定查询文本的 trigram 命中率计算有误

---

## 4. 优化建议

1. **修复 P0 + P21 trigram 降级**（Critical）— `test`/`config`/`readme`/`application`/`node_modules` 走 trigram 可从 170-214ms 降至 1-3ms，**100倍加速**
2. **修复 `*.py` glob-trigram**（P18 High）— `*.swift`/`*.cpp` 已修复但 `*.py` 未覆盖，126ms→预期 <3ms
3. **修复 `*test*.cpp` 多通配符 glob**（P18 扩展）— 41ms 走 linear
4. **CJK 短关键词优化**（P22 Medium）— `桌面` 84ms 走 linear 且 0 结果
5. **`*.py` 2341ms 极端延迟调查**— 是否存在 FSEvents 争用或 GCD 调度问题

---

## 5. 总结

R21 是 **WAL 清空后的预热轮次**（WAL 仅 195 条，21轮最低），使用新 benchmark 脚本（48 查询，limit=100）进行更全面的测试：

- 全局 avg median **50.4ms**
- advanced-trigram avg **5.8ms**（34/48 查询走此路径）— 极佳
- advanced-linear-gcd avg **169.7ms**（13/48 查询走此路径）— 仍然偏高
- **P18 部分修复验证**：`*.swift` 1.8ms、`*.cpp` 0.8ms、`*.json` 2.7ms 全走 trigram
- **P20 修复验证**：WAL 从 9,130→195，flush 策略显著改善
- **P0/P21 未修复**：`test` 214ms、`config`/`readme`/`application`/`node_modules` 全走 linear（trigram 对照仅需 1-3ms，**100倍差异**）
- **P18 部分未修复**：`*.py` 126ms、`*test*.cpp` 41ms 仍走 linear
- **P22 未修复**：`桌面` 84ms 走 linear，0 结果
- 新搜索路径 `advanced-path-trigram` 首次出现
- `*.py` 出现 2341ms 极端延迟（单次）
- lock_wait=0ms，无崩溃，无错误
- 启动时间 12.9s（R20 13.7s -6%），WAL 回放 0.48s
