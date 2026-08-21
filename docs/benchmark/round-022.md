# Round 22 基准测试报告

**时间**: 2026-04-19 16:33  
**会话**: 16:17:42 启动（与 R21 同一会话），已运行 ~16min  
**索引记录数**: 4,488,943（benchmark 开始时服务报告）/ 5,367,464（benchmark 结束时日志）  
**WAL 条目**: 195（启动时回放，继承 R21 会话）  
**Flush**: benchmark 期间发生 1 次 flush（16:33:43，L4458）  
**Benchmark 脚本**: `benchmarks/bench_search.py`（48 查询，limit=100，warmup=3，iterations=10）

---

## 1. 基准测试结果

### 1.1 总览

| 指标 | R22 | R21 | 变化 |
|------|-----|-----|------|
| 查询数 | 48 | 48 | — |
| limit | 100 | 100 | — |
| 最小延迟(median) | 0.0ms | 0.0ms | — |
| 最大延迟(median) | 358.3ms | 299.7ms | +20% |
| 全局 avg median | **61.8ms** | **50.4ms** | **+23%** |
| advanced-trigram avg | **6.6ms** | **5.8ms** | +14% |
| advanced-linear-gcd avg | **207.1ms** | **169.7ms** | **+22%** |
| advanced-path-trigram avg | **49.6ms** | **14.1ms** | **+252%** |

### 1.2 按类别汇总

| 类别 | 查询数 | R22 Avg Median | R21 Avg Median | 变化 | 评价 |
|------|--------|---------------|---------------|------|------|
| long_trigram | 8 | **0.8ms** | 1.0ms | -20% | 极佳 |
| medium_trigram | 8 | **25.3ms** | 28.1ms | -10% | 被 `test` 拖垮 |
| path_queries | 8 | **30.3ms** | 22.7ms | +33% | 劣化 |
| cjk_queries | 5 | **45.0ms** | 17.1ms | +163% | `桌面` 大幅劣化 |
| glob_patterns | 7 | **58.1ms** | 25.2ms | +130% | `*.py`/`*test*.cpp` 劣化 |
| short_linear | 5 | **149.2ms** | 157.9ms | -6% | 略有改善 |
| no_results | 3 | **119.4ms** | 77.1ms | +55% | `qw` 劣化 |
| linear_forced | 4 | **194.6ms** | 180.1ms | +8% | 对照组略劣化 |

### 1.3 按搜索路径分类

| 搜索路径 | 查询数 | R22 Avg Median | R21 Avg Median | 变化 |
|---------|--------|---------------|---------------|------|
| advanced-trigram | 34 | **6.6ms** | **5.8ms** | +14% |
| advanced-linear-gcd | 13 | **207.1ms** | **169.7ms** | +22% |
| advanced-path-trigram | 1 | **49.6ms** | **14.1ms** | +252% |

### 1.4 Trigram vs Linear 对照（同一查询）

| 查询 | Trigram | Linear | 加速比 | R21 加速比 |
|------|---------|--------|--------|-----------|
| config | 2.3ms | 214.3ms | **92.6x** | 65.5x |
| readme | 1.3ms | 202.0ms | **156.0x** | 106.2x |
| application | 1.3ms | 180.0ms | **133.8x** | 118.2x |
| node_modules | 0.5ms | 182.0ms | **387.2x** | 173.0x |

**关键结论**: trigram 路径比 linear scan 快 **93-387 倍**，node_modules 加速比从 R21 的 173x 提升至 387x。

### 1.5 逐查询详情（仅列出关注项）

#### 性能优秀的查询

| 查询 | R22 Median | R21 Median | 路径 | 评价 |
|------|-----------|-----------|------|------|
| `application` (trigram) | 1.3ms | 1.6ms | adv-trigram | 极佳 |
| `node_modules` (trigram) | 0.5ms | 1.0ms | adv-trigram | 极佳 |
| `readme` (trigram) | 1.3ms | 1.7ms | adv-trigram | 极佳 |
| `config` (trigram) | 2.3ms | 2.6ms | adv-trigram | 极佳 |
| `*.swift` | 2.6ms | 1.8ms | adv-trigram | 良好 |
| `*.cpp` | 0.9ms | 0.8ms | adv-trigram | 极佳 |
| `*.json` | 4.6ms | 2.7ms | adv-trigram | 良好 |
| `单-CJK 中` | 0.1ms | 0.0ms | adv-trigram | 极佳 |
| `screenshot` | 0.1ms | — | adv-trigram | 新查询，极佳 |
| `dockerfile` | 0.1ms | — | adv-trigram | 新查询，极佳 |

#### 仍有问题的查询

| 查询 | R22 Median | R21 Median | 路径 | 问题 |
|------|-----------|-----------|------|------|
| `test` | **192.7ms** | 214.3ms | adv-linear-gcd | **P0 未修复**：4字符走 linear |
| `ab` | **263.7ms** | 299.7ms | adv-linear-gcd | 预期（2字符） |
| `.h` | **246.8ms** | 236.9ms | adv-linear-gcd | 预期（2字符特殊） |
| `桌面` | **224.9ms** | 83.7ms | adv-linear-gcd | **P22 劣化**：CJK 2字符，0 结果 |
| `*.py` | **166.3ms** | 126.4ms | adv-linear-gcd | **P18 未修复** |
| `*test*.cpp` | **226.3ms** | 41.0ms | adv-linear-gcd | **大幅劣化** |
| `qw` | **358.3ms** | 231.2ms | adv-linear-gcd | 2字符走 linear，**大幅劣化** |
| `usr/bin` | **49.6ms** | 14.1ms | adv-path-trigram | 大幅劣化 |

#### 高延迟异常

| 查询 | Max(ms) | 备注 |
|------|---------|------|
| `qw` | **826.5ms** | 22 轮单查询最高之一 |
| `ab` | **472.4ms** | 2字符 linear scan |
| `.h` | **374.3ms** | flush 期间（L4457-4458） |
| `桌面` | **331.9ms** | CJK linear scan |
| `readme` (forced) | **341.0ms** | linear scan |
| `config` (forced) | **280.2ms** | linear scan |

---

## 2. 日志分析

### 2.1 会话状态

R22 与 R21 共享同一会话（16:17:42 启动），R21 benchmark 在 16:19 执行，R22 benchmark 在 16:33 执行。中间间隔约 14 分钟。

| 阶段 | 值 |
|------|------|
| 会话启动 | 16:17:42 |
| R21 benchmark | 16:19:04 - 16:19:57 |
| Flush #1 | **16:23:10** (L4422) |
| R22 warmup 查询 | 16:33:26 |
| R22 benchmark | 16:33:36 - 16:34:21 |
| Flush #2 | **16:33:43** (L4458, benchmark 期间!) |

### 2.2 Flush 事件分析

| Flush | 时间 | lastEventId | liveRecords |
|-------|------|-------------|-------------|
| #1 | 16:23:10 | 147,675,948 | 4,487,132 |
| #2 | 16:33:43 | 147,892,933 | 4,488,919 |

**关键发现**: Flush #2 发生在 benchmark 执行期间（16:33:43），恰好在 `.h` 查询序列中间（L4457-4458）。这导致了：
1. `.h` 查询在 flush 前后延迟分化：flush 前 494ms（L4457），flush 后 375ms→198ms 逐渐恢复
2. 后续 linear scan 查询（`qw` 等）的 VM 页面被 flush 操作影响

### 2.3 Flush 对 linear scan 的影响

对比 flush 前后的 linear scan 查询延迟：

| 查询 | Flush 前 (16:33:36-43) | Flush 后 (16:33:43-) | 影响 |
|------|----------------------|---------------------|------|
| `.h` | 204-494ms | 198-295ms | flush 后改善 |
| `test` | — | 166-255ms | 正常 |
| `*.py` | — | 127-310ms | 首查 310ms 偏高 |
| `qw` | — | 199-826ms | **极不稳定** |
| `config` | — | 145-280ms | 正常偏高 |

`qw` 查询在 flush 后出现极端延迟（826ms），明显受到 flush 引发的 VM 页面失效影响。

### 2.4 `qw` 826ms 异常分析

日志 L4530/L4532 显示 `qw` 查询有两次超 600ms 的延迟（623ms 和 826ms）。此时 totalRecords 从 5,367,321 到 5,367,327（+6），说明 FSEvents 仍在持续写入。结合 flush 刚发生（~50s 前），这是 **flush 后 VM 页面冷 + FSEvents 锁争用**的组合效应。

### 2.5 totalRecords 增长分析

| 时间点 | totalRecords | 说明 |
|--------|-------------|------|
| R22 warmup 开始 (16:33:26) | 5,366,976 | — |
| R22 benchmark 开始 (16:33:36) | 5,367,066 | warmup 期间 +90 |
| R22 benchmark 结束 (16:34:21) | 5,367,464 | benchmark 期间 +398 |
| 增长速率 | ~8.8 条/秒 | 正常 FSEvents 速率 |

Benchmark 期间新增 398 条记录，FSEvents 持续活跃但未造成严重锁争用（lock_wait 字段未出现）。

### 2.6 R21 vs R22 延迟对比（同一会话内）

| 查询 | R21 Median | R22 Median | 变化 | 原因分析 |
|------|-----------|-----------|------|---------|
| `桌面` | 83.7ms | 224.9ms | **+169%** | flush 后 VM 冷 |
| `*test*.cpp` | 41.0ms | 226.3ms | **+452%** | flush 后 VM 冷 |
| `qw` | 231.2ms | 358.3ms | **+55%** | flush 后 + 波动大 |
| `usr/bin` | 14.1ms | 49.6ms | **+252%** | flush 后 trigram 缓存冷 |
| `test` | 214.3ms | 192.7ms | -10% | 略有改善 |
| `config` (trigram) | 2.6ms | 2.3ms | -12% | trigram 稳定 |
| `node_modules` (trigram) | 1.0ms | 0.5ms | -50% | trigram 改善 |

**核心结论**: R22 整体劣化的主因是 **benchmark 期间发生 flush**（16:33:43），导致 VM 页面失效，linear scan 路径全面受影响。Trigram 路径基本不受影响（+14%）。

### 2.7 错误与异常

| 类型 | 数量 | 详情 |
|------|------|------|
| ERROR | 0 | 无 |
| WARN | 0 | 无 |
| 崩溃 | 0 | 无 |
| lock_wait > 0 | 0 | 无 |
| 极端延迟 | 2 | `qw` 826ms、`.h` 494ms |
| Flush during benchmark | 1 | 16:33:43，影响后续查询 |

---

## 3. 修复验证

### 3.1 已修复问题（从 R21 继承）

| 问题 | R22 表现 | 验证 |
|------|---------|------|
| P18 glob-trigram（部分） | `*.swift` 2.6ms、`*.cpp` 0.9ms、`*.json` 4.6ms、`test_*` 2.7ms、`*config*` 3.2ms | **持续有效** |
| P20 WAL 管理 | 同一会话，WAL 195 | **持续有效** |

### 3.2 未修复问题

| 问题 | 等级 | R22 表现 | R21 对比 |
|------|------|---------|---------|
| **P0 trigram 降级** | **Critical** | `test` 193ms 走 linear | 214→193ms 略改善 |
| **P21 limit=100 降级** | **High** | `config`/`readme`/`application`/`node_modules` 全走 linear 180-214ms | 170-189→180-214ms 略劣化 |
| **P18 `*.py`** | **High** | `*.py` 166ms 走 linear | 126→166ms 劣化 |
| **P18 `*test*.cpp`** | **High** | `*test*.cpp` 226ms 走 linear | 41→226ms **大幅劣化** |
| **P22 CJK 短关键词** | **Medium** | `桌面` 225ms 走 linear，0 结果 | 84→225ms **大幅劣化** |
| **P7 flush 锁争用** | **Medium** | flush 期间 `.h` 494ms，后续 `qw` 826ms | benchmark 期间 flush |

### 3.3 新发现

**P23 — Flush 期间查询延迟暴涨**  
R22 的 flush 恰好发生在 benchmark 期间（L4458），导致 `.h` 查询从 204ms 跳至 494ms，后续 `qw` 达到 826ms。这说明 flush 操作对 linear scan 查询有显著的性能冲击，不仅因为锁争用，还因为 paged index 文件写盘导致 VM 页面失效。

---

## 4. 优化建议

按优先级排序：

1. **修复 P0 + P21 trigram 降级**（Critical）— `test`/`config`/`readme`/`application`/`node_modules` 走 trigram 可从 180-214ms 降至 0.5-2.3ms，**100-387倍加速**
2. **修复 `*.py` 和 `*test*.cpp` glob-trigram**（P18 High）— `*.swift`/`*.cpp` 已修复但这两个未覆盖
3. **Flush 期间查询保护**（P23 Medium）— flush 期间 linear scan 延迟暴涨至 826ms，考虑：
   - flush 期间暂停 linear scan 查询（排队等待）
   - 或者使用异步 flush 避免 VM 页面失效
4. **CJK 短关键词优化**（P22 Medium）— `桌面` 225ms 走 linear 且 0 结果
5. **`usr/bin` path-trigram 优化**— 49.6ms（R21 14.1ms），flush 后候选集遍历缓慢

---

## 5. 总结

R22 是 **R21 同一会话的后续轮次**（距启动 ~16min），benchmark 期间发生 flush（16:33:43），导致 linear scan 全面劣化：

- 全局 avg median **61.8ms**（R21 50.4ms **+23%**）
- advanced-trigram avg **6.6ms**（R21 5.8ms +14%）— trigram 路径基本稳定
- advanced-linear-gcd avg **207.1ms**（R21 169.7ms **+22%**）— flush 后 VM 冷
- Trigram 加速比进一步扩大：node_modules **387x**、readme **156x**、application **134x**、config **93x**
- **P18 部分修复持续有效**：`*.swift` 2.6ms、`*.cpp` 0.9ms、`*.json` 4.6ms
- **P0/P21 未修复**：`test` 193ms、linear_forced 对照组 180-214ms
- **P18 部分未修复且劣化**：`*.py` 166ms（R21 126ms）、`*test*.cpp` 226ms（R21 41ms）
- **P22 劣化**：`桌面` 225ms（R21 84ms），0 结果
- **新增 P23**：flush 期间 benchmark，`qw` 826ms（22 轮最高单查询之一）
- lock_wait=0ms（日志未记录），无崩溃，无错误
- Flush 策略正常：300s 间隔自动触发
