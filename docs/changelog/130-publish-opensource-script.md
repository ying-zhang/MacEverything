# 130 — 开源发布脚本 (publish-opensource.sh)

## 背景

项目计划以 MIT 协议开源发布，但本地开发环境需要保留内部文件（CLAUDE.md、编译产物、
AI 开发配置等）和个人路径。直接从 git 中移除这些文件会影响
日常开发流程。

## 方案：双 remote + 发布脚本

采用「私有 origin + 公开 remote」双远程仓库策略：

- **origin**（私有）：保留所有文件，照常开发
- **public**（公开）：通过发布脚本自动清理后推送

### 发布脚本工作流程

`scripts/publish-opensource.sh` 执行以下步骤：

1. **临时克隆**：在 `/tmp` 下创建仓库的完整克隆
2. **移除内部文件**：
   - 编译产物：`mac_scanner`, `string_search_bench`, `string_search_bench.cpp`
   - AI 工具配置：`CLAUDE.md`, `.claude/`
   - 内部文档：`docs/superpowers/`
   - 误入文件：`assets/README.md`（Homebrew 的 README）
3. **路径脱敏**：将所有个人路径替换为 `/Users/username`
4. **添加 LICENSE**：生成 MIT 协议全文
5. **更新 README.md**：许可证部分改为 MIT 引用
6. **更新 .gitignore**：追加编译产物和内部文件的忽略规则
7. **验证**：`git grep` 确认无残留个人信息
8. **提交并推送**（或 dry-run 报告）

### 使用方法

```bash
# Dry-run（默认），查看清理结果
./scripts/publish-opensource.sh

# 实际推送到公开远程
git remote add public git@github.com:YOUR_USER/MacEverything.git
./scripts/publish-opensource.sh --push

# 自定义远程名和分支
./scripts/publish-opensource.sh --push --remote github --branch main
```

## 新增文件

| 文件 | 说明 |
|------|------|
| `scripts/publish-opensource.sh` | 开源发布清理与推送脚本 |

## 验证

- [x] Dry-run 模式正确移除所有内部文件
- [x] `git grep` 个人路径检查在清理后的仓库中返回空结果
- [x] LICENSE 文件正确生成（MIT 全文）
- [x] README.md 许可证部分正确更新
- [x] .gitignore 追加了编译产物和内部文件规则
- [x] 脚本在非 master 分支上正确拒绝执行

## 风险

低。脚本在临时克隆中操作，不修改本地仓库任何文件。
