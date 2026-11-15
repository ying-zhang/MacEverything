# MacEverything 1.11.15 发布说明

1.11.15 汇总自 1.5.30 以来的搜索、索引、持久化、界面与自动化更新，并修复安装后启动、文件变化同步及正则高亮等问题。

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
- **索引可靠性**：建立 WAL 增量写入、批量回放、写时复制压缩和 v6 Flat SoA 快照；增加 FSEvents 增量同步、启动阶段保护、单实例锁和崩溃恢复。全量扫描和后台替换会作废旧快照上的文件系统事件，实时监听从扫描起点或重放水位继续。
- **卷与网络目录**：本地外置盘、网络卷和 `/Volumes` 下的普通目录按卷身份区分。只有先前确认过且 UUID 仍然一致的本地外置盘，在不可达时才跳过扫描并清理缓存。离线 NAS、未知路径和同名但身份不同的卷会保留已有索引。
- **内容搜索**：增加独立内容索引、范围配置、增量更新、上下文片段和分页查询，并修复内容索引重建、取消和并发更新场景。
- **桌面体验**：支持主题、行高、图标和缩略图调整，多窗口与标签页，列表/网格结果，快速筛选、高级筛选、路径过滤器、Quick Look，以及信息面板中的预览、文件信息、批处理操作和原位改名。快速筛选在窄窗口中不再重叠。
- **自动化生态**：从 GUI Bridge 抽取 ServiceEngine，提供本地 HTTP API、`mace` CLI 和 MCP Server；支持 token 认证、严格 Host/Origin 校验、JSON-RPC 批量请求和嵌入式依赖部署。
- **工程质量**：持续补充 C++、Swift、Bridge、HTTP、InstanceLock、导出和交互测试，加入 AddressSanitizer/ThreadSanitizer 检查，并完善 arm64/Intel 发布构建与 Homebrew Cask 发布辅助流程。

## 本次重点修复

- **安装后启动闪退**：从校验过的官方源码静态链接 RE2 / Abseil，避免第三方动态库的签名身份不匹配；保留 Hardened Runtime 和库验证，并附带第三方许可证。
- **增量索引**：普通文件变化只更新对应文件，目录变化才协调子树；统一隐藏文件、排除规则、App 包、显式嵌套根及路径别名处理。修复单边重命名、硬链接、目录移出范围和根路径越界等边界。
- **数据可靠性**：临时权限或 I/O 错误不当作文件删除；快照及 WAL 检查定位、写入和长度错误，失败时保留恢复所需数据，并增加五处实际写入失败及重试回归。
- **正则高亮**：与搜索统一使用 RE2，正确处理中文、emoji、锚点和空匹配，限制高亮资源使用，避免复杂表达式拖慢界面。
- **卷与扫描生命周期**：离线网络目录不以空索引覆盖已有数据；重建前检查扫描根；外置盘清理同时核对卷身份；扫描、重建、重放与文件事件按统一生命周期交接。
- **连续输入与界面**：普通子串查询可复用上一轮全部命中候选；窄窗口中的快速筛选不再重叠，保留视图切换与信息面板入口。
- **自动化与帮助**：GUI、HTTP 和 MCP 从同一应用 Bundle 读取版本；修复锁元数据的超长版本字符串边界；统一中英文查询帮助，官网直接提供常用搜索及完整语法参考。

## 下载与安装

- [Apple Silicon：MacEverything-arm64.dmg](https://github.com/ying-zhang/MacEverything/releases/download/v1.11.15/MacEverything-arm64.dmg)
- [Intel：MacEverything-x86_64.dmg](https://github.com/ying-zhang/MacEverything/releases/download/v1.11.15/MacEverything-x86_64.dmg)
- [SHA-256 校验文件](https://github.com/ying-zhang/MacEverything/releases/download/v1.11.15/SHA256SUMS.txt)

要求 macOS 15 及以上。打开 DMG，将 MacEverything 拖到“应用程序”。应用采用 ad-hoc 签名，尚未经 Apple 公证；若首次启动被阻止，先尝试打开一次，再前往“系统设置 → 隐私与安全性”选择“仍要打开”。

也可通过 Homebrew 安装或升级：

```bash
brew tap ying-zhang/maceverything
brew install --cask maceverything
# 已安装时：
brew update
brew upgrade --cask maceverything
```

## 验证

发布包提供 arm64 与 x86_64 两种架构；Release 构建、签名、静态依赖和 DMG 安装副本检查均通过。快速核心测试 1977 项、Swift 正则高亮 89 项通过；两种架构的包内 MCP 各 45 项通过，GUI 在隔离目录中各运行 8 秒未退出。源码审查与自动化验证详见[审查记录](../research/main-branch-review.md)。Intel 包在 Apple Silicon 上交叉编译并通过 Rosetta 验证；这不替代原生 Intel、其他 macOS 版本或真实 NAS 长期环境的测试。
