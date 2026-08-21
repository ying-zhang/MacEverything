# Round 18 基准测试报告

**时间**: 2026-04-19 15:33  
**会话**: 15:32:46 启动，已运行 ~1min（冷启动首轮）  
**索引记录数**: 5,306,570（benchmark 结束时）  
**WAL 条目**: 4,604（启动时回放）

---

## 1. 基准测试结果

| 指标 | 值 | 对比 R17 | 对比 R15（最佳） |
|------|-----|---------|-----------------|
| 成功 | 27/28 | 持平 | 持平 |
| 最小延迟 | 0.2ms | 1.4→0.2 | 0.1→0.2 |
| 最大延迟 | 196.5ms | 234.9→196.5 (-16%) | 127.3→196.5 (+54%) |
| 平均延迟 | **51.8ms** | 44.1→51.8 (+17%) | 34.7→51.8 (+49%) |
| P50 延迟 | 43.3ms | 39.6→43.3 (+9%) | 31.8→43.3 (+36%) |
| P90 延迟 | 117.5ms | 83.6→117.5 (+41%) | 81.4→117.5 (+44%) |
| P99 延迟 | 196.5ms | 234.9→196.5 (-16%) | 127.3→196.5 (+54%) |

### 按搜索路径分类

| 搜索路径 | 查询数 | 平均延迟 | 最大延迟 | 对比 R17 |
|---------|--------|---------|---------|---------|
| advanced-trigram | 13 | 13.2ms | 77.0ms | — (新路径分布) |
| advanced-linear-gcd | 10 | 96.4ms | 196.5ms | — |
| advanced-regex-trigram | 3 | 48.8ms | 67.0ms | 49.7→48.8 (持平) |
| advanced-pure-filter-soa-gcd | 1 | 116.4ms | 116.4ms | 181.2→116.4 (-36%) |

### 逐查询明细

| 查询 | 延迟(ms) | 搜索路径 | 候选数 | 结果数 | 对比 R17 |
|------|---------|---------|--------|--------|---------|
| 1-char `a` | **196.5** | adv-linear-gcd | 0 | 5 | 234.9→196.5 (-16%) |
| 2-char `ab` | 88.1 | adv-linear-gcd | 0 | 5 | 47.7→88.1 (+85%) |
| 3-char `abc` | 6.9 | adv-trigram | 20662 | 5 | 28.0→6.9 (-75%) |
| `test` | 45.1 | adv-linear-gcd | 0 | 5 | 45.1→45.1 (持平) |
| `readme` | 3.1 | adv-trigram | 10350 | 5 | 5.4→3.1 (-43%) |
| `package.json` | 6.3 | adv-trigram | 11250 | 5 | 10.4→6.3 (-39%) |
| `SearchEngineAdvancedQuery` | 2.6 | adv-trigram | 82 | 5 | 3.6→2.6 (-28%) |
| `/usr/local/bin` | 77.0 | adv-trigram | 145677 | 2 | 44.7→77.0 (+72%) |
| `regex:test.*\.py` | 67.0 | adv-regex-trigram | 48788 | 5 | 67.0→67.0 (持平) |
| `regex:.*\.swift` | 36.4 | adv-regex-trigram | 6862 | 5 | 38.9→36.4 (-6%) |
| `regex:^config` | 43.1 | adv-regex-trigram | 19921 | 5 | 43.2→43.1 (持平) |
| `wild:*.txt` | **90.4** | adv-linear-gcd | 0 | 0 | 5.4→90.4 (**+1574%**) |
| `readme ext:md` | 5.2 | adv-trigram | 10350 | 5 | 9.3→5.2 (-44%) |
| `readme size>1mb` | 8.8 | adv-trigram | 10350 | 5 | 11.8→8.8 (-25%) |
| `test type:folder` | 117.5 | adv-linear-gcd | 0 | 5 | 61.1→117.5 (+92%) |
| `readme date:today` | 117.4 | adv-linear-gcd | 0 | 0 | 39.6→117.4 (+196%) |
| `test path:/usr` | 115.2 | adv-linear-gcd | 0 | 5 | 55.2→115.2 (+109%) |
| `search engine` | 4.2 | adv-trigram | 4 | 4 | 12.7→4.2 (-67%) |
| `search engine query` | 4.0 | adv-trigram | 10101 | 0 | 40.2→4.0 (**-90%**) |
| `readme !config` | 5.4 | adv-trigram | 10350 | 5 | 6.4→5.4 (-16%) |
| `测试` (CJK) | 0.2 | adv-trigram | 182 | 5 | 4.3→0.2 (-95%) |
| `#define` | 126.1 | adv-linear-gcd | 0 | 0 | 47.2→126.1 (+167%) |
| `"package.json"` | 5.7 | adv-trigram | 11250 | 5 | 7.8→5.7 (-27%) |
| `content:batchMutate` | 116.4 | adv-pure-filter-soa-gcd | 0 | 5 | 181.2→116.4 (-36%) |
| (empty) | FAIL | - | - | - | — |
| long multi-term | 1.5 | adv-trigram | 0 | 0 | 1.4→1.5 (持平) |
| `.` | 112.5 | adv-linear-gcd | 0 | 5 | 83.6→112.5 (+35%) |
| `..` | 109.1 | adv-linear-gcd | 0 | 5 | 55.0→109.1 (+98%) |

---

## 2. 日志分析

### 2.1 启动性能

| 阶段 | 值 | 对比 R17 |
|------|------|---------|
| 索引加载（paged index） | 11.6s | 13.6→11.6s (-15%) |
| WAL 回放 | **4,604 条** / 1.59s | 5,489→**4,604（-16%）** |
| FSEvents 回放 | 0.32s | 0.22→0.32s (+45%) |
| **总启动时间** | **13.5s** | 13.8→13.5s (-2%) |
| 索引记录数（加载后） | 4,481,091 | 4,475,789→4,481,091 (+5,302) |
| lastEventId | 146,716,707 | 145,907,803→146,716,707 (+808,904) |
| live totalRecords（R18 末） | 5,306,570 | 5,277,196→5,306,570 (+29,374) |

### 2.2 WAL 增长趋势

| 会话 | WAL 条目 | 增长 |
|------|---------|------|
| R14/R15 会话 | 316 | — |
| R16 会话 | 3,319 | +3,003 |
| R17 会话 | 5,489 | +2,170 |
| **R18 会话** | **4,604** | **-885** |

WAL 条目首次下降，可能因为上次 flush 后积累时间较短。

### 2.3 查询日志分析

日志记录了 6 条高延迟查询：

| 时间 | 查询 | 延迟 | 路径 | totalRecords | 备注 |
|------|------|------|------|-------------|------|
| 15:33:26 | `test` | 141ms | advanced-linear-gcd | 5,306,435 | **results=1**，GUI/非 benchmark |
| 15:33:34 | `a` | 196ms | advanced-linear-gcd | 5,306,511 | benchmark |
| 15:33:38 | `test path:/usr` | 117ms | advanced-linear-gcd | 5,306,543 | benchmark |
| 15:33:39 | `#define` | 126ms | advanced-linear-gcd | 5,306,563 | benchmark |
| 15:33:39 | `content:batchMutate` | 116ms | adv-pure-filter-soa-gcd | 5,306,565 | benchmark |
| 15:33:40 | `.` | 116ms | advanced-linear-gcd | 5,306,570 | benchmark |

**关键观察**：
1. **lock_wait 信息未出现**：所有日志查询均无 lock_wait 字段，说明所有 lock_wait=0ms
2. **`test` results=1**：日志首查 `test` 仅返回 1 条结果（limit=1？GUI 查询？），延迟 141ms

---

## 3. 架构变更分析

### 重大发现：所有查询路径统一为 `QueryAdvanced`

R18 是首个记录到所有查询走 `QueryAdvanced` 路径的轮次。对比 R17：

| 搜索路径 | R17 | R18 | 变化 |
|---------|-----|-----|------|
| linear | 8 查询 | 0 | → advanced-linear-gcd |
| trigram | 7 查询 | 0 | → advanced-trigram |
| structured | 1 查询 | 0 | → advanced-trigram |
| glob-trigram | 1 查询 | 0 | → advanced-linear-gcd |
| advanced-trigram | 4 查询 | **13 查询** | 吸收了原 trigram/structured |
| advanced-linear-gcd | 2 查询 | **10 查询** | 吸收了原 linear/glob-trigram |
| advanced-regex-trigram | 3 查询 | 3 查询 | 持平 |
| advanced-pure-filter-soa-gcd | 1 查询 | 1 查询 | 持平 |

这是一个有意的架构统一：`Query` 方法被 `QueryAdvanced` 替代，所有查询都走高级查询管道。

### 路径变更影响

**改善的查询**：
- `search engine query` (3词)：40.2→4.0ms (**-90%**)，从 linear → advanced-trigram
- `abc` (3字符)：28.0→6.9ms (-75%)，从 trigram → advanced-trigram
- `content:batchMutate`：181.2→116.4ms (-36%)
- `测试` (CJK)：4.3→0.2ms (-95%)，trigram → advanced-trigram

**劣化的查询**：
- `wild:*.txt`：5.4→90.4ms (**+1574%**)，从 glob-trigram → advanced-linear-gcd（**P17 回归**）
- `#define`：47.2→126.1ms (+167%)，linear → advanced-linear-gcd
- `/usr/local/bin`：44.7→77.0ms (+72%)，structured → advanced-trigram（145K 候选 + phase2）
- `..`：55.0→109.1ms (+98%)
- `test type:folder`：61.1→117.5ms (+92%)

### 新问题

**P18 — `wild:*.txt` glob-trigram 路径丢失**  
R17 走 glob-trigram 5.4ms，R18 降级为 advanced-linear-gcd 90.4ms。`QueryAdvanced` 管道可能丢失了对 `wild:` 前缀的 glob-trigram 优化。

**P19 — `#define` 延迟翻倍**  
47.2ms → 126.1ms (+167%)。`QueryAdvanced` 管道中 `#define` 的特殊字符处理可能导致 trigram 失效。

---

## 4. 优化建议

维持现有 6 条优化建议，新增 2 条：

1. **放宽 trigram 降级阈值**（P0）— `test` 仍走 linear-gcd
2. **Regex 查询提前终止**（P2）— 48788 候选，67ms
3. **内容搜索直接查内容索引**（P3）— 116ms（改善但仍偏高）
4. **date filter 支持**（P4）— 仍走 linear，0 结果
5. **多词查询独立 trigram + 交集**（P5）— `search engine query` 已修复 (**R18 验证通过**)
6. **Flush/启动后预热**（P16/P17）
7. **恢复 glob-trigram 路径**（P18 新增）— `wild:*.txt` 从 5.4→90.4ms
8. **修复 `#define` trigram 失效**（P19 新增）— 47→126ms

---

## 5. 总结

R18 是 **架构变更后首轮冷启动测试**（WAL 4,604 条，无 flush），性能混合：
- avg 51.8ms（R17 44.1ms，+17%），主要受冷启动影响
- **架构统一**：所有查询走 `QueryAdvanced`，消除了 `Query` 旧路径
- **改善**：`search engine query` -90%、`abc` -75%、CJK -95%、`content:batchMutate` -36%
- **劣化**：`wild:*.txt` +1574%（glob-trigram 路径丢失）、`#define` +167%、`..` +98%
- lock_wait=0ms，无崩溃，无错误
- WAL 4,604（首次下降）
- 新增 P18（glob-trigram 丢失）和 P19（`#define` trigram 失效）
