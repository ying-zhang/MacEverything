# MacEverything 1.9.27 发布说明

MacEverything 1.9.27 聚焦 NAS/SMB 目录稳定性、索引生命周期安全和搜索服务恢复，并同步修复 GitHub Actions 构建环境问题。

## 重点更新

- **NAS/SMB 同步改进**：网络根目录使用轮询协调，避免重复堆积扫描；混合本地目录和 NAS 时，本地目录继续回放 FSEvents 水位，NAS 只做网络重扫。
- **启动与重建安全**：不可达扫描根会报告明确错误，不会写入空索引；重建索引前校验目录，避免未挂载外置盘或 NAS 时删除旧索引。
- **监控与内容查询**：监控重启保留 FSEvents 水位；HTTP 内容查询支持客户端断开、服务停止和 30 秒截止时间取消。
- **正则高亮保护**：高亮器拒绝嵌套量词和有歧义的重复交替模式，搜索本身仍使用 RE2。
- **CI 稳定性**：GitHub Actions 构建 runner 显式安装 `ripgrep`，文档检查不再因缺少 `rg` 失败。
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
- Bridge、文档和本地化检查通过。
- Windows SMB 共享已完成基础扫描与同步验证。
