# MacEverything 1.10.20 发布说明

MacEverything 1.10.20 于 2026-10-21 正式发布，构建号 14，面向 macOS 15 及以上系统，提供 Apple Silicon 与 Intel 独立安装包。本版本从 1.5.30 基线继续迭代，汇总搜索、索引、持久化、界面、自动化和卷生命周期方面的更新。用户已确认修复启动闪退的测试包在另一台 Mac 上正常启动；GitHub Release、网站下载页和 Homebrew Cask 同步更新。

## 功能概览

- 面向数百万文件的文件名与完整路径毫秒级搜索，使用 Trigram 倒排索引、SoA 列式存储、SIMD 批过滤和自适应并行查询。
- 支持 AND、OR、NOT、Glob、RE2 正则，以及扩展名、类型、大小、日期、路径、深度和文件名长度等结构化过滤条件。
- 支持拼音首字母与 CJK 搜索、查询语法高亮、幽灵文本建议、关键词高亮和应用内语法帮助。连续输入普通子串时复用上一轮的全部命中，匹配阶段按结果上限保留前 K 条。
- 使用 v6 Flat SoA 快照、WAL 增量持久化和 FSEvents 实时更新，实现快速启动、崩溃恢复和文件变化同步。
- 提供 `infile:` / `content:` 文件内容搜索、独立内容索引、增量更新、范围设置和上下文片段。
- 支持多窗口和标签页、列表与网格结果、缩略图、可调行高与图标大小、快速过滤和高级过滤。
- 内置 `mace` 命令行客户端、MCP Server 和本地 HTTP API，可供终端、自动化工具和 AI 客户端使用。

## 自 1.5.30 以来的主要变更

- **搜索引擎与性能**：从基础字符串匹配扩展到 Trigram 倒排、选择性查询、Glob 预编译、RE2 正则、SoA 列存、SIMD 批过滤、扩展名索引和自适应并行；补充 CJK、拼音首字母、完整路径和结构化查询语法。普通子串查询在继续输入、范围确实变窄时，只重验上一轮的全部命中；OR、NOT、通配符、正则、过滤器和索引变更不复用。
- **索引可靠性**：建立 WAL 增量写入、批量回放、写时复制压缩、分页持久化和 v6 Flat SoA 快照；增加 FSEvents 增量同步、启动阶段保护、单实例锁和崩溃恢复。全量扫描和后台替换会作废旧快照上的文件系统事件，实时监听从扫描起点或重放水位继续。
- **卷与网络目录**：本地外置盘、网络卷和 `/Volumes` 下的普通目录按卷身份区分。只有先前确认过且 UUID 仍然一致的本地外置盘，在不可达时才跳过扫描并清理缓存。离线 NAS、未知路径和同名但身份不同的卷会保留已有索引。
- **内容搜索**：增加独立内容索引、范围配置、增量更新、上下文片段和分页查询，并修复内容索引重建、取消和并发更新场景。
- **桌面体验**：支持主题、行高、图标和缩略图调整，多窗口与标签页，列表/网格结果，快速筛选、高级筛选、路径过滤器、Quick Look，以及信息面板中的预览、文件信息、批处理操作和原位改名。快速筛选在窄窗口中不再重叠。
- **自动化生态**：从 GUI Bridge 抽取 ServiceEngine，提供本地 HTTP API、`mace` CLI 和 MCP Server；支持 token 认证、严格 Host/Origin 校验、JSON-RPC 批量请求和嵌入式依赖部署。
- **工程质量**：持续补充 C++、Swift、Bridge、HTTP、InstanceLock、导出和交互测试，加入 AddressSanitizer/ThreadSanitizer 定时检查，并完善 arm64/Intel 发布构建与 Homebrew Cask 发布辅助流程。

## 1.10.20 重点更新

- 连续输入时，普通子串查询复用上一轮的全部命中记录号；命中超过 200 万条时不写入缓存。匹配阶段每个线程只保留最好的 `maxResults` 条，同分按记录号排序。
- 快速筛选分段按钮不再被压窄。窗口最小宽度为 760；空间不足时先收起说明文字、缩略图滑杆和路径框。列表/网格切换和信息面板保持可见。
- NAS 轮询只重扫网络根，避免任务堆积。混合本地目录和 NAS 时，本地目录继续回放 FSEvents，NAS 使用轮询协调。首次扫描遇到不可达根时报告失败，不写入空索引。
- 重建索引前会检查扫描根。离线网络卷或未知根目录会阻止删除旧索引文件。
- 外置盘拔出后的清理同时核对卷 UUID 和索引所属 UUID。挂载名相同但身份不符时不清理缓存。每个引擎使用自己的卷分类存储。
- 扫描、重建和后台全量替换共用生命周期锁。发布新索引时递增快照编号，并只丢掉属于旧快照的文件系统事件。用户排队的重命名、删除和重扫仍在发布后执行。
- 实时监听在扫描或同步期间不会被卷通知提前拉起。全量扫描结束后从扫描起点重放；后台重放成功后，从重放流交付过的最后事件编号继续。
- 拖拽重命名、子树重扫和卸载删除若发生在扫描或同步期间，会延后到当前索引发布后再执行。已经重新挂载的路径不会被旧的卸载删除清掉。
- 修复安装后在 dyld 阶段因 RE2 动态库签名 Team ID 不匹配而退出的问题：从校验过的官方源码静态链接 RE2/Abseil，不再加载第三方 dylib，同时保留 Hardened Runtime 和库验证。
- 参考 szh0721 fork 对签名完整性检查不足的分析，但不采用关闭 Hardened Runtime 的方案；增加真实 Mach-O 签名回归测试和迁移后静态 RE2 应用启动测试。
- 本地和 CI 统一使用静态依赖构建；Apple Silicon 可交叉编译 Intel 包。打包前检查签名、运行时保护、架构、所有动态依赖，并附带第三方许可证。
- MCP 初始化响应从应用 Bundle 读取版本号，与 GUI、HTTP 状态和锁元数据保持一致；独立运行且没有应用 Bundle 时报告 `unknown`。
- 修复异常超长版本字符串导致 InstanceLock 元数据写入读取缓冲区边界外内存的问题，并增加截断回归测试。
- 应用发布记录只保留 1.5.30 与 1.10.20；文档检查自动验证应用版本、发布说明文件名和本地 Markdown 链接。

## 安装

通过 Homebrew Cask 安装：

```bash
brew tap ying-zhang/maceverything
brew install --cask maceverything
```

已有安装可执行 `brew update && brew upgrade --cask maceverything`。也可以从 [GitHub Release](https://github.com/ying-zhang/MacEverything/releases/tag/v1.10.20) 下载：

- [Apple Silicon：MacEverything-arm64.dmg](https://github.com/ying-zhang/MacEverything/releases/download/v1.10.20/MacEverything-arm64.dmg)
- [Intel：MacEverything-x86_64.dmg](https://github.com/ying-zhang/MacEverything/releases/download/v1.10.20/MacEverything-x86_64.dmg)
- [个人博客下载页](https://ying-zhang.cn/misc/2026-maceverything/)

SHA-256：

```text
0252a210abeba01ff1f424ad9777742cf678d64d787fbc0f3151d0646ccea987  MacEverything-arm64.dmg
6dc51796c9fd5ebced90741464f71766e79bad98e830e06c8e6fa4ea047255a7  MacEverything-x86_64.dmg
```

应用采用 ad-hoc 签名。若 macOS 阻止首次启动，请先尝试打开应用，再前往“系统设置 → 隐私与安全性”点击“仍要打开”。

## 验证

- `make test-fast`：串行核心快速测试 1924 passed / 0 failed，Swift/Bridge/本地化/文档检查通过；应用版本与文档链接护栏回归 7 项通过。
- `make test-asan`、`make test-tsan`：各 1924 passed / 0 failed，未报告 sanitizer 错误。
- 使用包内 MCP helper 的 `make test-all`：12385 passed / 0 failed。
- 签名回归测试：arm64 原生和 x86_64 Rosetta 各 7 项通过，保留 Hardened Runtime 的静态 RE2 测试应用迁移后可实际启动，并验证 MCP 的 Bundle 版本读取。
- 构建号 14 的两种架构均完成 Release 构建和 DMG 挂载安装副本校验；实际应用在隔离用户目录中运行 8 秒未退出，包内 CLI 帮助、MCP 应用版本与协议各 45 项测试通过。Intel 测试使用 Rosetta，不代替原生 Intel 或其他 macOS 版本验收。
- 异机验收：2026-10-21 用户确认另一台 Mac 安装后不再闪退；此反馈仅确认启动问题，不代表其他功能或原生 Intel 验收已经完成。
- `xcodebuild analyze` 完成，仍有 15 条静态分析提示；分类、性能断言的环境敏感性及后续验证边界见[当前审查](https://github.com/ying-zhang/MacEverything/blob/v1.10.20/docs/research/main-branch-review.md)。
