# 073: Cmd+Click to Reveal in Finder

## 概述

新增 Cmd+点击搜索结果行时，在 Finder 中显示（Reveal）文件的功能，而非打开文件。

## 变更内容

### 交互行为

| 操作 | 之前 | 之后 |
|------|------|------|
| 单击 | 无动作 | 无动作 |
| Cmd+单击 | 无动作 | **Reveal in Finder** |
| 双击 | 打开文件 | 打开文件（不变） |
| Cmd+双击 | 打开文件 | **Reveal in Finder** |
| 右键菜单 | Open / Reveal / Copy Path | 不变 |

### 修改文件

- `MacEverything/App/ResultRow.swift` — 文件搜索结果行，添加 Cmd+单击和 Cmd+双击检测
- `MacEverything/App/ContentResultRow.swift` — 内容搜索结果行，同上

### 技术实现

- 使用 `NSEvent.modifierFlags.contains(.command)` 在 tap gesture 回调中检测 Cmd 键状态
- `.onTapGesture(count: 2)` 在 `.onTapGesture(count: 1)` 之前注册，确保双击优先级正确
- 复用已有的 `revealInFinder()` 方法（`NSWorkspace.shared.selectFile`）

## 验收

- 构建成功
- 全部测试通过
