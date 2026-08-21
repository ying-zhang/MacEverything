# 099 - 窗口激活时自动聚焦搜索框

## 背景

当用户通过全局快捷键（Option+Space）或状态栏菜单将 MacEverything 窗口切换到前台时，搜索输入框没有显式的焦点管理。虽然 `makeKeyAndOrderFront` 会让窗口成为 key window，macOS 默认行为通常会将焦点给到第一个可交互控件，但这并不总是可靠——如果用户之前点击了结果列表或其他控件，再次激活窗口时焦点可能不在搜索框上，导致用户需要额外点击才能开始输入。

## 修改内容

### 文件：`MacEverything/App/ContentView.swift`

1. 添加 `@FocusState private var isSearchFieldFocused: Bool` 属性
2. 给搜索 `TextField` 添加 `.focused($isSearchFieldFocused)` 修饰符
3. 在 `NSApplication.didBecomeActiveNotification` 回调中设置 `isSearchFieldFocused = true`

## 影响范围

- 仅修改 SwiftUI 界面层，不涉及 C++ 核心引擎或桥接层
- 改动量极小（3 行新增），风险低
- 不影响现有搜索、Tab 补全、清除按钮等功能

## 验证方式

- 使用 Option+Space 隐藏后再呼出窗口，验证焦点在搜索框
- 点击结果列表后切换到其他 app，再切回，验证焦点回到搜索框
- 从状态栏菜单点击 Show MacEverything，验证焦点在搜索框
