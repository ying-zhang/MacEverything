# 交叉检验发现（本人独立分析部分）

> 本文档记录本人在 redo 交叉检验中独立发现的问题，最终报告将合并子代理审计结果。

## 已验证正确（独立复算/差分模糊测试通过）

- `PostingListIntersection.h` — `intersectSortedPostingLists` 与 `std::set_intersection` 参考实现 20 万组随机差分一致（0 不匹配），斜切策略阈值(≥32)与标量归并正确。
- `TrigramExtraction.h` — `extractByteTrigrams` 的 trigram **集合内容**与朴素参考实现 2 万组随机差分完全一致（contentMismatch=0）；`seen` 位图大小为 1<<24，与 `makeTrigram` 最大输出 0xFFFFFF 匹配，无越界。
- `SIMDSearch.h` — `simdFind` NEON 主循环/尾循环的 16 字节读边界经逐项推演均在 `hayLen` 内（末次读取最高到 `hayLen-1`），无越界读；`simdToLowerAscii` 原地小写边界正确。
- `SearchEngine::encodeScore` / `computeMultiTermScore` / `termQuality` — 评分语义（missCount>qualitySum>pathLen，低分优先）正确，排序比较器 `a.score < b.score` 是严格弱序。
- `HttpToken.h` / `HttpServer.cpp` — token 为 256 位小写 hex、文件 0600、比较为固定长度常量时间 XOR，未见越界或时序攻击。
- `QueryDateParser.h` — 关键字/相对/ISO/比较/区间解析逻辑正确（`>date` 用区间 end、`>=` 用 start、`<` 用 start、`<=` 用 end 的语义是对的）。

## 已发现的问题

### 1. 多音字拼音首字母错误（中危，已实证）
- 位置：`MacEverything/Core/StringUtils.cpp` `mandarinInitialsKey()` / `appendMandarinInitial()`。
- 现象：逐字调用 `CFStringTransform(kCFStringTransformMandarinLatin)`，对多音字只取一个（常为常用音）读音的首字母，词义上下文下常错：
  - `重庆` → `zq`（应为 `cq`，重=chóng）
  - `长城` → `zc`（应为 `cc`，长=cháng）
- 影响：用户用正确拼音首字母（cq、cc）搜索时无法命中含多音字词的文件（搜索召回下降）。
- 建议修复：引入小型多音字词表（按词匹配覆盖常见多音词：重庆/长城/音乐/银行/重复/重要/调整/生长…），命中词表时用整词首字母，否则回退逐字。

### 2. 拼音仅为「首字母」，未实现全拼匹配（信息/范围澄清）
- 位置：`SearchEngine.h` `pinyinInitialsPool_` / `mandarinInitialsKey()`。
- 现象：`mandarinInitialsKey("张三") == "zs"`，仅 2 字符，无法用全拼 `zhangsan` 命中（`simdContains(pinyinData, ...)` 与 trigram 索引均只基于首字母 key）。提交信息「Add pinyin matching」与清单中「全拼」描述偏宽，实际仅为拼音首字母。

### 3. `extractByteTrigrams` 输出顺序不一致（低危/信息）
- 位置：`MacEverything/Core/TrigramExtraction.h`。
- 现象：短串(≤512B)走 sort+unique 返回**有序**结果；长串(>512B)走位图去重返回**首次出现序**。内容集合正确，但顺序不同。
- 影响：当前唯一调用方 `ContentIndex::extractTrigrams` 将 trigram 作为哈希表 key，不依赖顺序，故无实际危害；但对未来假设有序的调用方是隐患（注释已说明为有意优化，长串避免全局位图随机访问）。

### 4. `QueryDateParser.h` 比较语义注释与实现不符（低危/文档）
- 位置：`QueryDateParser.h` L109-110 注释「For GT/GE: compare against the start」「For LT/LE: compare against the end」与实现相反（GT 用 end、LT 用 start）。代码正确，注释误导。

## 待合并
- 4 个子代理的审计结果（WAL/持久化、查询解析、StringPool/SIMD、排序/内容搜索/索引生命周期）。

## 子代理审计结果（已合并）

### 子代理 B：StringPool/SIMD/字符串工具（已完成，均实证）

**确认 bug：**

- **BUG-1（中危，潜伏）`StringPool::compact()` 越界读** — `StringPool.h` L183-184 拷贝 `buffer_.data()+entries_[i].offset` 时**未**加 `offset+length<=buffer_.size()` 守卫（而 fork 给 `data()/length()/view()/str()` 都加了）。ASan 实证：用 `{offset:0xFFFFFFF0,length:500}` 的损坏 entry 加载池即 heap-buffer-overflow。任何从持久化恢复的损坏池都会触发。`isLive()` 也缺同一守卫（损坏 entry 下 `isLive()==true` 而 `length()==0` 不一致）。修复：compact 两趟循环都跳过 `offset+length>buffer_.size()` 的 entry。
- **BUG-2（中危）拼音丢全部扩展区 CJK（Ext B+, U+20000+）** — `StringUtils.cpp` 只按 UTF-16 码元判断 BMP 范围（0x3400-4DBF/4E00-9FFF/F900-FAFF），代理对永不匹配。实证：`mandarinInitialsKey("𠀀")==""`；端到端 `query("h")` 对 `𠀀.txt` 返回 0 结果。`SearchEngine::isCJKCodepoint`（SearchEngineIndex.cpp L426-430）同病。修复：解码代理对，范围扩到 0x20000-0x2FFFF。
- **BUG-3（低中危）3.3% BMP 汉字拼音 key 为空** — CF 对部分汉字（U+FA11 﨑、U+3410 等）转写无产出，`appendMandarinInitial` 静默丢弃。全量扫描：930/28096（3.3%）产出空 key（`崎→"q"` 但 `﨑→""`）。端到端 `query("q")` 命中 崎.txt 但漏 﨑.txt。修复：无产出时回退为原码点。

**验证正确：** SIMDSearch.h（ASan 穷举 hayLen 0-80 × 全部 needle 长度无越界）、TrigramExtraction.h（0-600 全尺寸与朴素参考一致）、PostingListIntersection.h（2 万随机 + 2 千斜切差分一致）、PathUtils.h、toLower/normalizeNFC/NFD（无效 UTF-8、内嵌 NUL 不崩溃）。仓库自身 test_string_pool/test_tilde/test_trigram/test_adaptive 全通过。

### 子代理 C：查询解析/过滤/日期（已完成，均实证）

**确认 bug：**

- **C1（中危）`QueryTokenizer.h` L114-122 过滤器参数吞掉尾部 `<`/`>`** — 参数重收集循环只断在空格/`|`/`!`/`"`，不断 `<`/`>`。实证：`<ext:cpp | ext:h>` → `OR(FILTER(ext:cpp), FILTER(ext:"h>"))`，`ext:h` 结果静默丢失；仓库自身测试 55.16 也产出 `ext:"cpp>"`（仅 dump 所以通过）。修复：只允许 `<`/`>` 作为参数首字符。
- **C2（中危）`QueryDateParser.h` L283-286 相对月/年在月末漂移** — `tm_mon -= 1` 后 mktime 把 2 月 31 日归一化为 3 月 2 日。实证：2024-03-31 上 `last1months` 起点为 2024-03-02 而非 2024-02-29（漏 2/29-3/1）；2024-02-29 上 `last1years` 起点为 2023-03-01。每月 29-31 日触发。另 L282 `tm_mday -= n*7` 大 n 有符号溢出 UB。修复：钳制 day 到目标月长度。
- **C3（低中危）`ASTStructuredTransform.h` L57 大小写敏感的斜杠查询被小写化** — 用已小写的 pattern 作 `TERM.text` 却保留 `caseSensitive=true`。实证：`case:Abc/Def` → `TERM("def", caseSensitive)`，`Def` 永不匹配。修复：term 文本取原始大小写。
- **C4（低危）`QueryFilterParser.h` L123-128 开放式 size 区间损坏** — `size:1mb..` → `RANGE [1MB, 0]` → 无匹配（日期解析器正确处理 `dm:2024-01-01..` → UINT64_MAX）。修复：右空→UINT64_MAX，左空→0。
- **C5（低危）`QueryParser.cpp` L86-96 + `SearchEngineAdvancedQuery.cpp` L193 取反的非法过滤器匹配所有** — `!dm:banana` → `NOT(invalid)` → evalFilter 返回 false → NOT 翻转为 true，违反「非法过滤不扩大结果」原则（B17）。修复：非法过滤器取反也返回不匹配。
- **C6（低危）`QueryFilterParser.h` L30-32 `type:` 参数未小写** — `type:File`/`type:FOLDER` 永不匹配。修复：统一小写。
- **C7（低危）`QueryTokenizer.h` L70-80 无转义处理** — `"a\"b"` 被拆为 `QUOTED("a\")+WORD(b)+QUOTED("")`；`path:/a!b` → `path:/a AND NOT b`。路径中带转义引号/`!` 无法表达。
- **C8（低危）`ASTStructuredTransform.h` L28-30 引号短语失去字面语义** — `"a/b"` 变路径约束；`"*.cpp"` 变 GLOB。

**次要注记（已证）：** `ext:.cpp` 永不匹配；`dm:"today"`/`ext:"cpp"` 空参数零结果；`size:1.5` 截断为 1 字节；`size:1mbx` → 1 字节；多余 `>` 丢弃后续词（`a > b` → 仅 `a`）；`lastNdays` 跨 N+1 个日历日（符合测试 A10，文档化行为）；`content:`/`infile:` 无 C++ 引擎处理（Swift 拦截，HTTP 调用方得零结果）。

**验证正确：** 日期比较阈值（GT=end/GE=start/LT=start/LE=end）、闰年/ISO 校验、开放日期区间、thisweek/lastweek 周日锚定、DST 安全的 yesterday、size 单位/饱和、ext 多扩展名、结构化查询各模式、AND/OR/NOT 优先级与嵌套上界、`lastNdays` 语义、`QueryNeedsAnalysis` 去掉 `needsName` 的改动正确。

### 子代理 A：WAL/持久化/CRC（已完成）

**核心结论：** CRC32（ARM 硬件路径 + 软件 slicing-by-4）逐位匹配 CRC-32/ISO-HDLC 参考（`"123456789"`→`0xCBF43926`，`""`→0，穷举 0..257）；WAL 帧格式与 torn-write 恢复健全（每个字节边界截断均恢复正确前缀）；flush/WAL 轮转的崩溃安全性经 fork+SIGKILL 随机杀点 5/5 次恢复连续未损坏前缀；段轮转 + 幂等重放（按 path 键控）正确。**无 critical/high。**

**确认缺陷（均为低危）：**
- **W1/B1（低危）`StringPool::Entry` 8 字节中含 2 字节未初始化 padding** — `{uint32_t offset; uint16_t length}` 原始序列化进每种索引格式并纳入 CRC（FlatIndexWriter.cpp:32-33、PagedIndexWriter.cpp:66-68/106-128）。CRC 自洽故不损坏，但文件字节非确定 + 陈旧堆数据泄露；基线即存在。修复：每 entry 只序列化 6 字节或零填充 padding。
- **W2/B2（低危）写入端/读取端长度上限不对称** — IndexWAL.cpp:204/210 与 ContentIndexPersistence.cpp:149/154/207 写 uint32 长度**无上限**，而读取端拒绝 pathLen>65536（IndexWAL.cpp:289）、nameLen/pathLen>65536(:393/:399)、pathLen>1MiB 与 triCount>1M（ContentIndexPersistence.cpp:275/284）。超限 entry 带合法 CRC 写入但重放时被判损坏并尾部截断 → 变更静默丢失。macOS 上不可达（PATH_MAX 1024）但属帧契约违约。修复：写入端施加相同上限。
- **W3/B3（低危）torn-header（1-7 字节文件）处理不一致** — IndexWAL.cpp:118-126 拒绝打开（attachWAL 回退到新段），ContentIndexPersistence.cpp:85-103 截断为 0 并重写头部。两种情况都无数据丢失（头部先于任何 entry）。修复：统一为截断重写。
- **W4/B4（信息）字节序** — 所有格式主机字节序无归一化，macOS 恒 LE 故无碍。
- **W5/B5（信息）FlatIndexWriter.cpp:295-304 注释错误** — 注释称头部 CRC 覆盖 60 字节，代码实为 36（加载端 :374 一致）。

**已验证正确（子代理 A）：** CRC32 双路径、WAL entry 帧往返（空/UTF-8/65536 字节）、每个字节边界的 torn-write 恢复、CRC 不匹配首错停止 + 尾部截断、段轮转 + 幂等重放（SearchEngine.cpp:1279-1360）、先写 WAL 再变更的追加顺序（:561）、load() 先于 attachWAL 的启动顺序、v6 头部 CRC(36 字节)与各 section 边界、v6/v5/paged 格式、ContentIndexWAL v2 路径格式 + resolver 重放（:410/419）、软大小上限不丢变更。


### 子代理 D：排序/内容搜索/索引生命周期（已完成）

**确认 bug：**

- **D1（中危）CJK 查询静默丢路径匹配（相对基线的回归）** — `SearchEngineAdvancedQuery.cpp:906-932`（fork 新增的 Stage 1b）。当 Stage 1 名字 trigram 候选超 `totalSize/10` 时落入 Stage 1b，仅求交 **CJK bigram 索引**——而该索引只覆盖文件名（`cjkBigramIndex_` 由 `namePool_` 构建，SearchEngineIndex.cpp:505-521）。Stage 1b 成功（`cjkCands<=totalSize/4`）时，候选集排除「仅在路径中含该词」的记录，`evalNode` 永不看到它们。基线则回退线性扫描能命中。实证（真实引擎）：10000 记录、2000 名字含 `文件`、一条 `report.txt` 在路径 `/Users/test/文件/2024` → fork `query("文件")` 返回 2000（**漏 report.txt**），基线返回 2001（含）。修复：Stage 1b 时并入 path-trigram 候选，或仅当 path-trigram 索引为空/无新增时才用 Stage 1b，否则回退线性扫描。
- **D2（低中危）`generateSnippet` 对 65537 字节关键词死循环** — `ContentIndex.cpp:211-251`，`advance = bytesRead - overlapSize` 在 `keyword.size()==65537` 时 `overlapSize==bytesRead==65536` → `advance==0` → `fileOffset` 不前进，`while(fileOffset<maxRead)` 永久重读首块。HTTP 层请求体/头各限 65536 故非网络可达 DoS，但 GUI/CLI/库调用方用 ≥64KB 精确 65537 字节查询即挂死。修复：`if (overlapSize>=bytesRead) advance=bytesRead;` 或前置拒绝超长关键词。
- **D3（中高危，低概率）`completePhase2()` 可能安装来自压缩前陈旧快照的 trigram 索引（竞态）** — `SearchEngineV6.cpp:225-286` 在快照 SoA 后于锁外构建索引（秒级），随后锁内交换却**不复查** `phase2Pending_`/`compactionGen_`。若 `compactRecords()`（SearchEngine.cpp:903-1174）在快照与交换之间完成，记录被重编号且 `phase2Pending_` 置 false，但交换照常进行 → 装进引用压缩前索引的 postings。重放循环（V6:241-279）以旧 `snapSize` 为界不纠正，查询随即以陈旧/越界 idx 读 `typesPtr[idx]`（`SearchEngineAdvancedQuery.cpp:1083/1131` 无越界检查）→ 越界读。当前用法下概率低（大索引首次压缩慢于 Phase-2 构建），数百万记录索引上压缩与 Phase-2 同秒级时更现实。修复：交换前在 unique lock 内复查 `phase2Pending_`（及 `compactionGen_`），若发生压缩则重建或直接返回。

**次要/设计注记（非确认 bug）：** `computeMultiTermScore` 的 `missCount` 为 uint8_t（>255 词回绕，实际不可达）；`ContentIndex::indexFile` 的 modTime 早退（mtime 未变但内容变会被漏，既有取舍）；Stage 1 多词在某单词语义超阈值时回退线性扫描（仅性能）。

**已验证正确（子代理 D）：** termQuality/computeMultiTermScore/encodeScore 与排序比较器严格弱序（穷举 (missCount,qualitySum) 字节对，多匹配词绝不排在少匹配词之后）；posting 合并 + 倒排维护（3000 随机增删改保持有序、200 查询与暴力一致）；SIMDSearch/TrigramExtraction；RescanDebounce（含 `/Users` vs `/Users2`、根 `/` 边界）；DirectoryScanner 工作线程终止不变量/取消复用；FSEvents 应用顺序（先对内容索引删、后 `batchMutate` 增，fileIndex 始终有效）；Content↔engine remap 一致性（`beginFileIndexRemap`/`endFileIndexRemap` + 租约，无死锁）；`queryStructured` 死代码但 `pathSegmentsMatch` 无 OOB；ServiceEngine 生命周期/WAL attach 顺序/实例锁/代际检查一致。
