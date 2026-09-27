# 第二轮评审：codex 对 N1–N8 的修复

**评审对象**：工作树未提交改动（相对 `dc9cf79`），共 12 个改动文件 + 2 个新文件 + 1 个删除文件。
**评审时间**：2026-09-27
**评审方式**：逐条回读修复后源码 + 实跑测试/构建 + 独立探针验证。

本轮改动没有新提交，全部停留在工作树。`git diff --numstat` 记录：`TextHighlight.swift`、`HttpServer.{h,cpp}`、`PathUtils.h`、`ServiceEngine+FSEvents.cpp`、`ServiceEngine.{h,cpp}`、`tests/test_highlight_ranges.swift`、`tests/test_review_regressions.h` 及文档。

---

## 一、独立验证结果

| 项目 | 命令 | 结果 |
|---|---|---|
| 快速测试 | `./test_all --fast` | **1838 passed / 0 failed**（上一轮基线 1835，+3 为新回归检查） |
| 完整测试 | `./test_all` | **12299 passed / 0 failed**（上一轮 12296） |
| 分区 78 | `./test_all --part 78 --quiet` | 33 passed / 0 failed（与变更日志一致） |
| Swift 高亮 | `make test-swift-highlight` | 77 passed / 0 failed（上一轮 76） |
| 桥接 lint | `make lint-bridge` | 通过 |
| Core 告警 | 全部 `Core/*.cpp` `-Wall -Wextra -Wunused-function -fsyntax-only` | 零告警 |
| Release 构建 | `xcodebuild -scheme MacEverything -configuration Release build` | **exit 0，成功** |
| 文档 lint | `make lint-docs` | **本机失败**：`error: documentation checks require ripgrep (rg)`（`rg` 未安装；脚本现在会硬失败，属预期行为） |

`test_all` 二进制（12:24）晚于 `tests/test_review_regressions.h`（12:14），确认 1838 这个数字确实包含了新增回归检查，不是陈旧二进制。

---

## 二、逐条复核：N1–N8

### ✅ N1 已修（轮询路径）

`ServiceEngine+FSEvents.cpp:172-174` 现在先按 `isNetworkFilesystem` 过滤，只对网络根 `rescanSubtree`；`networkPollingInFlight_` + 每次轮询独立 `serial` + 引用计数 `remaining` 构成进行中保护。

关键正确性点：`rescanSubtree` 有 7 条 return 路径，本轮把 `notifyCompletion()` 补到了**每一条**（`ServiceEngine+FSEvents.cpp:351-465`）。这是必要的——否则任何一条提前返回都会让 `remaining` 永不归零、`networkPollingInFlight_` 永久为 `true`，轮询被静默关闭。我逐条走查确认无遗漏。

`stopNetworkPolling()` 先 `serial++` 再 `inFlight=false`（:196-198），使旧轮询的 `finish` 不会误清新轮询的状态；`stopMonitoring()`（:222）确实调用了它，`restartMonitoring()` 也经由 `stopMonitoring` 重建定时器。逻辑自洽。

**但同一缺陷在另一条路径上仍然存在 → 见 R1。**

### ✅ N2 已修（静默空索引 / 落盘空索引）

- `ServiceEngine.cpp:425-431`：不完整扫描改为 `onStartupFailed(reason)` + `completion(0, false)` 后返回，不再进入持久化。
- `ServiceEngine.cpp:554-563`：`startIncremental` 的无缓存回退新增 `if (!didFullScan) { completion(0, false); return; }`，不再围绕缺失的 engine 构造 persistence。
- 新增回归测试 `tests/test_review_regressions.h:16-62`：断言「跑到完成回调 + 触发 startupFailed + engine 未被替换 + `index.v6` 未被写入」。该测试在修复前的代码上必然失败（旧代码会写空索引且不报错），是有效的回归护栏。

**但失败呈现方式与 rebuild 语义有问题 → 见 R2、R3。**

### ✅ N3 已加固（网络文件系统识别）

`PathUtils.h:42-57` 新增 mount table 回退：`statfs` 失败时用 `getmntinfo(MNT_NOWAIT)` 找最长前缀挂载点。`pathContainsOrEquals(mountPoint, path)` 的参数顺序正确（mountPoint 为父），`length < bestMountLength` 的取舍正确取最长匹配。

需要说明两点：
- 该回退只在 `statfs` **失败**时生效。若配置根本身是未挂载的网络卷（`/Volumes/NAS` 不存在），仍判定为本地——但此时没有网络 I/O 可拖慢，挂载后 `statfs` 即成功，无实际危害。
- 我**无法在本机复现上一轮 N3 的前提**（"`statfs` 看到的是本地 `/Volumes` 文件系统"）——本机没有 SMB/NFS 挂载点。因此我只能确认新代码本身正确，不能确认旧代码确实误判。这条建议在真实 NAS 上复测。

### ✅ N4 已修（监控重启保留水位）

`ServiceEngine+FSEvents.cpp:208-210`：先读 `watcher_->getLastEventId()`，再 `stopMonitoring()`，再以该水位启动，为 0 时才回退 `SinceNow`。读在 stop 之前是必须的，写法正确。`FileSystemWatcher::stop()`（`FileSystemWatcher.cpp:140-151`）同步清空 `stream_`，因此随后的 `start()` 不会被 `if (stream_) return true;` 短路。

**没有测试覆盖 → 见 R5。**

### ✅ N6 已修（HTTP 内容查询可取消）

`HttpServer.cpp:597-600` 引入每请求 30s deadline；`handleContentSearch` 构造 `shouldCancel`（:939-952）并传入 `ContentIndex::query`（:960-965），在批次边界（`ContentIndex.cpp:688`）与逐候选（:700）两处检查，同时在 3 次重试循环首尾各检查一次（:954、:966），返回 408。

并发安全我单独确认了：`HttpServer::stop()`（:406）对活动连接只做 `::shutdown(fd, SHUT_RDWR)`，**从不 `close()`**，`close` 只由持有该 fd 的 worker 线程在 `handleConnection` 末尾执行。所以 `recv(MSG_PEEK|MSG_DONTWAIT)` 不会撞上 fd 复用；`dispatch_apply` 多线程并发 peek 同一 fd 也是安全的（PEEK 不消费数据）。这一处设计是干净的。

### ✅ 高亮守卫（N8 的一半）——并更正我上一轮的一个错误结论

**更正**：我上一轮报告称 `isSafeHighlightRegex` "仍然放行了 `(a|ab)+`"。这是**错的**。`hasAlternation` 逻辑在 `dc9cf79` 就存在（`git show HEAD:MacEverything/App/TextHighlight.swift` 第 225/300/315/321 行），本轮 `git diff` 只改了那 4 行文档注释，逻辑一字未动。探针实测（`computeRangesForHint`）`(a|ab)+`、`(a|aa)+` 均返回 0 区间，即已被拒绝。

上一轮结论的后半句（过度拒绝）成立且本轮**被测试固化** → 见 R6。

### ◻ N7 未被处理，但被明确声明为设计约束

新文档 `main-branch-review.md` 把 `regex:` 吞掉剩余文本列为"已确认的设计约束"，不再算回归。这是可接受的收敛（GUI 侧的 `regex:case:kw` 已在上一轮修掉，`SearchOptions.swift:49-53` 现在输出 `(?mace:...)` 标记形式）。残留：用户或 CLI/MCP 直接输入 `regex:case:foo` 仍会被当作字面量正则，静默零结果。若认为这属于"已知限制"，建议在 `SearchSyntaxHelpView` 的用户可见文案里也写清楚，而不只是放在研究文档里。

### ❌ N5 未处理

`tests/` 中仍然没有任何 `test_scan_eventid_watermark` 类测试。全仓 `tests/*.h` 里 `lastEventId` 只出现在持久化往返断言（`test_flat_persistence_v6.h` 等），没有任何测试覆盖"全量扫描 → 水位写入 → 重启不触发全量重扫"或"`restartMonitoring` 保留水位"。本轮新增的 N4 修复同样零覆盖。**见 R5。**

---

## 三、剩余与新增问题

### R1（Medium）N1 的同一缺陷在 `backgroundSyncEngine` 里完好保留

`ServiceEngine.cpp:753-773`：

```cpp
if (hasNetworkRoot) {
    LOG_INFO(..., "Network root detected; skipping FSEvents replay and using polling reconciliation");
    if (config.realtimeMonitoring) this->startMonitoring();   // :757 用 SinceNow 默认值
    for (const auto& root : roots) {                          // :759 遍历「全部」根
        this->rescanSubtree(root);
    }
```

进入这个分支的条件是"**至少一个**根是网络根"，但循环体遍历的是**全部**根。于是：只要配置里有一个 NAS 根，每次启动都会对本地全部索引根（默认含 `~/Desktop`、`~/Documents`…）做一次完整子树重建——全量扫描 + `removeByPathPrefix` 清内容索引 + 逐文件重做内容索引 + 墓碑超 30% 时 `compactRecords()`。这正是我上一轮 N1 指责的模式，只是频率从每 60 秒变成每次启动。

同时 `startMonitoring()` 用 `SinceNow`（:757），本地根因此没有 FSEvents replay；正确性目前只靠这次全量清扫兜住——代价是全量成本。

**修复应与 N1 一致**：循环加 `if (PathUtils::isNetworkFilesystem(root))`，本地根继续走 replay 路径。

### R2（Medium）扫描不完整的失败被塞进了"实例锁"UI，诊断文案与恢复路径都是错的

`onStartupFailed` 的语义在本轮被扩大（`ServiceEngine.h:121-124` 的注释已改），但 UI 没有跟着改：

- `ContentView.swift:537-541`（工具栏）：`"Service unavailable — another instance holds the index lock"`
- `ContentView.swift:145-147`（整屏）：`"This usually means another copy of MacEverything is already running with the same index. Quit the other instance and restart, or restart your Mac."`
- `SearchViewModel.localizedStartupFailureReason`（:177-185）对 `"Full scan incomplete: N root or traversal error(s)"` 落到底部分支，返回"MacEverything could not access the index. Check folder permissions and available disk space."

结果：用户只是没插外置盘/没连 NAS，却被告知"另一个实例持有索引锁"，而且同一屏上两句解释互相矛盾。

恢复路径也粘住：`SearchViewModel.rebuildIndex()`（:328-350）**不重置** `startupFailed`，而 `startIncremental()`（:233-234）才重置。因此一次 rebuild 撞上不完整扫描后，即使之后扫描成功，`startupFailed` 仍为 `true`，`ContentView` 的 `if service.startupFailed` 分支优先于 `else if service.isScanning`，界面会一直停在失败页，只有点屏上的 Retry（→`startIncremental()`）或重启才能脱出。

建议：给失败引入种类（锁 / 扫描不完整），或在 reason 里带上具体不可达根，UI 按种类显示；`rebuildIndex()` 开头也重置失败态。

### R3（Medium）rebuild 会先删掉旧索引，再因为一个不可达根中止

`ServiceEngine::rebuildIndex`（:599-680）的执行顺序是：替换为**空 engine**（:631）→ `removeIndexFiles(config)`（:664）→ `startIncremental`（:676）→ 无缓存 → `startFullScan` → 只要任一根 `stat` 失败就整体中止。

因此"用户点了 Rebuild Index，恰好一个可选的 NAS/外置盘没挂载"的后果是：旧索引已被删除、内存 engine 已空、新索引因中止而未写入、界面进入 R2 的错误失败页。这比修复前更糟——修复前至少不会报错并写了空索引（同样是坏的，但语义一致）。

更根本地：**N2 的"全有或全无"扫描策略并未改变**。`isComplete()` 要求 `rootFailureCount_ == 0`，基线 `f233a52` 是跳过坏根、用其余根建部分索引。现在的取舍是"宁可整体失败也不给部分结果"，并已写进新文档（"`DirectoryScanner::isComplete()` 对请求根不可达保持失败语义"）。这是个可以辩护的决定，但请明确二选一：

1. 保持全有或全无，但 rebuild 必须在**删除索引之前**验证所有根可达；或
2. 区分"单根 `stat` 失败"（可跳过，提交部分结果，并在 UI 列出不可达根）与"遍历错误/锁失败"（整体失败）。

现状是两者的最坏组合：策略是全有或全无，而执行顺序保证了损失不可逆。

### R4（Medium，文档）被删除的审查历史声称"已合并"，实际是丢失

`docs/research/cross_check_findings.md`（94 行）被删除，`main-branch-review.md` 从 209 行缩到 58 行，`docs/research/README.md` 结尾写：

> 旧的交叉检验快照和临时审查报告已合并到当前审查文档，避免同一问题在多个文件中出现不同状态。

但 58 行的新文档里**没有**任何被合并的内容：H1–H8 明细、M1–M17 表、Low 清单、"已验证正确性"清单、`cross_check_findings` 的逐条状态表（18 项历史 bug 已修的证据）、"已排除的疑似问题"表（防止误报复燃）、以及发布/个人信息审计章节，全部消失。

净效果恰好是旧文档 §6 警告的场景：下一个维护者会重复排查已修问题，或"照旧笔记把代码改坏"。二选一：真正把这些表并进新文档（加一节"历史结论"），或者在 README 里如实写"已删除，历史见 git 记录"。顺带：确实没有其它文件还引用 `cross_check_findings.md`（已 grep 确认），链接层面是干净的。

### R5（Low）六个修复里有三个没有任何回归覆盖

- 网络轮询的根过滤 + 进行中保护：`tests/` 中 grep 不到 `networkPolling` / `isNetworkFilesystem`。
- `restartMonitoring` 水位：无测试（N5 的原始要求）。
- HTTP 取消 / 408：`tests/` 中 grep 不到 `408` / `cancelled` / `shouldCancel`。

变更日志《验证》一节只列了 `--part 78` + Swift 高亮 + 两个 lint，措辞本身没有夸大，但这三项确实是"改了、没护栏"。

### R6（Low）新增的高亮测试有一条是空转的，而过度拒绝被固化

`tests/test_highlight_ranges.swift:235-244` 的拒绝列表是 `["(a|ab)+", "(a|aa)+", "(foo|bar)+"]`，文本统一是 `String(repeating: "a", count: 64) + "!"`。`(foo|bar)+` 在这种文本上**本来就不匹配**，无论守卫是否触发都会得到 0 区间——这条断言是空转的。真正有效的只有 `(a|ab)+` / `(a|aa)+`。

同一守卫的过度拒绝依然存在（探针实测当前代码，`computeRangesForHint` 返回区间数）：

| pattern | 文本 | 区间数 | 备注 |
|---|---|---|---|
| `(a\|ab)+` | `aaaa…!` | 0 | 灾难性，正确拒绝 |
| `(a\|aa)+` | `aaaa…!` | 0 | 灾难性，正确拒绝 |
| `(foo\|bar)+` | `foobarfoo` | **0** | 安全（<3ms），被误拒 |
| `(ab\|cd)+` | `abcdabcd` | **0** | 安全，被误拒 |
| `(a\|b)+` | `ababab` | **0** | 安全，被误拒 |
| `(ab+)+` | `abbbbb` | **0** | 安全，被误拒 |
| `\d{4}` / `(ab)+` / `a+b` | — | 1 | 对照，正常 |

即"重复交替"被一刀切拒绝，而重复交替本身只有在分支**前缀歧义**时（`a` vs `ab`）才是灾难源。新注释把"repeated alternation"整体描述为灾难性回溯的常见来源，并把 `(foo|bar)+` 写进期望拒绝集，等于把一个过度保守的行为固化为规范。合理做法：把 `(foo|bar)+` 换成有意义的安全用例（或从断言里删掉），并保留"长期改用 RE2 匹配结果"的结论——新文档已经诚实地写了这一点，值得保留。

### R7（Low）把对端半关闭当作"取消"

`shouldCancel`（`HttpServer.cpp:947`）在 `recv()==0` 时返回 `true`。HTTP 客户端在发完请求后 `shutdown(SHUT_WR)` 是合法用法，而 macOS 上 `recv` 无法区分"半关闭"与"已关闭"，这类客户端会拿到 408 而不是结果。另外取消探针是**逐候选**调用（`ContentIndex.cpp:700`），每次一个 `recv` + 一次 `steady_clock::now()`，候选集很大时是每候选一次系统调用。两者都不严重，但值得记一笔。

### R8（Low）轮询完成回调经主队列回到裸 `this`

本轮把 `notifyCompletion()` 补到了 `rescanSubtree` 的每条返回路径（正确），但它统一 `dispatch_async(dispatch_get_main_queue(), ...)`，而轮询的 `finish` lambda 捕获裸 `this` 并读写 `networkPollingSerial_` / `networkPollingInFlight_`。`ServiceEngine::shutdown()` 只 `dispatch_sync(mutationQueue_)` 排空 mutation 队列，**不保证**主队列上残留的 block 在 engine 析构前执行完。概率低（主要发生在退出路径，进程随即结束），但既然这一轮把该回调从"仅成功路径"扩展到"全部路径"，风险面确实变大了。建议 completion 携带 `std::weak_ptr<ServiceEngine>`（`safeEngine()` 已有）或一个与 `this` 无关的共享状态对象。

### R9（Nit）变更日志的 `make lint-docs` 结论在本机不可复现

`scripts/check-docs.sh:6-9` 现在无 `rg` 时 `exit 69`，本机 `which rg` 为空，因此 `make lint-docs` 与 `make test-fast` 本机都会失败。这是 M16 想要的行为（假绿灯改真红灯），不算缺陷，但新文档"验证命令"一节的 `make lint-docs` 应标注前置条件（`brew install ripgrep`），否则贡献者按文档跑会得到失败。

---

## 四、结论

本轮修复的**质量明显高于上一轮**：N1 的进行中保护连同"所有提前返回都要通知完成"这一必要条件一起做对了；N2 的修复有真实可失败的新回归测试兜底；N3/N4/N6 的并发与生命周期细节（`stop()` 只 shutdown 不 close、读水位必须在 stop 之前、`watcher_->stop()` 同步清 `stream_`）都经我逐条回读确认无误；测试与构建我全部复跑通过。

需要继续处理的，按优先级：

1. **R3 + R2**：先修 rebuild 的顺序问题（删索引前校验根可达），再把"扫描不完整"从实例锁 UI 里分离出来，并让 `rebuildIndex()` 重置失败态。这是当前唯一会**不可逆损坏用户索引并给出错误诊断**的路径。
2. **R1**：把 N1 的根过滤补到 `backgroundSyncEngine:759-761`，本地根回到 replay 路径。
3. **R4**：补回被删掉的历史结论表，或如实说明已删除。
4. **R5**：补三个缺失的护栏（轮询过滤/进行中、`restartMonitoring` 水位、HTTP 取消）。
5. **R6/R7/R8/R9**：高亮用例空转 + 过度拒绝固化、半关闭误判、裸 `this` 回调、`rg` 前置条件。

另需更正我上一轮的一处误判：`isSafeHighlightRegex` 在 `dc9cf79` 就已经拒绝 `(a|ab)+`，本轮只改注释，我上一轮"仍然放行"的结论不成立。
