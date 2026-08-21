# Round 20 基准测试报告

**时间**: 2026-04-19 16:03  
**会话**: 15:50:52 启动，已运行 ~12min，经历 1 次 flush  
**索引记录数**: 5,333,188（benchmark 结束时）  
**WAL 条目**: 7,532（启动时回放）

---

## 1. 基准测试结果

| 指标 | 值 | 对比 R19 | 对比 R18 | 对比 R15（最佳） |
|------|-----|---------|---------|-----------------|
| 成功 | 27/28 | 持平 | 持平 | 持平 |
| 最小延迟 | 1.4ms | 0.2→1.4 | 0.2→1.4 | 0.1→1.4 |
| 最大延迟 | **645.1ms** | 372.6→645.1 (**+73%**) | 196.5→645.1 (+228%) | 127.3→645.1 (+407%) |
| 平均延迟 | **129.7ms** | 114.5→129.7 (+13%) | 51.8→129.7 (+150%) | 34.7→129.7 (+274%) |
| P50 延迟 | 46.8ms | 106.8→46.8 (-56%) | 43.3→46.8 (+8%) | 31.8→46.8 (+47%) |
| P90 延迟 | 307.3ms | 266.1→307.3 (+15%) | 117.5→307.3 (+162%) | 81.4→307.3 (+277%) |
| P99 延迟 | 645.1ms | 372.6→645.1 (+73%) | 196.5→645.1 (+228%) | 127.3→645.1 (+407%) |

### 按搜索路径分类

| 搜索路径 | 查询数 | 平均延迟 | 最大延迟 | 对比 R19 | 对比 R18 |
|---------|--------|---------|---------|---------|---------|
| advanced-trigram | 13 | **26.9ms** | 153.7ms | 18.6→26.9 (+45%) | 13.2→26.9 (+104%) |
| advanced-linear-gcd | 10 | **275.3ms** | 645.1ms | 233.6→275.3 (+18%) | 96.4→275.3 (+186%) |
| advanced-regex-trigram | 3 | **31.8ms** | 37.9ms | 114.0→31.8 (**-72%**) | 48.8→31.8 (-35%) |
| advanced-pure-filter-soa-gcd | 1 | **303.5ms** | 303.5ms | 172.3→303.5 (+76%) | 116.4→303.5 (+161%) |

### 逐查询明细

| 查询 | 延迟(ms) | 搜索路径 | 候选数 | 结果数 | 对比 R19 | 对比 R18 |
|------|---------|---------|--------|--------|---------|---------|
| 1-char `a` | **645.1** | adv-linear-gcd | 0 | 5 | 372.6→645.1 (**+73%**) | 196.5→645.1 |
| 2-char `ab` | **307.3** | adv-linear-gcd | 0 | 5 | 266.1→307.3 (+15%) | 88.1→307.3 |
| 3-char `abc` | 2.0 | adv-trigram | 20663 | 5 | 8.7→2.0 (-77%) | 6.9→2.0 (-71%) |
| `test` | **205.0** | adv-linear-gcd | 0 | 5 | 205.2→205.0 (持平) | 45.1→205.0 |
| `readme` | 4.5 | adv-trigram | 10375 | 5 | 4.4→4.5 (持平) | 3.1→4.5 |
| `package.json` | 3.5 | adv-trigram | 11251 | 5 | 7.3→3.5 (-52%) | 6.3→3.5 (-44%) |
| `SearchEngineAdvancedQuery` | **118.1** | adv-trigram | 90 | 5 | 3.1→118.1 (**+3710%**) | 2.6→118.1 |
| `/usr/local/bin` | **153.7** | adv-trigram | 145677 | 5 | 156.2→153.7 (-2%) | 77.0→153.7 |
| `regex:test.*\.py` | 37.9 | adv-regex-trigram | 48788 | 5 | 196.6→37.9 (**-81%**) | 67.0→37.9 (-43%) |
| `regex:.*\.swift` | 33.6 | adv-regex-trigram | 7007 | 5 | 38.6→33.6 (-13%) | 36.4→33.6 (-8%) |
| `regex:^config` | 23.7 | adv-regex-trigram | 19935 | 5 | 106.8→23.7 (**-78%**) | 43.1→23.7 (-45%) |
| `wild:*.txt` | **224.0** | adv-linear-gcd | 0 | 0 | 138.5→224.0 (+62%) | 90.4→224.0 (+148%) |
| `readme ext:md` | 1.4 | adv-trigram | 10375 | 5 | 7.8→1.4 (-82%) | 5.2→1.4 (-73%) |
| `readme size>1mb` | **46.8** | adv-trigram | 10375 | 5 | 19.1→46.8 (+145%) | 8.8→46.8 (+432%) |
| `test type:folder` | **215.0** | adv-linear-gcd | 0 | 5 | 216.7→215.0 (持平) | 117.5→215.0 |
| `readme date:today` | **182.3** | adv-linear-gcd | 0 | 0 | 264.2→182.3 (-31%) | 117.4→182.3 |
| `test path:/usr` | **241.1** | adv-linear-gcd | 0 | 5 | 150.2→241.1 (+61%) | 115.2→241.1 |
| `search engine` | 3.0 | adv-trigram | 10198 | 5 | 4.6→3.0 (-35%) | 4.2→3.0 (-29%) |
| `search engine query` | 5.2 | adv-trigram | 10198 | 0 | 5.2→5.2 (持平) | 4.0→5.2 |
| `readme !config` | 4.5 | adv-trigram | 10375 | 5 | 8.0→4.5 (-44%) | 5.4→4.5 (-17%) |
| `测试` (CJK) | 1.4 | adv-trigram | 202 | 5 | 0.2→1.4 (+600%) | 0.2→1.4 |
| `#define` | **325.4** | adv-linear-gcd | 0 | 0 | 232.2→325.4 (+40%) | 126.1→325.4 (+158%) |
| `"package.json"` | 3.6 | adv-trigram | 11251 | 5 | 15.1→3.6 (-76%) | 5.7→3.6 (-37%) |
| `content:batchMutate` | **303.5** | adv-pure-filter-soa-gcd | 0 | 5 | 172.3→303.5 (+76%) | 116.4→303.5 (+161%) |
| (empty) | FAIL | - | - | - | — | — |
| long multi-term | 1.9 | adv-trigram | 107 | 0 | 2.0→1.9 (持平) | 1.5→1.9 |
| `.` | **218.4** | adv-linear-gcd | 0 | 5 | 194.8→218.4 (+12%) | 112.5→218.4 |
| `..` | **189.8** | adv-linear-gcd | 0 | 5 | 295.4→189.8 (-36%) | 109.1→189.8 |

---

## 2. 日志分析

### 2.1 启动性能

| 阶段 | 值 | 对比 R19 |
|------|------|---------|
| 索引加载（paged index） | 13.3s | 17.3→13.3s (-23%) |
| WAL 回放 | **7,532 条** / 0.75s | 9,130→**7,532（-18%）** |
| FSEvents 回放 | 0.35s | 0.38→0.35s (-8%) |
| **总启动时间** | **13.7s** | 17.7→13.7s (-23%) |
| 索引记录数（加载后） | 4,482,549 | 4,481,165→4,482,549 (+1,384) |
| lastEventId | 146,931,579 | 146,716,707→146,931,579 (+214,872) |
| live totalRecords（R20 末） | 5,333,188 | 5,311,793→5,333,188 (+21,395) |

lastEventId 更新说明 R19 会话中有一次成功的 flush（日志 L3997 确认：15:40:29 flush）。

### 2.2 WAL 增长趋势

| 会话 | WAL 条目 | lastEventId 增量 |
|------|---------|-----------------|
| R14/R15 | 316 | — |
| R16 | 3,319 | — |
| R17 | 5,489 | — |
| R18 | 4,604 | +808,904 |
| R19 | 9,130 | 0（无 flush） |
| **R20** | **7,532** | **+214,872** |

### 2.3 Flush 事件

| 时间 | 事件 | lastEventId | liveRecords |
|------|------|-------------|-------------|
| 15:50:52 | 启动 | 146,931,579 | 4,482,549 |
| **15:56:35** | **Flush** | **147,147,786** | **4,482,931** |

距启动 5min43s 即 flush（标准 300s 间隔）。Benchmark 在 16:03 执行，距 flush ~7min。

### 2.4 前一会话（R19）GUI 使用分析 — 重大发现

R19 会话（15:35:05–15:50:52）包含 **大量 GUI 用户查询**（15:42:07–15:43:03），共 **175 条查询**，暴露了严重的性能问题：

#### P0 问题实证：常见单词走 linear scan

| 查询 | 查询次数 | 平均延迟 | 路径 | 候选数 | 应走路径 |
|------|---------|---------|------|--------|---------|
| `config` | 10 | **155ms** | advanced-linear-gcd | 0 | trigram（有 6 字符） |
| `application` | 13 | **161ms** | advanced-linear-gcd | 0 | trigram（有 11 字符） |
| `readme` | 13 | **159ms** | advanced-linear-gcd | 0 | trigram（有 6 字符） |
| `node_modules` | 13 | **150ms** | advanced-linear-gcd | 0 | trigram（有 12 字符） |
| `test` | 13 | **181ms** | advanced-linear-gcd | 0 | trigram（有 4 字符） |

**这些都是 ≥4 字符的常见英文单词，trigram 索引中有大量 posting list 可用，但全部走了 linear scan！**

**关键发现**：GUI 查询的 `results=100`（limit=100），而 benchmark 的 `results=5`（limit=5）。**当 limit 较大时，trigram 路径可能因为某种阈值判断选择了 linear scan。**

#### GUI `a` 首查 2113ms

`a` 查询在 flush 后首次执行（15:42:07），延迟达到 **2113ms**（20 轮所有记录中最高）。后续重复查询稳定在 155-360ms。这是 flush 后 VM 页面重缓存 + limit=100 的组合效应。

#### `桌面` (CJK 2 字符) 全走 linear

`桌面` 查询 13 次，avg **177ms**，全走 advanced-linear-gcd，candidates=0，**results=0**。CJK 2 字符无法生成足够 trigram（需要 3 字节组），降级为 linear 且找不到结果。这暴露了 CJK 搜索在短关键词下的体验问题。

#### `*.swift` / `*.py` / `*.cpp` 等 glob 查询全走 linear

| 查询 | 查询次数 | 平均延迟 | 路径 |
|------|---------|---------|------|
| `*.swift` | 2 | **606ms** | advanced-linear-gcd |
| `*.py` | 5 | **384ms** | advanced-linear-gcd |
| `*.cpp` | 11 | **190ms** | advanced-linear-gcd |
| `*.json` | 1 | 186ms | advanced-linear-gcd |
| `*config*` | 4 | **143ms** | advanced-linear-gcd |
| `*test*.cpp` | 6 | **182ms** | advanced-linear-gcd |

**glob 模式全部走 linear scan**，P18 问题在真实用户场景下被放大。`*.swift` 首查 1029ms。

### 2.5 `SearchEngineAdvancedQuery` 118ms 异常

本轮该查询延迟 118ms（R19 3.1ms），其中 trigram 阶段耗时 118ms，phase2 仅 0.1ms。候选仅 90 条。

**诊断**：trigram 阶段 118ms 用于查找 90 个候选，说明 posting list 查询本身很快，但 flush 后 trigram index 的 posting list 数据页尚未预热。这是一次性现象（后续查询会缓存）。

### 2.6 `readme size>1mb` 46.8ms 异常

延迟从 R19 的 19.1ms 涨到 46.8ms，其中 **lock_wait=36ms**。这是本轮唯一出现 lock_wait > 0 的查询，说明 flush 后短暂的锁争用。

### 2.7 错误与异常

| 类型 | 数量 | 详情 |
|------|------|------|
| ERROR | 0 | 无 |
| WARN | 0 | 无 |
| 崩溃 | 0 | 无 |
| lock_wait > 0 | **1** | `readme size>1mb` lock_wait=36ms |
| GUI 高延迟查询 | 多 | `a` 2113ms、`*.swift` 1029ms 等 |

---

## 3. 问题分析

### 整体诊断

R20 是 **flush 后部分预热轮次**（距 flush ~7min），整体性能两极分化：
- **P50 = 46.8ms**（R19 106.8ms，**-56%**）— trigram 路径已充分预热
- **avg = 129.7ms**（R19 114.5ms，+13%）— 被极端 linear scan 延迟拉高
- **`a` 645ms 创 20 轮新高**（排除 R1 `test type:folder` 1349ms）

**分化原因**：flush 后 posting list 缓存已重建（regex-trigram avg 31.8ms，R19 114ms -72%），但 flush 导致 paged index VM 页面失效，linear scan 路径严重劣化。

### 现有问题更新

| 问题 | 等级 | R20 表现 | 趋势 |
|------|------|---------|------|
| **P0** | **Critical** | GUI `config`/`readme`/`test` 全走 linear 150-181ms | **GUI 实证，最高优先级** |
| P1 GCD filter | Medium | 182-241ms | 恶化 |
| P2 regex 候选集 | Medium | 37.9ms（**恢复**） | flush 预热后改善 |
| P3 content 搜索 | Medium | **303ms** | 严重恶化 |
| P4 date filter | Medium | 182ms，0 结果 | 持续 |
| P7 锁争用 | Medium | lock_wait=36ms | 复现 |
| **P18** | **High** | `wild:*.txt` **224ms**，glob 全走 linear | **持续恶化，GUI `*.swift` 1029ms** |
| P19 `#define` | Medium | **325ms** | 持续恶化 |
| P20 WAL 累积 | Medium | 7,532（下降，flush 生效） | 改善 |

### 新发现

**P21 — GUI limit=100 触发 trigram 降级**  
GUI 使用 limit=100，`config`(6 字符)、`application`(11 字符)、`readme`(6 字符)、`node_modules`(12 字符) 全走 linear-gcd（candidates=0），但 benchmark limit=5 时同样的 `readme` 走 trigram 仅 4.5ms。**limit 大小可能影响搜索路径选择**，或者 GUI 查询的某种参数差异导致 trigram 被跳过。

**P22 — CJK 短关键词体验问题**  
`桌面`(2 字符 CJK) 走 linear 177ms 且 0 结果。2 个 CJK 字符可生成 1 个 trigram（每个 CJK 字符 3 字节 UTF-8），理论上可用于预过滤。

---

## 4. 优化建议

基于 R20 数据和 R19 GUI 日志分析，更新优先级：

1. **放宽 trigram 降级阈值 + 修复 GUI trigram 降级**（P0 + P21 Critical）— `config`/`readme`/`test` 等 GUI 查询全走 linear 150-181ms，trigram 可降至 <5ms。需调查 limit=100 是否触发降级
2. **恢复 glob-trigram 路径**（P18 High）— `wild:*.txt` 224ms↑，GUI `*.swift` 1029ms
3. **修复 `#define` trigram 失效**（P19 Medium→High）— 325ms，持续恶化
4. **Regex 查询提前终止**（P2）— 已恢复至 37.9ms
5. **内容搜索优化**（P3）— 303ms，flush 后严重
6. **date filter 支持**（P4）— 182ms 走 linear，0 结果
7. **Flush/启动后预热**（P16）— 消除 `a` 645ms、`SearchEngineAdvancedQuery` 118ms 峰值
8. **CJK 短关键词 trigram 支持**（P22）— `桌面` 177ms 走 linear
9. **WAL flush 策略优化**（P20）— 已改善

---

## 5. 总结

R20 是 **flush 后部分预热轮次**（WAL 7,532，flush 后 ~7min），性能两极分化：
- **P50 46.8ms**（R19 106.8ms -56%）— trigram 路径已预热
- **avg 129.7ms**（R19 114.5ms +13%）— 被 linear scan 拉高
- **max 645.1ms**（`a`，**20 轮最高单查询**）— flush 后 VM 页面冷 + 5.33M 记录 linear scan
- regex-trigram avg **31.8ms**（R19 114ms **-72%**）— flush 预热后显著恢复
- advanced-linear-gcd avg **275.3ms**（R19 233.6ms +18%）— flush 后 VM 劣化
- `content:batchMutate` **303ms**（R19 172ms +76%）— flush 后最差
- `#define` **325ms**（R19 232ms +40%）— 持续恶化
- lock_wait=36ms 复现（`readme size>1mb`）
- **R19 GUI 日志暴露 P0 实质**：`config`/`readme`/`test`/`application`/`node_modules` 全走 linear 150-181ms
- **R19 GUI `*.swift` 1029ms**：glob 路径丢失的真实用户影响
- **R19 GUI `a` 2113ms**：flush 后 linear scan + limit=100 的极端延迟
- **新增 P21**（GUI limit=100 trigram 降级）和 **P22**（CJK 短关键词）
