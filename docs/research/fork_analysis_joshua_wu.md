# joshua-wu/MacEverything Fork 对比分析

**分析日期**: 2026-07-25
**Fork 仓库**: https://github.com/joshua-wu/MacEverything
**Fork 分支**: main (v1.2, 2026-04 开源快照) + master (v1.5, 持续开发)
**本项目状态**: 下一版本开发中

## 分叉关系

两个仓库 SHA 不共享（fork 做了 sanitize 后重新发布）。fork 的 main 分支是 2026 年 4 月的快照，此后本项目已大幅超越。master 分支有额外功能但也包含大量性能分析报告提交。

## 本项目已领先的领域（无需采纳）

- 多词项评分 — 本项目 `encodeScore(missCount, qualitySum, pathLen)` 三维编码 vs fork 单一 priority
- hash-keyed pathIndex — 本项目已用 uint64 hash 替代 string key
- lowerPathPool 消除 — 本项目用 SIMD 实时小写转换，省掉路径池副本
- IME 组合输入防护 — 本项目有 `hasMarkedText()` 检查，fork 没有
- highlightHints 缓存 — 本项目用 `@Published` + 显式更新，fork 用计算属性每次重算
- Ghost text、搜索语法高亮、搜索选项菜单 — 功能等价，本项目略更完善
- 拼音首字母、CJK Bigram、Unicode NFC/NFD、系统应用别名 — 本项目独有
- SearchEngineOptions 可配置化 — 本项目支持运行时开关，fork 全部硬编码
- SearchServiceModel 架构分离 — 本项目解耦索引管理与搜索 UI
- PostingListIntersection / TrigramExtraction 优化 — 本项目有自适应策略
- StringPool 边界检查、CompactResult 向量化 — 本项目更健壮
- 多路径扫描、ScanConfig 过滤、动态配置、卷宗卸载保护 — 本项目独有
- HttpServer 8-worker 线程池 — fork 单线程阻塞
- CLI (mace)、MCP 升级为 ObjC++ — 本项目独有
- Phase 2 内存压力检查 — 本项目通过 Mach API 判断

## Fork 独有功能（master 分支）— 值得关注

### 高优先级

**batchMutate 写锁分块（300-op chunks）**
- 将 FSEvents 批量变更的写锁从整批持有改为每 300 个操作一组，组间释放锁
- 防止大量文件变更（如 git checkout）时搜索查询被饥饿
- 实现极简：`batchMutate()` 中加一层 for 循环以 `kChunkSize = 300` 分块加锁
- 状态：**已采纳**。当前 `SearchEngine::batchMutate()` 已按 300 项分块，并有 650 项跨分块回归测试。

### 中优先级

**ShortQueryCache — 1-2字符 ASCII 短查询缓存**
- 为 26 个单字母 + 676 个双字母组合预构建排序结果缓存（共 702 个 entry，每个最多 100 条）
- 文件：`ShortQueryCache.h/cpp`，支持增量删除、磁盘序列化、50% 删除阈值触发重建
- 单字符查询跳过全量扫描，直接返回缓存结果
- 状态：**未采纳，输入体验改善明显但需适配拼音场景**

**RichTextExtractor — 富文档内容提取三层回退**
- Spotlight MDItem API → PDFKit → NSAttributedString
- 支持 pdf/doc/docx/xls/xlsx/ppt/pptx/rtf/odt/ods/odp
- 状态：**未采纳，本项目已有 ContentIndex 框架，可参考其回退策略**

### 低优先级

**双击闪烁高亮** — ResultRow 加 ~18 行 SwiftUI overlay + opacity 动画，纯视觉增强

**AppDelegate 引擎启动** — 将引擎启动移到 `applicationDidFinishLaunching`，避免窗口未显示时引擎不启动。本项目 `SearchServiceModel.shared` 单例模式可能已避免此问题

### 仅供参考

**AI/LLM 搜索集成（大型功能集）**
- 三个演进阶段：Python AI 服务 + Ollama → LiteLLM 代理 + 嵌入向量搜索 → 内置 llama.cpp
- NL-to-Query 翻译思路有价值（自然语言 → 搜索语法），但实现过重
- 语义向量搜索对文件名搜索工具投入产出比不高
- 如要做 AI 功能建议采用更轻量方案
