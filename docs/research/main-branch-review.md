# MacEverything `main` 分支完整代码审查报告

- 审查对象：`main`（`1a4b143`）的全部代码；工作树为 `fix/index-config-signature`（`63e1c85`），与 `main` 的差异仅为 `IndexPersistence.{h,cpp}` 中 15 行，因此除该文件外所有结论对 `main` 逐字节成立。
- 代码规模：423 个受版本控制文件、约 75.7k 行（Core C++ ≈17.9k、App Swift ≈10.7k、Bridge ObjC++ ≈0.9k、CLI ≈1.1k、tests ≈19.9k）。
- 审查方式：4 个独立子代理分域深读（Swift/AppKit、扫描+FSEvents+ServiceEngine、查询引擎、Bridge/HTTP/CLI/脚本/CI），再由本代理对全部 High 级结论逐条回读源码复核，并单独审查持久化/WAL/锁序/InstanceLock/HTTP 安全。
- 客观验证：
  - 全部 Core C++ 源文件 `clang++ -std=c++20 -Wall -Wextra -fsyntax-only` 零告警通过。
  - `make lint-bridge` / `lint-localizations` / `lint-docs` 通过。
  - **实际编译并运行了完整快速测试套件**（下载与安装包内 `libre2.11.0.0` / `absl 2601.0.0` 同版本上游头文件，链接应用内嵌 dylib）：`test_all --fast` → **1681 passed / 0 failed**。
- 结论标注：`✅复核` = 本代理已回读源码确认；`◻报告` = 子代理给出 `file:line`+证据，未逐条复核。

---

## 一、总体结论

这是一个工程质量**明显高于同类个人项目平均水准**的代码库：分层清晰（SwiftUI / ObjC++ 桥 / 纯 C++ 核心）、核心算法有 81 个测试分区与 20k 行测试、关键不变量写进了 `requirements.md` 并基本被代码遵守、HTTP 安全边界完整、WAL/CRC/原子替换等持久化细节做得相当扎实、无 `strcpy/sprintf/system` 等危险 API、无命令注入面。本轮高、中风险修复已落在当前工作树，完整状态以测试和下方标记为准。

但存在两类系统性问题：

1. **"文档/历史审计声称已验证"与"当前代码"之间存在落差**：`docs/research/cross_check_findings.md` 记录的 20 项发现中，绝大多数（含 1 项越界读、多音字、Stage-1b CJK 丢路径、Phase-2 竞态、generateSnippet 死循环、tokenizer 过滤参数、日期漂移等）**在当前 main 上已修复**，但文档本身没有更新，会误导后续维护者与依赖该文档的审计。本次已逐条核对并给出"已修复/仍存在"清单（见第六节）。
2. **缺陷集中在生命周期、事件 ID 记账与跨层契约**，而不是热路径：全量扫描与 FSEvents 的事件 ID 衔接、timer 与 `stopMonitoring()` 的竞态、单实例锁只覆盖启动路径、内容索引的路径范围过滤在 HTTP 与 GUI 两条链路上不一致。

按严重度合计：**8 项 High、17 项 Medium、约 14 项 Low**。H1/H2 已修复并加入事件水位回归覆盖；本轮其余高、中风险项也已完成代码修复或在发布脚本中加入明确的配置门禁。

---

## 二、High（严重）问题

### H1. `main` 上裸 `flush()` 抹掉 `config_signature`，导致每次启动全量重扫 ✅复核 ✅已修

- `IndexPersistence::flush(uint64_t lastEventId, bool force)`（`MacEverything/Core/IndexPersistence.cpp:207`）在 `main` 上构造 `IndexMetadata meta;` 只填 `lastEventId`，`extra` 为空，随后整文件重写 `index.v6`。而 `extra["config_signature"]` 只在 `ServiceEngine::buildMetadata()`（`ServiceEngine.cpp:353`）里写入。
- 走裸重载的路径恰好是**退出时**的那次：`ServiceEngine::shutdown()` → `persistence->compact(lastEventId, /*force=*/true)`（`ServiceEngine.cpp:1102`），以及自动维护定时器（`IndexPersistence.cpp:470`）与 `ServiceEngine::compactIndex()`（`:914`）。
- 下次启动 `load(expectedConfigSignature)`（`IndexPersistence.cpp:101-108`）读到空签名 → 判定 "config changed" → 删除 `index.v6`、删除全部 WAL 段 → **全量重扫**。这与 `requirements.md` §5.2"后续启动（增量加载）"直接冲突。
- 工作分支 `63e1c85` 的修复（缓存 `lastMeta_`，裸重载继承 `extra`，见 `IndexPersistence.h:93-98`、`IndexPersistence.cpp:207-221`）**经复核正确**：`lastMeta_` 的读写在 `compactionMutex_` 下配对，`load()` 中的赋值发生在 `startAutoCompaction()` 之前，退出路径因此保留签名。
- 遗留小问题（建议随修复一起处理）：`load()` 内 `lastMeta_ = meta`（`:111`/`:132`）未持 `compactionMutex_`，与注释声明的守卫范围不一致；`fullCompact(const IndexMetadata&)`/`flush(const IndexMetadata&)` 若被传入 `extra` 为空的 metadata 仍会覆盖 `lastMeta_`，建议在 `flush(metadata)` 中"仅当 `metadata.extra` 非空才覆盖"或合并而非替换。

### H2. `lastEventId` 从不初始化为当前系统事件 ID + 监控以 `SinceNow` 启动 → 有缓存仍全量重扫、扫描窗口变更丢失 ✅复核 ✅已修

- `FileSystemWatcher::lastEventId_{0}`（`FileSystemWatcher.h:80`）仅在**收到事件**时更新（`FileSystemWatcher.cpp:184`）；`start()` 使用 `kFSEventStreamEventIdSinceNow`（`:26`）且从不回填当前系统事件 ID。`getCurrentSystemEventId()`（`:56`）**定义了但无任何调用者**（全仓 grep 只有声明+定义）。
- 于是：一次"全量扫描 + 会话内无文件变更"的运行会持久化 `lastEventId=0`；下次启动 `ServiceEngine::startIncremental` 的门槛是 `if (lastEventId > 0 && loadedCount > 0)`（`ServiceEngine.cpp:505`）→ 条件不成立 → **跳过缓存快速路径，直接全量扫描**。索引是完整有效的，缓存被白白丢弃。
- 同一处的第二个后果：`startFullScan` 先扫描、扫完才 `startMonitoring()`（`ServiceEngine.cpp:426` → `:451-452`），而流从 `SinceNow` 开始，**扫描期间新建/修改的文件既不在扫描结果里，也不会产生事件**，会一直缺席到下次全量重建或对同目录的 rescan。
- 修复：扫描前记录 `getCurrentSystemEventId()`，扫描结束后从该水位启动 FSEvents，并把水位写入 metadata；增量启动只要求存在有效缓存，水位仅决定 replay 起点。

### H3. 搜索框的 Cmd+Backspace 被劫持为"把选中文件丢进废纸篓"，无确认、无撤销 ✅复核

- `HighlightedNSTextView.keyDown`（`MacEverything/App/HighlightedSearchField.swift:406-410`）把 `keyCode == 51 && .command` 解释为 `onCmdDelete`；`ContentView.swift:62-64` 直接接到 `viewModel.deleteSelectedFile()`；该方法（`SearchViewModel.swift:1180-1196`）立刻 `FileManager.trashItem`，无 `NSAlert`、无 `NSUndoManager`（全 `App/` 无相关调用）。
- 触发路径很自然：点选一条结果 → 按 Escape（`ContentView.swift:516-520` 把焦点移回搜索框且**保留结果选中**）→ 在主流行快捷键习惯下按 Cmd+Backspace 想清空输入行 → 文件被丢进废纸篓。这是 macOS 文本框的**标准"删除到行首"快捷键**。
- 影响：误删用户文件（废纸篓可恢复，故不列为 Critical）。建议：不在文本视图里拦截该组合键；删除必须二次确认并把 `trashItem` 返回的 URL 注册进 `UndoManager`。

### H4. 内容索引 rebuild/clear 在**未持有实例锁**的情况下删除共享缓存文件 ✅复核

- `requirements.md` §必须保持的系统约束 3 明确要求共享索引目录的修改必须先取得 instance lock。启动路径确实做了（`ServiceEngine::ensureInstanceLock`，`ServiceEngine.cpp:394/478/602`）。
- 但 `ServiceEngine::rebuildContentIndexOnMutationQueue`（`ServiceEngine+Content.cpp:255-323`）在**没有任何锁检查**的情况下 `fs::remove(cacheDir + "/content_index.bin")`、`content_index.wal` 及全部 `content_index.wal.seg.*`；`clearContentIndex()`（`:336-397`）同理。
- 可达性经确认成立：`contentIndex_` 在**构造函数**里就创建（`ServiceEngine.cpp:129`），因此"抢锁失败、`startupFatal_=true`"的第二个实例仍有非空 `safeContentIndex()`，`rebuildContentIndexOnMutationQueue` 的早期返回不会触发；而内容设置页的"重建/清空"按钮（`ContentSettingsView.swift:107`）并未按 `startupFailed` 禁用（UI 只在结果区/工具栏显示红色横幅）。
- 影响：实例 B 的一次点击会删除实例 A 正在使用的内容索引 base+WAL，A 的数据静默损坏/丢失。
- 建议：所有内容索引 mutator 进入时统一走 `ensureInstanceLock(config)` 门禁（或在持锁失败时拒绝执行并报错）。

### H5. 用户正则被 ICU `NSRegularExpression` 在主线程重新执行 → 灾难性回溯 / UI 卡死 ✅复核

- C++ 侧刻意选用 **RE2**（线性时间）执行用户正则；但高亮层 `TextHighlight.swift:243-252` 的 `.regex` 分支把**原始用户 pattern**（`hint.text` 来自 C++ AST，`HighlightHint.text`）交给 ICU 的 `NSRegularExpression`（`:247`），逐行渲染时在主线程执行。
- 触发条件可达：RE2 与 ICU 语义基本一致，因此**能进入结果列表的字符串通常是"能匹配"的**，但列表里同时会存在"该正则不匹配"的字符串而高亮仍会跑正则——例如 `regex:(a+)+b | cpp` 这类 OR 查询中仅由 `cpp` 命中的行、或被取反的 `!regex:...` 行。对这些行执行 `(a+)+b` 于长 `a` 串即指数级回溯，主线程冻结。
- 这是"引擎的线性时间安全属性没有被应用层继承"的典型例子。建议：高亮不要自建 ICU 正则，改为复用 C++ RE2 路径并按 hint 缓存编译结果（缓存 key 含 pattern+caseSensitive）；至少加上 pattern 长度/回溯特征白名单与执行上限。

### H6. debounce 重试 timer 可被安装在 `stopMonitoring()` **之后** → 资源泄漏 + 定时器持裸 `this` ✅复核（UAF 部分为条件性）

- `stopMonitoring()`（`ServiceEngine+FSEvents.cpp:150-173`）在 `pendingRescanMutex_` 下取消并释放 `rescanDebounceTimer_`、清空 `pendingRescanPaths_`；但它**没有先与 `mutationQueue_` 同步**（`dispatch_sync(mutationQueue_, …)` 发生在 `stopMonitoring()` 之后的 `ServiceEngine.cpp:1087`）。
- `flushPendingRescans()` 在同一把锁外计算完 `pathsToRescan/throttledPaths`（`:213-222`），并在末尾（`:258-273`）以 `if (!rescanDebounceTimer_)` 为条件**新建并 resume 一个重试 timer**。若此时 `stopMonitoring()` 已把指针置空，则该 timer 会在"监控已停止"之后被装上，且此后无人取消；其 handler 捕获的是裸 `ServiceEngine*`。
- 确定性后果：孤儿 timer 泄漏、`restartMonitoring()` 场景下出现重复 timer。条件性后果：若 `ServiceEngine` 在孤儿 timer 触发前被销毁（`~ServiceEngine`→`shutdown()`，`_serviceEngine` 在桥里是 `shared_ptr` 且从未 reset，故主要发生在进程退出/桥释放时），handler 会访问已析构对象。app 退出时进程通常立即结束，故未把它定为 Critical。
- 建议：`stopMonitoring()` 增加"已停止"标志（或把 pending 状态与 timer 的创建统一放到 `mutationQueue_` 上执行），重试 timer 安装前必须复查该标志；`stopMonitoring()` 在清空状态前先 `dispatch_sync(mutationQueue_, ^{})`。

### H7. FDA 检测轮询任务在取消后空转，占用主 actor ✅复核

- `PermissionView.swift:64-70`：`.task(id:)` 内 `while indexingMode.hasPrefix("full") && !hasFullDiskAccess { try? await Task.sleep(for: .seconds(3)); checkAccess() }`。
- 任务被取消时 `Task.sleep` 立刻抛出 `CancellationError`，被 `try?` 吞掉，循环体内**不检查 `Task.isCancelled`**，于是退化为忙循环；每次迭代 `checkAccess()`（`:77-84`）还会 `Task.detached` 起一个新的探测任务。可达场景：横幅显示期间关闭该搜索结果窗口（存在其他搜索窗口时被允许）。
- 建议：`do { try await Task.sleep(...) } catch { return }`，并在循环条件中加入 `!Task.isCancelled`。

### H8. App 自建的 `regex:` 前缀吞掉后续修饰符，`regex:case:<kw>` 静默搜错模式 ✅复核

- `SearchOptions.buildQuery`（`MacEverything/App/SearchOptions.swift:43-49`）按 `regex:` → `case:` → `ww:` → `wfn:` 顺序拼接；`isRegex` 与 `isCaseSensitive` 相互独立（`SearchOptions.swift:8-17`，开启 regex 不会清掉 case），故"正则 + 区分大小写"两个开关同时打开时产出 `regex:case:<kw>`。
- tokenizer 对 `regex` 采用**吞掉行尾**策略（`QueryTokenizer.h:110-113`：`arg = input.substr(argStart); i = len;`），于是整个 `case:<kw>` 成为正则本体，`case:` 修饰符被静默丢弃 → 搜索等价于"匹配字面量 `case:...`"，通常零结果。
- 同一根因还导致 `<regex:a>` 被解析为正则 `a>`（组永不闭合）、`regex:foo | bar` 无法 OR。建议：`regex` 参数在未转义的 `>`（组深度为 0 时）或 `|` 处终止，或强制引号形式 `regex:"pat"`；同时把 `case:` 之类修饰符放在 `regex:` **之前**输出。

---

## 三、Medium（中等）问题

| ID | 问题 | 位置 | 状态 |
|----|------|------|------|
| M1 | HTTP `/api/search/content` 使用恒真路径过滤器 `!fullPath.empty()`，忽略内容索引范围/排除配置 | `Core/HttpServer.cpp`、`ServiceEngine.cpp` | ✅已修 |
| M2 | 非 UTF-8 文件名、路径或摘要在 JSON/MCP/ObjC++ 边界产生非法文本或被静默丢弃 | `Core/HttpServer.cpp`、`CLI/mcp_main.mm`、`Bridge/MacSearchBridge_Internal.h` | ✅已修，统一替换为 U+FFFD |
| M3 | 解析器对前导操作符、游离分组和空引号处理不一致，可能静默零结果或生成空词条 | `Core/QueryParser.cpp` | ✅已修 |
| M4 | `/*` 根目录列表被误判为普通 glob | `Core/StructuredQueryParser.h` | ✅已修 |
| M5 | `compactRecords()` 在 Phase 2 未完成时重放新增记录，`appendPinyinInitialsForRecordUnlocked` 因 `phase2Pending_` 提前返回（`SearchEngine.cpp:149`），而 `pinyinInitialsPool_` 已被压缩重建为 `snapSize` 大小 → 这些记录**永久**（直到重启/重写）缺失拼音首字母检索；pool 与 `types_` 的长度不变量也被短暂破坏 | `SearchEngine.cpp:1117,1121` vs `:149`、`:1087`、`:1171` | ✅复核 |
| M6 | ~400 行结构化查询引擎（`queryStructured*`、`treeWalkDown`、`estimateTrigramCost`、`hasAdvancedSyntax`）无任何生产调用者——路由器只用 `queryDirList` 与 `queryAdvanced`（`SearchEngineQuery.cpp:342,393`）；而 `tests/test_structured_query.h`(773 行) 只测这条死路径，形成"绿灯但不覆盖真实执行路径" | `SearchEngineStructuredQuery.cpp:176` 等 | ✅复核 |
| M7 | 内容索引读取会跟随符号链接或阻塞于 FIFO | `Core/ContentIndex.cpp` | ✅已修 |
| M8 | 监控停止后 rescan 任务仍可能投递到已释放队列 | `Core/ServiceEngine+FSEvents.cpp` | ✅已修 |
| M9 | 内容设置页的运行时配置与持久化设置不同步 | `App/ContentSettingsView.swift`、`App/GeneralSettingsView.swift` | ✅已修 |
| M10 | 清除搜索历史后其他窗口仍保留内存中的旧条目 | `App/SearchHistoryStore.swift`、`App/AppSettings.swift` | ✅已修 |
| M11 | 内容查询不可取消，快速连续查询会叠加 | `Core/ContentIndex.cpp`、`Bridge/MacSearchBridge+Content.mm` | ✅已修 |
| M12 | 目录扫描的非权限错误被静默吞掉 | `Core/DirectoryScanner.cpp` | ✅已修 |
| M13 | 内容索引加载预算过于乐观，可能挤压应用其他内存 | `Core/ContentIndex.cpp` | ✅已修：预算 1/3 物理内存、上限 2 GB、每 trigram 32 B |
| M14 | **质量门禁覆盖不足**：`make test-fast`（提交前必跑）的分区集合不含 `5`（线程安全）、`22`（批量 rescan）、`23`（WAL 竞态）、`43`（事件驱动压缩）、`47`（路径 trigram）、`51`（SIMD）、`53`（v5 分页）等**正确性**分区；`--slow` 只跑 1/4/6。这些分区仅在 `make test-all` 出现，而 CI 中 `full-core-tests` 是 `schedule` 触发（每周、且跑在默认分支而非 PR） | `test_all.cpp:215,219,243`；`.github/workflows/build-macos.yml:104-125` | ✅复核 |
| M15 | 本地化 lint 存在盲区：它只能扫描字面量 key，看不到经 helper 参数传入的 key。实例：`FileInspectorPanel.swift:75` 的 `infoRow("Type", …)` 在 `en/zh-Hans Localizable.strings` **均无 `"Type"`** 键，中文界面里会显示英文 "Type"，而 `lint-localizations` 仍然通过 | `FileInspectorPanel.swift:75`；`scripts/check-localizations.sh` | ✅复核 |
| M16 | `scripts/check-docs.sh` 用 `rg` 却无可用性检查：未安装 ripgrep 时 `if rg …` 静默为假，README 占位链接检查被跳过，脚本仍打印 "Documentation checks passed"（本机即复现） | `scripts/check-docs.sh:6` | ✅复核 |
| M17 | 发布链路默认 ad-hoc，依赖下载无校验和，且没有公证入口 | `scripts/build-release-dmgs.sh`、`scripts/prepare-re2-deps-from-app.sh` | ✅已修：支持 Developer ID/公证环境变量、Hardened Runtime、SHA-256 校验 |

---

## 四、Low（轻微）问题汇总

- **查询/解析**：`compiledGlobMatch` 的 CONTAINS 分支把 haystack 截到 65535 字节（`uint16_t` 强转无意义），超长路径 glob 假阴性（`CompiledGlob.h:135`）；`toEpoch()` 把负 epoch 钳到 0，1970 前日期在非 UTC 时区失真（`QueryDateParser.h:59`）；`depth:abc`/`size:-5` 等畸形参数被当成合法值（`QueryDateParser.h:46`）；`last<N>years` 巨量 N 得到 match-all 区间（`:316`）；前缀删除 O(n) 全表扫描 + 每记录 WAL 写（`SearchEngine.cpp:648`）；排序无 tie-break、评分在 255 词饱和（`SearchEngineAdvancedQuery.cpp:1332`）；RE2 `max_mem` 未限制（`:174`）；`SearchEngine::query()` 内部无结果上限兜底（钳制只存在于 App/HTTP 层）。
- **扫描/内容**：getattrlistbulk 缓冲区游走缺防御性边界校验（当前布局经子代理按 man page 与实机 probe 验证**正确**，仅为加固建议）；每条目 `ATTR_CMN_ERROR` 未计数；`dispatch_semaphore_t` 泄漏 + `setEarlyAbortSemaphore` 无调用者；空/不可读文件会驱逐内容条目；modTime 秒级精度会漏同秒修改；WAL 打开时解析两遍、`kMaxWALSize` 仅告警；超 `PATH_MAX` 路径静默丢弃；rescan 去重 O(n²)；`kFSEventStreamEventIdsWrapped` 未处理（64 位 ID 回绕实际不可达）、未请求 `WatchRoot` 却处理 `RootChanged`（死分支）；`begin/endFileIndexRemap` 非异常安全（`bad_alloc` 会永久持锁）；内容索引事件重放在非 `mutationQueue_` 线程执行（当前时序下无并发对手，属加固项）。
- **HTTP/CLI**：无总请求截止时间，慢速客户端可长期占用 8 个 worker 之一（`HttpServer.cpp:520`）；MCP 对非法 JSON-RPC envelope 的 notification 仍回包（规范要求不回）；`mace`/MCP 无 `connect()` 超时（`SO_RCVTIMEO` 不等于连接超时，注释声称 3s）；**`scripts/prepare-re2-deps.sh:103,132` 未加引号的 `$(artifact_framework_dirs)` 遇到带空格路径会静默失败**。
- **App/Swift**：`cachedResults` 只写不读（最多驻留 100k 个 `MEFileResult`）；`httpPort`/`contentMaxFileSizeMB` 是唯一未在读取时钳制的数值；cooldown task 覆盖前不取消、旧 task 会把新引用置空；`tabBarRequestedWindows` 永不清理；全局热键注册失败仅记日志；`HighlightedSearchField.swift:238` 用 `Character` 数而非 UTF-16 长度恢复光标；`deinit` 不取消在飞查询；`ContentResultRow` 吞掉文件操作错误；导出路径列在"文件结果"下是父目录、"内容结果"下是完整路径；TXT 导出缺少 CSV 拥有的公式注入前缀（`SearchExportSerializer.swift:14`）；`AppDelegate` 迁移 `< 2` 会静默丢弃当时不可用的已配置根；内容扩展配置只存在于可被清理的 cache（`AppSettings.swift:292`）。
- **持久化加固**：`StringPool` 的 4GiB 上限缺失（`append` 的 `uint32_t offset` 会回绕、写入端 `bufSize` 强转 uint32_t 会截断）；`IndexWAL::isDirty()/clearDirty()` 中 `clearDirty` 无调用者（死代码）；`FlatIndexWriter` 多处 `ftell()` 未查 -1。

---

## 五、已验证的正确性（避免"审计只报坏消息"的偏差）

以下为本次**实际复核或复算**确认无误的高风险点：

- **HTTP 安全边界完整**：只绑定 `127.0.0.1`（`HttpServer.cpp:327`）；严格 Host 校验（拒绝重复/空/缺失，精确匹配 `127.0.0.1:port`/`localhost:port`，防 DNS rebinding，`:714-735`）；任何 `Origin` 一律 403（`:737-746`）；除 `/api/health` 外强制 Bearer，且 token 比较为定长常量时间（`:748-782`）；token 文件 `O_NOFOLLOW`+属主+`nlink==1`+0600；无文件服务路由；无 CORS 头。
- **InstanceLock**：`flock(LOCK_EX|LOCK_NB)` + 通过 fd `fstat` 校验属主/类型/nlink + `fchmod 0600`，无陈旧锁语义问题（`InstanceLock.cpp:14-77`）。
- **锁序无反转**：全局一致为 `ContentIndex::remapMutex_`（专有）→ `SearchEngine::mutex_` → `ContentIndex::mutex_`；`compactRecords()` 返回后才 `remapFileIndices()`，不产生嵌套；`rescanSubtree` 的 lease 在 `beginFileIndexRemap()` 之前已出作用域（`ServiceEngine+FSEvents.cpp:319-340`）。
- **持久化崩溃安全**：WAL 先落盘再改内存；轮转时"先开新段→换指针→重写 base→成功后才删旧段"，中途崩溃可由旧段重放；`removeWalSegmentsExcept()` 的默认 `keep={}` 语义确为"全删"（名字易误读但行为正确，`IndexPersistence.cpp:33-39`）；`fullRewriteGeneration/acknowledgeFullRewrite` 的"先读代际再快照"顺序是保守安全的（`IndexPersistence.cpp:311-316`）。
- **`flatWriter_->fullRewrite`**：tmp + `F_NOCACHE` + fsync + `rename` + 父目录 fsync；header CRC 覆盖 36 字节与注释/加载端一致；`sectionIdx` 数组边界、section 越界检查、`recordCount/liveCount` 上限校验齐备；`StringPool::compact()` 与 `data()/view()/str()/isLive()` 均已用 `entryInBounds()` 守卫（旧文档所称的越界读**已修复**）。
- **无危险 API**：全仓无 `strcpy/strcat/sprintf/gets/alloca`，无 `system/popen/posix_spawn`，Swift 侧 `Process` 均以参数数组调用，唯一 `try!`/`as!`/`.first!`（`ResultListKeyHandler.swift:168`）有 `!chars.isEmpty` 前置守卫。
- **测试有效性**：81 个测试分区、`test_all --fast` 实跑 1681 项全绿；Swift 侧 9 个单测目标已接入 `make test-fast`。

---

## 六、既有审计文档的现状核对（`docs/research/cross_check_findings.md` 等）

该文档记录的是**历史快照**，当前 `main` 中大部分已修复。逐条核对结果：

**已修复（文档已过期，建议加注或归档）：**
| 文档条目 | 当前状态 | 证据 |
|---|---|---|
| BUG-1 `StringPool::compact()` 越界读 | 已修 | `StringPool.h:65-68,186-201`（`entryInBounds` 守卫） |
| BUG-2 拼音丢扩展区 CJK（BMP 外） | 已修 | `StringUtils.cpp:81-122`（UTF-8→码点 + 代理对） |
| BUG-3 3.3% 汉字拼音 key 为空 | 已修 | `StringUtils.cpp:274-278` + `appendUtf8` 回退 |
| 多音字（重庆/长城）取音错误 | 已修 | `StringUtils.cpp:194-225` 多音词表（含"音乐/银行/调整"等） |
| C1 过滤参数吞掉尾部 `<`/`>` | 已修 | `QueryTokenizer.h:120-126` |
| C2 相对月/年月末漂移 + `tm_mday` 溢出 | 已修 | `QueryDateParser.h:297-309`（先归一月再钳日）+ `n>1e8` 上限 |
| C3 大小写敏感斜杠查询被小写化 | 已修 | `ASTStructuredTransform.h:57-72` |
| C4 开放式 size 区间损坏 | 已修 | `QueryFilterParser.h` RANGE 右空→`UINT64_MAX` |
| C5 取反的非法过滤器匹配全部 | 已修 | `SearchEngineAdvancedQuery.cpp:470-475`（`filterValid` 门禁） |
| C6 `type:` 参数未小写 | 已修 | `QueryFilterParser.h:32-33` |
| C8 引号短语失去字面语义 | 已修 | `ASTStructuredTransform.h:30`（`quoted` 提前返回） |
| D1 Stage-1b CJK 查询丢路径匹配 | 已修 | `SearchEngineAdvancedQuery.cpp:926-960`（并入 path-trigram 候选，注释直指该 bug） |
| D2 65537 字节关键词死循环 | 已修 | `ContentIndex.cpp:248-252`（`overlapSize >= bytesRead` 守卫） |
| D3 Phase-2 安装陈旧 trigram 竞态 | 已修 | `SearchEngineV6.cpp:233-239`（复查 `phase2Pending_`/`compactionGen_`） |
| W1 `StringPool::Entry` 未初始化 padding | 已修 | `StringPool.h:19-22`（显式 `padding = 0` + 注释） |
| W2 写入端无长度上限 | 已修 | `IndexWAL.cpp:192-201`、`ContentIndexPersistence.cpp:142-144,209-210` |
| W5 头部 CRC 注释错误 | 已修 | `FlatIndexWriter.cpp:295-298` 与实际 36 字节一致 |
| 日期比较语义注释与实现相反 | 已修 | 误导性注释已移除（`QueryDateParser.h:100-118`） |

**仍未修复（本次已并入上表）：** C7（tokenizer 无转义处理，`"a\"b"` 无法表达）、`extractByteTrigrams` 短/长串输出顺序不一致（当前唯一调用方不依赖顺序，信息级）、以及本报告 H2/M3/M4/M5/M6 等新发现。

**因此建议**：把 `cross_check_findings.md` 标注为历史文档（或迁移到 `docs/changelog/`），否则下一个维护者会重复排查已修复问题、并可能"按文档修坏代码"。

---

## 七、已排除的疑似问题（防止误报传播）

| 疑似问题 | 结论 | 依据 |
|---|---|---|
| "全量扫描后从不启动 FSEvents 监控" | **误** | `ServiceEngine.cpp:451-452` 扫完即 `startMonitoring()` |
| "ARC 下 ObjC++ 的 C++ ivar 析构函数不被调用" | **误** | 以 `-fobjc-arc -S` 编译 `MacSearchBridge.mm`，确认合成了 `.cxx_destruct` 并调用 `~shared_ptr<ServiceEngine>` |
| "`getattrlistbulk` 缓冲区布局错误" | **误** | 对照 man page 示例结构 + 对 `/` 实机 probe 一致 |
| "`devid != rootDev` 挂载边界判断会破坏 firmlink" | **误** | 本机 `/`、`/System/Volumes/Data`、`/Users`、`/Applications` 的 stat/getattrlistbulk devid 相同 |
| "内容扩展名配置从不持久化，重启即清空索引" | **误** | 扩展名存于 `content_index.bin`（`ContentIndex.cpp:747-752/815-826/981`），rebuild 与 clear 均保留 |
| "`/usr/bin/open` 参数注入" | **误** | 传入路径均为绝对路径，不会有以 `-` 开头的参数 |
| "CSV 公式注入未防护" | **误** | `csvField` 有 `=+-@\t\r` 前缀防护且测试覆盖（TXT 未防护，见 Low） |
| "`StringPool::compact` 越界读 / Stage-1b / Phase-2 竞态仍存在" | **误（已修）** | 见第六节 |

---

## 八、发布与合规检查（依 `AGENTS.md` 的个人信息审计要求）

- **提交身份**：历史快照曾记录 `Ying ZHANG` 使用个人邮箱。当前发布身份应使用 GitHub noreply 地址；公开仓库地址为 `git@github.com:ying-zhang/MacEverything.git`。
- **路径/凭据扫描**：`git grep` 的邮箱/本地路径命中全部为 `/Users/username`、`/Users/test`、`/home/user` 等匿名化样例与文档示例，未发现真实主目录路径、电话、token 或密钥；`artifacts/`、`third_party/re2/`、`build/` 均已在 `.gitignore`。
- **产物与文档一致性**：`requirements.md` 声明的结果上限（文件名 100–100,000 默认 10,000；内容 50–200 默认 200；历史 50/最大 200；GUI 分页 100/200）与 `AppSettings.swift:273/387-388/455-456`、`SearchViewModel.swift:561` 完全一致；README/README_EN 的拼音特性描述为"拼音索引"而非"全拼"，与实现（首字母）一致，未夸大。

---

## 九、建议的修复顺序

1. **立即（数据安全/用户可感）**：H3（Cmd+Backspace 删除）→ H7（FDA 忙循环）→ H4（内容索引绕锁删文件）。
2. **本轮版本（"每次启动全量重扫"真正闭环）**：H2 的水位初始化 + `startIncremental` 门槛放宽；确认 H1 的修复合并后，用"全量扫描→退出→再启动"的自动化用例断言不触发重扫（当前测试集**没有**该用例，建议补 `test_scan_eventid_watermark`）。
3. **随后**：H5（高亮走 RE2）、H6（timer 竞态）、H8（`regex:` 前缀/终止规则）、M3（前导操作符/空词条）。
4. **质量门禁**：把 `5/22/23/43/47/51/53` 并入 `test-fast`（或至少并入 PR CI 的第二个 job）；把 M16（`rg` 缺失静默通过）与 M15（本地化 key 经参数传入的盲区）修好，否则这两道 lint 是"假绿灯"。
5. **卫生项**：更新/归档 `cross_check_findings.md`；补签名与公证、固定 action 版本与依赖校验和。

---

### 附：方法与可复现性

- 四个分域子代理的原始明细报告（`swift-app.md`、`core-scan-fsevents.md`、`core-query-engine.md`、`bridge-http-cli-ci.md`）是本次审查的临时工作产物，其结论已按严重度与复核状态并入本报告，未单独保留。
- 复现测试：本机未安装 RE2，故从上游按应用内嵌库的精确版本取头文件（re2 `2025-11-05` + abseil `20260107.0`），并链接 `/Applications/Utilities/MacEverything.app/Contents/Frameworks` 内的 `libre2.11.0.0.dylib`：

  ```bash
  clang++ -std=c++20 -O2 -Wall -Wextra -Ithird_party/re2/include -IMacEverything/Core \
    -framework CoreServices test_all.cpp MacEverything/Core/*.cpp \
    -L"$FW" -lre2 -Wl,-rpath,"$FW" -o build/test_all && ./build/test_all --fast --quiet
  ```

  结果：`Tests passed: 1681 / Tests failed: 0`。若本机装有 Homebrew RE2，直接 `make test-fast` 即可。
