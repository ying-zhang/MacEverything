# MacEverything 变更日志

项目从 2026-04-14 开始开发，以下按时间顺序记录所有重要变更。

> **注意：** 编号 058、060、097、106、107、113、117 各有两个条目，源于并行分支开发同时产出。文件名保留原样以维护 Git 历史引用。

## 变更索引

| # | 类型 | 标题 | 日期 |
|---|------|------|------|
| [001](001-测试框架模块化拆分.md) | refactor | 测试框架模块化拆分 | 2026-04-14 |
| [002](002-Core层高严重度Bug修复-C1-C5.md) | bugfix | Core 层高严重度 Bug 修复 (C1-C5) | 2026-04-14 |
| [003](003-Core层中严重度问题修复-Phase2.md) | bugfix | Core 层中严重度问题修复 (Phase 2) | 2026-04-14 |
| [004](004-Bridge层问题修复-B1-B6.md) | bugfix | Bridge 层问题修复 (B1-B6) | 2026-04-14 |
| [005](005-App层问题修复-A1-A8.md) | bugfix | App 层问题修复 (A1-A8) | 2026-04-14 |
| [006](006-12项性能优化.md) | performance | 12 项性能优化（Trigram 索引等） | 2026-04-14 |
| [007](007-Bridge层编译错误与lint检查.md) | bugfix | Bridge 层编译错误与 lint 检查 | 2026-04-14 |
| [008](008-启动挂起防御.md) | bugfix | 启动挂起防御 | 2026-04-14 |
| [009](009-大文件拆分与测试重组.md) | refactor | 大文件拆分与测试重组 | 2026-04-14 |
| [010](010-搜索延迟-recentIndices增量缓存.md) | bugfix | 搜索延迟：recentIndices 增量缓存 | 2026-04-14 |
| [011](011-最近文件列表橙色背景.md) | feature | 最近文件列表橙色背景 | 2026-04-14 |
| [012](012-搜索延迟-查询取消修复.md) | bugfix | 搜索延迟：查询取消修复 | 2026-04-14 |
| [013](013-10个CRITICAL与HIGH级Bug修复.md) | bugfix | 10 个 CRITICAL/HIGH 级 Bug 修复 | 2026-04-14 |
| [014](014-CRITICAL与HIGH并发安全与性能修复.md) | bugfix | CRITICAL/HIGH 并发安全与性能修复 | 2026-04-14 |
| [015](015-最近文件标签徽章样式.md) | feature | 最近文件标签徽章样式 | 2026-04-14 |
| [016](016-搜索栏蓝色边框与字号调整.md) | feature | 搜索栏蓝色边框与字号调整 | 2026-04-14 |
| [017](017-CRITICAL与HIGH第二轮修复.md) | bugfix | CRITICAL/HIGH 第二轮修复 | 2026-04-14 |
| [018](018-快速输入压力测试.md) | test | 快速输入压力测试 | 2026-04-14 |
| [019](019-内存优化550MB.md) | performance | 内存优化：节省约 550 MB | 2026-04-14 |
| [020](020-空闲CPU100%修复.md) | bugfix | 空闲 CPU 100% 修复 | 2026-04-14 |
| [021](021-焦点感知索引刷新节流.md) | bugfix | 焦点感知索引刷新节流 | 2026-04-14 |
| [022](022-索引刷新节流状态机.md) | feature | 索引刷新节流状态机 | 2026-04-14 |
| [023](023-非焦点时跳过刷新.md) | bugfix | 非焦点时跳过刷新 | 2026-04-14 |
| [024](024-移除废弃测试代码.md) | refactor | 移除废弃测试代码 (~970 行) | 2026-04-14 |
| [025](025-信号量累积竞态.md) | bugfix | 信号量累积竞态 | 2026-04-14 |
| [026](026-nil字符串崩溃.md) | bugfix | nil 字符串崩溃 | 2026-04-14 |
| [027](027-移除废弃recordAtIndex接口.md) | refactor | 移除废弃 recordAtIndex 接口 | 2026-04-14 |
| [028](028-rebuildIndex重置内容搜索状态.md) | bugfix | rebuildIndex 重置内容搜索状态 | 2026-04-14 |
| [029](029-窗口查找改用标题匹配.md) | bugfix | 窗口查找改用标题匹配 | 2026-04-14 |
| [030](030-提取共享工具函数.md) | refactor | 提取共享工具函数 | 2026-04-14 |
| [031](031-P2级别批量修复.md) | bugfix | P2 级别批量修复 | 2026-04-14 |
| [032](032-PathTable线程安全与WAL互斥锁.md) | bugfix | PathTable 线程安全与 WAL 互斥锁 | 2026-04-15 |
| [033](033-rebuildIndex任务取消与nil检查.md) | bugfix | rebuildIndex 任务取消与 nil 检查 | 2026-04-15 |
| [034](034-焦点变更冷却与safeEngine修复.md) | bugfix | 焦点变更冷却与 safeEngine 修复 | 2026-04-15 |
| [035](035-代码简化与P2批量修复与LazyVStack.md) | bugfix | 代码简化、P2 批量修复与 LazyVStack | 2026-04-15 |
| [036](036-统一日志系统.md) | feature | 统一日志系统 | 2026-04-15 |
| [037](037-文件拖放支持.md) | feature | 文件拖放支持 | 2026-04-15 |
| [038](038-查询与日志与WAL性能优化.md) | performance | 查询/日志/WAL 性能优化 | 2026-04-15 |
| [039](039-Bridge信号量与LazyVStack修复v2.md) | bugfix | Bridge 信号量与 LazyVStack 修复 v2 | 2026-04-15 |
| [040](040-增量trigram更新.md) | performance | 增量 Trigram 更新 | 2026-04-15 |
| [041](041-单实例文件锁与WAL路径修复.md) | bugfix | 单实例文件锁与 WAL 路径修复 | 2026-04-15 |
| [042](042-WAL批量回放优化.md) | performance | WAL 批量回放优化 | 2026-04-15 |
| [043](043-两阶段即时启动.md) | feature | 两阶段即时启动 | 2026-04-15 |
| [044](044-WAL-rename失败链修复.md) | bugfix | WAL rename 失败链修复 | 2026-04-15 |
| [045](045-重扫防抖与节流.md) | bugfix | 重扫防抖与节流 | 2026-04-15 |
| [046](046-Compaction-FSEvents正反馈循环.md) | bugfix | Compaction-FSEvents 正反馈循环 | 2026-04-15 |
| [047](047-Logger刷新缺陷.md) | bugfix | Logger 刷新缺陷 | 2026-04-15 |
| [048](048-搜索栏幽灵文本自动补全.md) | feature | 搜索栏幽灵文本自动补全 | 2026-04-15 |
| [049](049-跳过空压缩.md) | performance | 跳过空压缩（dirty 标志） | 2026-04-15 |
| [050](050-COW无阻塞压缩.md) | performance | COW 无阻塞压缩 | 2026-04-15 |
| [051](051-压缩阈值与F_NOCACHE.md) | bugfix | 压缩阈值与 F_NOCACHE | 2026-04-15 |
| [052](052-分页增量持久化.md) | performance | 分页增量持久化 | 2026-04-15 |
| [053](053-unify-persistence-infrastructure.md) | refactor | 统一持久化基础设施 | 2026-04-16 |
| [054](054-fswatcher-log-clarity.md) | refactor | FSWatcher 日志可读性改进 | 2026-04-16 |
| [055](055-fix-content-compaction-waste.md) | bugfix | 修复 Content Compaction 全量无效问题 | 2026-04-16 |
| [056](056-embedded-http-server.md) | feature | Embedded HTTP Server | 2026-04-16 |
| [057](057-xcuitest-target.md) | test | XCUITest Target | 2026-04-16 |
| [058](058-http-admin-api.md) | feature | HTTP Admin API Extensions | 2026-04-16 |
| [058](058-fix-exit-content-compact.md) | bugfix | 退出时强制 Content Index Compact | 2026-04-16 |
| [059](059-fix-force-compact-dirty.md) | bugfix | Fix force-compact skip logic | 2026-04-16 |
| [060](060-fix-lasteventid-zero-cycle.md) | bugfix | Fix FSEvents lastEventId=0 恶性循环 | 2026-04-16 |
| [060](060-service-engine-cli-daemon.md) | feature | ServiceEngine 提取 + CLI Daemon | 2026-04-16 |
| [061](061-startup-optimization.md) | performance | 启动优化：增量内容索引 + 延迟 HttpServer | 2026-04-16 |
| [062](062-content-modtime-skip.md) | performance | 内容索引 modTime 跳过优化 | 2026-04-16 |
| [063](063-fix-content-prune-persistence.md) | bugfix | 修复内容索引 prune 后状态未持久化 | 2026-04-16 |
| [064](064-minimized-launch.md) | feature | 支持启动后最小化 | 2026-04-16 |
| [065](065-inplace-wal-replay-incremental-content.md) | performance | In-place WAL Replay + Incremental Content Indexing | 2026-04-16 |
| [066](066-event-driven-compaction-split-timing.md) | feature | Event-Driven Content Compaction + Split Startup Timing | 2026-04-16 |
| [067](067-query-perf-optimization.md) | performance | 查询性能优化 | 2026-04-17 |
| [068](068-fix-record-dedup-gap.md) | bugfix | Fix record deduplication — eliminate search result gaps | 2026-04-17 |
| [069](069-10m-query-perf-bench.md) | performance | 10M 记录查询性能基准 | 2026-04-17 |
| [070](070-remove-layer1-bypass.md) | refactor | 去掉 Layer1 Adaptive Trigram Bypass | 2026-04-17 |
| [071](071-path-trigram-index.md) | feature | Path Trigram Index | 2026-04-17 |
| [072](072-slash-query-port-retry.md) | feature | 含 `/` 查询 Trigram 加速 + HTTP 端口重试 | 2026-04-17 |
| [073](073-cmd-click-reveal-finder.md) | feature | Cmd+Click to Reveal in Finder | 2026-04-17 |
| [074](074-mcp-server.md) | feature | MCP Server (Model Context Protocol) | 2026-04-17 |
| [075](075-abspath-trigram-logfix.md) | bugfix | 绝对路径查询 Trigram 加速 + 日志修复 | 2026-04-17 |
| [076](076-abspath-zero-results-fix.md) | bugfix | 绝对路径查询返回 0 结果修复 | 2026-04-17 |
| [077](077-scanner-autofs-and-path-supplement.md) | bugfix | Scanner autofs 挂起修复 + 路径查询补全 | 2026-04-17 |
| [078](078-audit-fixes-tier1-tier2.md) | bugfix | 项目审计修复（Tier 1 + Tier 2） | 2026-04-18 |
| [079](079-mcp-menu.md) | feature | MCP Integration Menu | 2026-04-18 |
| [080](080-mcp-app-menu.md) | feature | MCP Integration in App Menu Bar | 2026-04-18 |
| [081](081-graceful-shutdown.md) | bugfix | Fix Graceful Shutdown: Paged Index Flush on Quit | 2026-04-18 |
| [082](082-string-search-benchmark.md) | test | 5GB 内存字符串搜索性能基准测试 | 2026-04-18 |
| [083](083-log-field-naming.md) | refactor | 统一日志字段命名 + tombstone 回收日志 | 2026-04-18 |
| [084](084-contiguous-memory-simd.md) | performance | Contiguous Memory Layout + NEON SIMD Search | 2026-04-18 |
| [085](085-competitive-analysis.md) | docs | 同类文件搜索产品竞品分析 | 2026-04-18 |
| [086](086-fix-nanov2-heap-crash.md) | bugfix | Fix nanov2 Heap Corruption Crash | 2026-04-18 |
| [087](087-query-timing-api.md) | feature | Detailed Query Timing Breakdown (HTTP API) | 2026-04-18 |
| [088](088-fix-crossboundary-highlight.md) | bugfix | Fix Cross-Boundary Keyword Highlighting | 2026-04-18 |
| [089](089-trigram-split-zero-alloc.md) | performance | Trigram-Split 路径零分配优化 | 2026-04-18 |
| [090](090-trigram-fallback-threshold.md) | performance | Trigram 回退阈值调优 | 2026-04-18 |
| [091](091-lower-path-pool.md) | performance | Lower Path Pool 优化 | 2026-04-18 |
| [092](092-v5-path-dict-namepool.md) | performance | V5 Path Dict + NamePool | 2026-04-18 |
| [093](093-refactor-query-complexity.md) | refactor | 查询复杂度重构 | 2026-04-18 |
| [094](094-http-search-benchmark.md) | test | HTTP 搜索性能基准测试工具 | 2026-04-18 |
| [095](095-glob-search-optimization.md) | performance | Glob 搜索优化：预编译 + Trigram 预过滤 | 2026-04-18 |
| [096](096-glob-multi-segment-trigram.md) | performance | 复合 Glob 多段 Trigram 预过滤优化 | 2026-04-18 |
| [097](097-phase1-timestamp-fix.md) | bugfix | 修复 Slash 查询 phase1 时间戳为负数 | 2026-04-18 |
| [097](097-slash-trigram-split-fix.md) | bugfix | 修复 Slash 查询始终走 linear scan | 2026-04-18 |
| [098](098-searchengine-split.md) | refactor | SearchEngine.cpp 拆分为 3 个文件 | 2026-04-18 |
| [099](099-auto-focus-search-field.md) | feature | 窗口激活时自动聚焦搜索框 | 2026-04-19 |
| [100](100-query-parser-phase1.md) | feature | Query Parser with Boolean Operators | 2026-04-19 |
| [101](101-query-filters-phase2.md) | feature | Core Query Filters | 2026-04-19 |
| [102](102-date-filters-phase3.md) | feature | Date Filters (dm:, dc:, da:) | 2026-04-19 |
| [103](103-node-centric-structured-query.md) | refactor | Node-Centric Structured Query System | 2026-04-19 |
| [104](104-structured-query-perf-report.md) | performance | Structured Query Performance Report | 2026-04-19 |
| [105](105-anchor-selection-optimization.md) | performance | Anchor-Selection Optimization for Structured Queries | 2026-04-19 |
| [106](106-fix-phase1ms-negative.md) | bugfix | Fix phase1Ms negative values | 2026-04-19 |
| [106](106-modifiers-macros-phase4.md) | feature | Phase 4: 修饰符和宏 (Modifiers & Macros) | 2026-04-19 |
| [107](107-glob-highlight.md) | feature | Glob 查询结果高亮非通配符部分 | 2026-04-19 |
| [107](107-linear-scan-optimizations.md) | performance | Structured Query Linear Scan Optimizations | 2026-04-19 |
| [108](108-regex-trigram-prefilter.md) | feature | Regex Trigram Pre-filtering | 2026-04-19 |
| [109](109-advanced-query-scoring-fix.md) | bugfix | 高级查询评分修复 | 2026-04-19 |
| [110](110-fsevents-batch-mutation.md) | performance | FSEvents Batch Mutation | 2026-04-19 |
| [111](111-search-menu-options.md) | feature | Search 顶级菜单：搜索选项 Toggle | 2026-04-19 |
| [112](112-buffer-scan-optimization.md) | performance | Buffer-Scan & Path-Trigram Optimization | 2026-04-19 |
| [113](113-soa-simd-parallel.md) | performance | SoA 列式存储 + SIMD 批量过滤 + GCD 并行化 | 2026-04-19 |
| [113](113-search-bar-syntax-highlight.md) | feature | Search Bar Syntax Highlighting | 2026-04-19 |
| [114](114-result-keyword-highlight-fix.md) | bugfix | Fix Search Result Keyword Highlighting | 2026-04-19 |
| [115](115-ghost-text-alignment-fix.md) | bugfix | Fix ghost text misalignment | 2026-04-19 |
| [116](116-unified-query-paths.md) | refactor | 整合结构化查询与高级查询路径 | 2026-04-19 |
| [117](117-system-keyword-ghost-suggestions.md) | feature | System keyword ghost suggestions | 2026-04-19 |
| [117](117-tilde-expansion-in-query.md) | feature | 查询管道支持 ~ (Home 目录) 展开 | 2026-04-19 |
| [118](118-extract-preprocessQuery-function.md) | refactor | Extract `preprocessQuery()` function | 2026-04-19 |
| [119](119-whitespace-trim-preprocess.md) | refactor | Add Whitespace Trim to `preprocessQuery()` | 2026-04-19 |
| [120](120-search-syntax-help-window.md) | feature | Search Syntax Help Window | 2026-04-19 |
| [121](121-simplify-query-paths.md) | refactor | 简化查询执行路径：消除 Simple 查询路径 | 2026-04-19 |
| [122](122-phase1-parallel-adaptive-threshold.md) | performance | Phase 1 多线程化 + 预取 + 自适应阈值 | 2026-04-19 |
| [123](123-unified-preprocess-optimization.md) | performance | Unified Query Preprocessing Optimization | 2026-04-19 |
| [124](124-fix-trigram-regression.md) | bugfix | 修复 GLOB trigram 预过滤和路径 trigram 竞争选择 | 2026-04-19 |
| [125](125-relax-trigram-threshold.md) | performance | 放宽 name trigram 候选阈值 | 2026-04-19 |
| [126](126-simplify-codebase-dead-code-removal.md) | refactor | 简化代码库：删除死代码与冗余抽象 | 2026-04-19 |
| [127](127-highlight-hints.md) | feature | Highlight Hints: AST-Based Search Result Highlighting | 2026-04-19 |
| [128](128-path-trigram-early-exit.md) | performance | Path trigram early-exit 优化 | 2026-04-19 |
| [129](129-fix-test-quality.md) | test | Fix Test Quality — Tautological & Weak Assertions | 2026-04-19 |
| [130](130-publish-opensource-script.md) | chore | 开源发布脚本 (publish-opensource.sh) | 2026-04-19 |
| [131](131-soa-tombstone-check.md) | performance | SoA Tombstone Check 优化 | 2026-04-19 |
| [132](132-docs-release-preparation.md) | docs | 公开发布前文档审计与修复 | 2026-04-19 |
| [146](146-release-1.5.md) | docs | MacEverything 1.5 Release Notes | 2026-05-21 |

## 统计

| 类型 | 数量 |
|------|------|
| bugfix | 50 |
| feature | 33 |
| performance | 31 |
| refactor | 17 |
| test | 5 |
| docs | 2 |
| chore | 1 |
| **合计** | **139 条 (132 个编号, 7 组重复编号各 2 条)** |

---

| 日期范围 | 条目数 |
|----------|--------|
| 2026-04-14 | 31 |
| 2026-04-15 | 21 |
| 2026-04-16 | 16 |
| 2026-04-17 | 11 |
| 2026-04-18 | 22 |
| 2026-04-19 | 38 |
