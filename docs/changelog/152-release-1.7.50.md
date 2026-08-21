# MacEverything 1.7.50 发布说明

MacEverything 1.7.50 是 1.7.30 之后的补丁版本，聚焦两处修复：拼音搜索召回和符号链接路径下的实时索引。安装方式与 1.7.30 相同（Homebrew Cask 或 GitHub Releases 下载 arm64 / x86_64 DMG）。

## 重点更新

- **拼音搜索召回更完整**：多音字词表匹配顺序改为确定性（最长优先），不再依赖哈希表迭代顺序；Core Foundation 无法转写的生僻汉字（如扩展 A 区）现在回退保留原字，而不是被静默丢弃导致搜索漏结果。
- **符号链接路径的实时索引修复**：FSEvents 上报的是解析过符号链接的真实路径（`/tmp` → `/private/tmp`、`/var` → `/private/var`），此前与扫描根做字符串前缀比较时对不上，导致扫描根含符号链接组件时文件变更无法实时进索引。现在比较前会用 `std::filesystem::canonical` 解析根路径再匹配。
- **CI 全绿**：修复了三个 FSEvents 测试（同进程写入被 `IgnoreSelf` 过滤、`/var/folders` 临时目录的符号链接与系统路径双重问题），weekly 全量测试不再误报失败。

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

- 全量 `./test_all`：12290 通过 / 0 失败（含本地构建 `MacEverythingMCP` 后的 MCP 协议测试）。
- FSEvents 集成、端到端、搜索延迟三组用例全部通过。
