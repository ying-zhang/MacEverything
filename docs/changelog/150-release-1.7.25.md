# MacEverything 1.7.25 发布说明

MacEverything 1.7.25 面向 macOS 15 及以上系统，提供 Apple Silicon 与 Intel 独立安装包。本版本从 1.5.30 基线继续迭代，集中完成搜索、索引、持久化、界面、自动化和安全能力的升级。

## 功能概览

- 面向数百万文件的文件名与完整路径毫秒级搜索，使用 Trigram 倒排索引、SoA 列式存储、SIMD 批过滤和自适应并行查询。
- 支持 AND、OR、NOT、Glob、RE2 正则，以及扩展名、类型、大小、日期、路径、深度和文件名长度等结构化过滤条件。
- 支持拼音首字母与 CJK 搜索、查询语法高亮、幽灵文本建议、关键词高亮和应用内语法帮助。
- 使用 v6 Flat SoA 快照、WAL 增量持久化和 FSEvents 实时更新，实现快速启动、崩溃恢复和文件变化同步。
- 提供 `infile:` / `content:` 文件内容搜索、独立内容索引、增量更新、范围设置和上下文片段。
- 支持多窗口和标签页、列表与网格结果、缩略图、可调行高与图标大小、快速过滤和高级过滤。
- 内置 `mace` 命令行客户端、MCP Server 和本地 HTTP API，可供终端、自动化工具和 AI 客户端使用。

## 自 1.5.30 以来的主要变更

- **搜索引擎与性能**：从基础字符串匹配扩展到 Trigram 倒排、选择性查询、Glob 预编译、RE2 正则、SoA 列存、SIMD 批过滤、扩展名索引和自适应并行；补充 CJK、拼音首字母、完整路径和结构化查询语法。
- **索引可靠性**：建立 WAL 增量写入、批量回放、写时复制压缩、分页持久化和 v6 Flat SoA 快照；增加 FSEvents 增量同步、启动阶段保护、单实例锁和崩溃恢复，显著缩短大型索引的启动时间。
- **内容搜索**：增加独立内容索引、范围配置、增量更新、上下文片段和分页查询，并修复内容索引重建、取消和并发更新场景。
- **桌面体验**：支持主题、行高、图标和缩略图调整，多窗口与标签页，列表/网格结果，快速筛选、高级筛选、路径过滤器、Quick Look，以及信息面板中的预览、文件信息、批处理操作和原位改名。
- **自动化生态**：从 GUI Bridge 抽取 ServiceEngine，提供本地 HTTP API、`mace` CLI 和 MCP Server；支持 token 认证、严格 Host/Origin 校验、JSON-RPC 批量请求和嵌入式依赖部署。
- **工程质量**：持续补充 C++、Swift、Bridge、HTTP、InstanceLock、导出和交互测试，加入 AddressSanitizer/ThreadSanitizer 定时检查，并完善 arm64/Intel 发布构建与 Homebrew Cask 发布辅助流程。

## 1.7.25 重点更新

- 完善英文界面本地化：搜索结果的文件大小与日期按英文格式显示，快速筛选、设置页选项、高级筛选及独立帮助窗口在切换语言后立即刷新。
- 主界面新增信息面板：顶部直接显示 Quick Look 预览，中部提供打开、复制文件名、复制完整路径、在访达中显示和删除到废纸篓等操作，底部显示可复制的完整文件名、路径、大小、类型和时间信息。
- 多选结果时，右键菜单与信息面板统一显示批处理数量；打开、复制、在访达中显示、Quick Look 和删除支持批处理，改名与终端操作在不适用时明确置灰。
- 文件名可直接在信息面板中原位修改；面板支持自适应、从不显示和总是显示三种默认策略，并可通过主界面按钮或“显示”菜单切换。
- 搜索结果支持导出 CSV/TXT，导出范围覆盖查询上限内的完整过滤结果，不受 GUI 分页限制；大批量序列化和写入在后台执行，CSV 带 UTF-8 BOM 并防止公式前缀注入。
- 文件名搜索默认上限 10,000、最大 100,000，GUI 每页显示 100 项；内容搜索独立设置且最多 200 项；搜索历史默认 50、最大 200 项。
- 路径过滤器支持文件夹选择和最近使用目录；首次索引流程提供“快速开始”与“全盘搜索”两种权限路径。
- 单个英文字母查询使用 300ms 防抖，第二个字符起恢复 80ms，减少用户连续输入时的大候选集冗余查询。
- 删除独立 daemon 运行模式，由 GUI/菜单栏进程统一持有 ServiceEngine 和索引生命周期，避免多个引擎同时访问同一索引。
- InstanceLock 在读取快照或 WAL 前强制获取，记录 PID、应用版本和启动时间，并拒绝符号链接或非当前用户所有的锁文件。
- HTTP API 增加严格 Host、Origin、Authorization 与 Content-Length 校验，支持可选 256-bit token，并修正状态文本、header 大小写和 OWS 处理。
- MCP 的 JSON-RPC 批量请求按规范返回单一数组；本地服务版本由应用 Bundle 注入，健康检查与锁元数据保持一致。
- 权限探测移出主线程，内容分页增加异步防重入保护，Quick Look 初始化失败不再导致崩溃。
- 修复分页任务与筛选、排序或内容索引清理并发时的数组越界；信息面板改名后同步刷新文件名、内容索引和 WAL。
- 加固 FSEvents watcher 生命周期，运行状态改用原子快照，停止时清理早退信号量，并避免在回调执行期间销毁 callable。

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

- 快速测试：C++、Objective-C++ bridge lint、Swift 导出/交互/MCP/高亮/刷新节流测试。
- HTTP 安全、InstanceLock 强制保护、FSEvents 批量变更和 MCP JSON-RPC 协议回归测试。
- AddressSanitizer 与 ThreadSanitizer 每周定时任务。
- arm64 与 x86_64 Release 构建分别验证主程序、`mace`、MCP 和嵌入 dylib 的目标架构及动态库路径。
