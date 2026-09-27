# MacEverything 1.9.27 发布说明

MacEverything 1.9.27 汇总搜索正确性、索引可靠性、NAS/SMB 目录稳定性、索引生命周期安全和搜索服务恢复方面的改进，并同步修复 GitHub Actions 构建环境问题。

## 重点更新

- **搜索更准确**：修复多词 AND、拼音、Unicode 路径和文本高亮中的漏结果或崩溃问题；多音字词表使用确定性的最长优先匹配，生僻汉字转写失败时保留原字；日期、大小和嵌套查询严格校验，并支持开放日期区间。
- **搜索能力更完整**：支持 Trigram 倒排、Glob 预编译、RE2 正则、SoA 列存、SIMD 批过滤、扩展名索引、自适应并行查询，以及 CJK、拼音首字母、完整路径和结构化语法。
- **内容索引更可靠**：自定义内容目录正确传递到运行时，配置更新、手动重建、文件删除和索引压缩并发时保持一致；内容索引启动预算按物理内存自适应，并提供增量更新、Unicode 片段匹配和分页查询。
- **实时索引与崩溃恢复更稳健**：统一文件系统事件、重扫和压缩的执行顺序，加强 WAL 损坏恢复、完整重写确认和 v6 Flat SoA 快照；符号链接路径使用 canonical 根路径匹配，避免实时更新漏检。
- **NAS/SMB 同步改进**：网络根目录使用轮询协调，避免重复堆积扫描；混合本地目录和 NAS 时，本地目录继续回放 FSEvents 水位，NAS 只做网络重扫。
- **启动与重建安全**：不可达扫描根会报告明确错误，不会写入空索引；重建索引前校验目录，避免未挂载外置盘或 NAS 时删除旧索引。
- **监控与内容查询**：监控重启保留 FSEvents 水位；HTTP 内容查询支持客户端断开、服务停止和 30 秒截止时间取消。
- **自动化接口更安全**：CLI、MCP 和 HTTP API 正确报告服务端错误，限制请求和响应大小，补强认证、转义和结果类型检查；本地 HTTP 服务默认关闭、按需启用。
- **桌面交互完善**：支持“打开方式”应用选择，改进信息面板、Finder 定位、多选、屏外选择保留，以及窗口关闭时的查询资源回收；支持主题、多窗口与标签页、列表/网格结果、Quick Look、结果导出、批处理和原位改名。
- **正则高亮保护**：高亮器拒绝嵌套量词和有歧义的重复交替模式，搜索本身仍使用 RE2。
- **CI 稳定性**：GitHub Actions 构建 runner 显式安装 `ripgrep`，文档检查不再因缺少 `rg` 失败；修复 FSEvents 测试中的 IgnoreSelf 和系统路径误报。
- **文档整理**：历史审查结论合并到主分支审查入口，保留竞品分析和 Joshua Wu fork 分析作为独立研究。

## 安装

Homebrew Cask：

```bash
brew tap ying-zhang/maceverything
brew install --cask maceverything
```

也可以从 GitHub Releases 下载：

- Apple Silicon：`MacEverything-arm64.dmg`
- Intel：`MacEverything-x86_64.dmg`

## 验证

- 完整 `./test_all`：12299 passed / 0 failed。
- 快速测试：1838 passed / 0 failed。
- Swift 高亮测试：79 passed / 0 failed。
- Bridge、文档和本地化检查通过；AddressSanitizer 与 ThreadSanitizer 覆盖关键并发与协议路径。
- arm64 与 x86_64 Release 构建验证主程序、`mace`、MCP、嵌入动态库和代码签名。
- Windows SMB 共享已完成基础扫描与同步验证。
