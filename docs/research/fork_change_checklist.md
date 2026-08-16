# Fork 变更与新增功能清单（redo 基线）

> 基线：上游 `joshua-wu/MacEverything` 最后一个提交 `26f0332`（v1.2, 2026-04-20）
> 终点：本 fork 当前 `main`（`cf78a7e8e`, 1.7.30, 2026-08-02）
> 共 42 个提交，193 个文件变更，+22634 / -3665 行。
> 本清单用于指导 `redo` 分支从 26f0332 起独立重做这些功能，再与 main 交叉检验。

## A. 搜索与索引核心（C++）

1. **RE2 依赖本地化与打包**（dd1d4fa, c940585, 78124ac）
   - RE2 从 `/opt/homebrew/opt/re2` 改为 `third_party/re2`，`-Wl,-rpath` 指向本地库。
   - 新增脚本 `scripts/prepare-re2-deps.sh`、`scripts/prepare-re2-deps-from-app.sh`、`scripts/embed-homebrew-dylibs.sh`，把 Homebrew 的 RE2/abseil 复制进 App 并改写 install_name。

2. **文件名与路径搜索增强**（5e8dea9）
   - 路径搜索、文件名匹配的边界情况（`~` 展开、斜杠查询、路径分隔符）。

3. **索引根迁移 + 设置 UI**（44dc0ad）
   - 索引根目录（index roots）迁移/持久化；设置界面打磨。

4. **正则帮助 + 中文搜索**（9b03ca2）
   - 正则语法帮助完善；中文搜索（NFC/NFD、CJK）支持。

5. **内容搜索设置 + 可缩放结果列**（372079d）
   - ContentIndex 内容搜索的运行时开关；结果列表列宽可调。

6. **拼音匹配**（8cf5448）
   - 全拼 + 拼音首字母匹配；CJK Bigram；Unicode NFC/NFD 归一化；系统应用别名。

7. **索引内存优化**（cbf5ea0, 2601e05, 5cdacb4）
   - 优化索引内存占用；2601e05 的内存优化被 5cdacb4 回退，同时修复搜索缓存。

8. **SIMD 搜索引擎优化**（506dec8）
   - 新增 `SIMDSearch.h`、`PostingListIntersection.h`、`TrigramExtraction.h`，用 SIMD 加速 posting list 求交与 trigram 提取。

9. **WAL 损坏 / 陈旧结果 / 焦点**（12ae979）
   - 修复 WAL 损坏（CRC 校验）、陈旧结果、窗口焦点处理。

10. **排序目录优先 + 图标打磨**（822230d）
    - 结果排序目录优先；应用图标打磨。

11. **Phase 2 索引可靠性**（0c635c6）
    - 索引可靠性修复（扫描取消、重扫描去抖）。

12. **搜索窗口恢复 + RE2 打包**（c940585）
    - 搜索窗口状态恢复；RE2 dylib 打包修正。

13. **USB 热插拔 + 多词项排序**（5eba018）
    - 卷宗热插拔（USB 挂载/卸载）保护；多词项排序 `encodeScore(missCount, qualitySum, pathLen)` 三维编码。

14. **结果点击延迟 + 索引生命周期**（1365e8e）
    - 结果点击延迟修复；索引生命周期管理（增量缓存 recentIndices）。

15. **内容搜索加固**（5fbb01a）
    - ContentIndex 内容搜索健壮性。

16. **review-v3 关键问题 + 持久化恢复**（eeba659）
    - 关键并发/持久化问题修复；持久化恢复。

17. **索引加固 + 1.7.30 发布**（0b6a2d1）
    - 索引健壮性、自适应索引内核、扫描配置、卷宗卸载保护。

## B. UI 与交互（Swift）

1. **中文本地化**（dd1d4fa）：`en.lproj` / `zh-Hans.lproj` Localizable.strings。
2. **可配置设置 + Dock 隐藏**（946a09f）：AppSettings、GeneralSettingsView、ShortcutSettingsView、PermissionView、HotkeyManager。
3. **搜索加速键 + 菜单栏图标**（fb38bae）：Option+Space 唤起、StatusBarIcon。
4. **Swift 代码质量**（e614c8a）：MainActor 隔离等。
5. **行内重命名 + 崩溃修复**（47cfaa3, 387cd9d）：结果行内重命名。
6. **应用图标设计**（bbe8181）。
7. **GitHub Pages 落地页**（1866712）：`docs/index.html`。
8. **结果交互 + 快速过滤**（f6e0e99）：ResultInteraction、QuickFilter。
9. **搜索标签页 + 窗口行为**（70eb171）。
10. **主题自定义 + 文件缩略图**（1cf7222）：ThemeManager、ThumbnailService。
11. **菜单栏 Copy Path**（230d8fc）。
12. **文件结果图标显示模式**（1041490）：图标视图。
13. **结果 UI 重构 + 显示控件**（c0f26f0）：ResultGridCell、ResultRow、列显示控制。
14. **状态栏高级过滤**（a5b65a9）：状态栏显示过滤。
15. **结果裁剪修复**（08b5f27）。
16. **文件交互改进文档**（249a347）。

## C. CLI 与集成

1. **CLI (`mace`) + Homebrew 集成**（2d2747d）：
   - `MacEverything/CLI/mace_main.cpp` + `MaceClient.h`（替代 daemon_main.cpp）。
   - `MacEverything/App/CLIInstallManager.swift`。
   - `scripts/update-homebrew-tap.sh`。
2. **MCP 升级为 ObjC++**（2d2747d）：`MacEverything/CLI/mcp_main.mm`。
3. **架构键控 Homebrew cask checksum**（6b28dcd）。

## D. 构建 / 发布 / 文档

1. **双架构 DMG**（78124ac）：`scripts/build-release-dmgs.sh`（arm64 + x86_64）。
2. **GitHub Actions CI**（78124ac/0b6a2d1）：`.github/workflows/build-macos.yml`（arm64、x86_64、周度 ASan/TSan/全量）。
3. **个人隐私审计**（703e47d）。
4. **发布说明/文档**（a6f7aa7, db27265, 703e47d, 0b6a2d1）。
5. **DMG 创建脚本**：`scripts/create-dmg.sh`、`scripts/download-intel-dmg.sh`。

## E. 测试体系

- `Makefile` 扩充：`test-fast` 现在包含 lint-localizations、lint-docs、test-mace-client、多个 Swift 单测目标（content-roots、cli-install、interaction、mcp、export、highlight、throttle、l10n）。
- 新增测试模块：test_mace_client、test_cli_install、test_content_root_policy、test_result_interaction、test_mcp_config、test_search_export、test_review_regressions、test_scanner_config、test_http_security、test_instance_lock_enforcement、test_l10n_formatting、test_query_date_filters、test_query_simplification、test_slash_query、test_tilde_expansion 等。
- 从 87 个测试模块扩到 90+；新增 Swift 单测与 C++ 回归测试。

## 优先交叉检验的核心算法（bug 高发区）

- 拼音匹配（全拼/首字母/CJK/NFC-NFD）
- 多词项排序 encodeScore(missCount, qualitySum, pathLen)
- SIMD posting list 求交 + trigram 提取（越界/边界）
- WAL CRC 校验与 rename chain
- ContentIndex trigram 内容搜索
- 结构化查询 / slash 查询 / 日期过滤 / 查询简化
- StringPool 边界检查
- 查询取消（per-session cancellation）
