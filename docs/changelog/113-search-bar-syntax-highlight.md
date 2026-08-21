# 113 - Search Bar Syntax Highlighting for Advanced Queries

## Problem

搜索栏使用 SwiftUI 原生 `TextField`，不支持富文本。用户输入 `ext:cpp hello` 时，所有文字显示为同一颜色（黑色），无法区分 filter token、操作符和搜索关键词。

## Solution

### 1. Swift-side Query Tokenizer (`QueryHighlightTokenizer`)

在 Swift 端实现轻量级 tokenizer，镜像 C++ `QueryTokenizer` 的 token 分类逻辑：

- 识别 28 个已知 filter name（case-insensitive）：`ext`, `size`, `file`, `folder`, `path`, `nopath`, `parent`, `depth`, `len`, `dm`, `dc`, `da`, `datemodified`, `datecreated`, `dateaccessed`, `case`, `nocase`, `regex`, `ww`, `wfn`, `wholeword`, `wholefilename`, `audio`, `video`, `pic`, `doc`, `exe`, `zip`, `content`, `type`
- Filter argument 内允许 `<` `>` 字符（与 C++ 行为一致，如 `size:>1mb`）
- 识别引号字符串、操作符（`|`, `!`, `<`, `>`）
- 输出 `[QueryToken]`，每个 token 包含 `NSRange` 和类型

### 2. NSViewRepresentable (`HighlightedSearchField`)

用 `NSViewRepresentable` 包装 `NSTextView` 替代原生 `TextField`：

- 单行模式（`isFieldEditor = true`）
- 每次文本变化时通过 `NSTextStorage` 应用颜色
- 支持 placeholder 文本
- 支持 Tab 键（ghost suggestion 接受）
- 支持 `@FocusState` 绑定

### 颜色方案

| Token 类型 | 颜色 |
|---|---|
| Filter name (含冒号) | `.systemPurple` |
| Filter argument | `.systemBlue` |
| Quoted string | `.systemOrange` |
| Operator (`\|`, `!`, `<`, `>`) | `.systemRed` |
| Plain word | Default label color |

## Files Changed

- **NEW** `MacEverything/App/HighlightedSearchField.swift` — Swift tokenizer + NSViewRepresentable
- **MODIFIED** `MacEverything/App/ContentView.swift` — 替换 `TextField` 为 `HighlightedSearchField`
- **MODIFIED** `MacEverything.xcodeproj/project.pbxproj` — 添加新文件到 Xcode 项目

## Testing

- Build succeeded (Release configuration)
- HTTP API 验证搜索功能正常 (`ext:cpp hello` 返回结果)
- Ghost suggestion overlay 保留
- Clear button 保留
- 窗口激活时自动聚焦保留
