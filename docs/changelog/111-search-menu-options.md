# 111 - Search 顶级菜单：搜索选项 Toggle

## 概述

在应用菜单栏新增 "Search" 顶级菜单，提供 Regex、Case Sensitive、Whole Word、Match Filename 四个 Toggle 选项，并在搜索栏右侧显示激活状态标签。

## 动机

高级查询引擎已通过文本前缀（`regex:`, `case:`, `ww:`, `wfn:`）支持正则表达式、大小写敏感、全词匹配、全文件名匹配等功能，但用户必须手动输入前缀。本次变更通过菜单 UI 暴露这些功能，降低使用门槛。

## 设计决策

- **独立搜索参数**：Toggle 状态作为独立参数，不修改用户可见的搜索文本。在 ViewModel 层通过 `SearchOptions.buildQuery()` 将选项组装为前缀后传给引擎。
- **零 C++ 改动**：完全复用已有的前缀解析逻辑，无需修改 C++ 引擎或 Bridge 层。
- **互斥关系**：Regex、Whole Word、Match Filename 三者互斥；Case Sensitive 可与任何选项组合。

## 搜索选项

| 选项 | 菜单标题 | 快捷键 | 引擎前缀 |
|------|----------|--------|----------|
| 正则表达式 | Regex | ⌘R | `regex:` |
| 大小写敏感 | Case Sensitive | ⌘⇧C | `case:` |
| 全词匹配 | Whole Word | ⌘⇧W | `ww:` |
| 全文件名匹配 | Match Filename | ⌘⇧F | `wfn:` |

## 变更文件

| 文件 | 变更类型 | 说明 |
|------|---------|------|
| `MacEverything/App/SearchOptions.swift` | 新建 | SearchOptions 单例 + SearchOptionBadges 视图 |
| `MacEverything/App/SearchViewModel.swift` | 修改 | 集成 SearchOptions，选项变化自动重新搜索 |
| `MacEverything/App/MacEverythingApp.swift` | 修改 | 添加 Search 顶级菜单 |
| `MacEverything/App/ContentView.swift` | 修改 | 搜索栏添加选项状态标签 |
| `MacEverything.xcodeproj/project.pbxproj` | 修改 | 注册 SearchOptions.swift |

## 验证

- Release 构建成功
- HTTP API 验证：`regex:`, `case:`, `ww:`, `wfn:` 前缀均返回正确结果
- 菜单栏 "Search" 菜单可见，4 个 Toggle 可切换
- 搜索栏右侧显示激活选项彩色标签，点击可 toggle
