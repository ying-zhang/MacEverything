# MacEverything 1.7.30 发布说明

MacEverything 1.7.30 面向 macOS 15 及以上系统，提供 Apple Silicon 与 Intel 独立安装包。本说明汇总从 1.5.30 fork 基线至今的主要改进，并重点介绍本版在搜索正确性、索引可靠性和自动化接口方面的修复。

## 功能概览

- 面向数百万文件的文件名与完整路径毫秒级搜索，使用 Trigram 倒排索引、SoA 列式存储、SIMD 批过滤和自适应并行查询。
- 支持 AND、OR、NOT、Glob、RE2 正则，以及扩展名、类型、大小、日期、路径、深度和文件名长度等结构化过滤条件。
- 支持拼音首字母与 CJK 搜索、查询语法高亮、幽灵文本建议、关键词高亮和应用内语法帮助。
- 使用 v6 Flat SoA 快照、WAL 增量持久化、写时复制压缩和 FSEvents 实时更新，实现快速启动、崩溃恢复和文件变化同步。
- 提供 `infile:` / `content:` 文件内容搜索、独立内容索引、可配置索引范围、增量更新和上下文片段。
- 支持多窗口和标签页、列表与网格结果、缩略图、快速过滤、高级过滤、完整结果导出和信息面板。
- 内置 `mace` 命令行客户端、MCP Server 和本地 HTTP API，可供终端、自动化工具和 AI 客户端使用。

## 自 1.5.30 以来的主要能力

- **搜索引擎与性能**：建立 Trigram 倒排、选择性查询、Glob 预编译、RE2 正则、SoA 列存、SIMD 批过滤、扩展名索引和自适应并行查询，并支持 CJK、拼音首字母、完整路径和结构化语法。
- **索引可靠性**：加入 WAL、批量回放、写时复制压缩、分页持久化和 v6 Flat SoA 快照，并以 FSEvents 增量同步、单实例锁和崩溃恢复保护大型索引。
- **内容搜索**：提供独立内容索引、范围配置、增量更新、Unicode 片段匹配和分页查询。
- **桌面体验**：支持主题、多窗口与标签页、列表/网格结果、快速与高级筛选、路径过滤、Quick Look、结果导出、信息面板、批处理操作和原位改名。
- **自动化生态**：提供本地 HTTP API、`mace` CLI 和 MCP Server，支持 token 认证、Host/Origin 校验、JSON-RPC 批量请求和应用内依赖部署。
- **工程质量**：覆盖 C++、Swift、Bridge、HTTP、持久化、并发和交互测试，并持续运行 AddressSanitizer 与 ThreadSanitizer。

## 1.7.30 重点更新

- **搜索更准确**：修复多词 AND、拼音、Unicode 路径和文本高亮中的漏结果或崩溃问题；日期、大小和嵌套查询现在会严格校验，并支持开放日期区间。
- **内容索引配置生效更可靠**：自定义内容目录会正确传递到运行时，配置更新、手动重建、文件删除和索引压缩并发时不再丢设置或关联到错误文件。
- **大型内容索引启动更稳定**：加载预算会根据机器物理内存自适应（保留 512 MB 至 8 GB 的安全边界），避免大内存机器反复重建合法索引。
- **实时索引与崩溃恢复更稳健**：统一文件系统事件、重扫和压缩的执行顺序，加强 WAL 损坏恢复与完整重写确认，降低异常退出或磁盘写入失败后的更新丢失风险。
- **CLI、MCP 与 HTTP API 更安全**：正确报告服务端错误，限制请求和响应大小，补强认证、转义和结果类型检查；本地 HTTP 服务改为默认关闭、按需启用。
- **桌面交互完善**：新增“打开方式”应用选择，改进信息面板、Finder 定位、多选、屏外选择保留，以及窗口关闭时的查询资源回收。
- **测试与 CI 更可信**：修复 benchmark 分区校验，避免 MCP 测试静默跳过，并让完整测试、AddressSanitizer 和 ThreadSanitizer 覆盖关键并发与协议路径。

## 安装

Homebrew Cask：

```bash
brew tap ying-zhang/maceverything
brew install --cask maceverything
```

也可以从 GitHub Releases 下载：

- Apple Silicon：`MacEverything-arm64.dmg`
- Intel：`MacEverything-x86_64.dmg`

应用当前采用 ad-hoc 签名，尚未经过 Apple 公证。若 macOS 阻止首次启动，请先尝试打开应用，再前往“系统设置 → 隐私与安全性”点击“仍要打开”。

## 验证

- 快速测试覆盖核心查询、持久化、HTTP、MCP、Bridge 和 Swift 辅助模块。
- AddressSanitizer 与 ThreadSanitizer 分别覆盖快速测试与并发分区。
- arm64 与 x86_64 Release 构建验证主程序、`mace`、MCP、嵌入动态库和代码签名。
