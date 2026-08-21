# 080 — MCP Integration in App Menu Bar

## 概要

将 MCP Integration 子菜单同步添加到应用前台菜单栏（SwiftUI `CommandGroup`），使状态栏菜单和窗口菜单栏保持一致。

## 动机

079 中 MCP Integration 子菜单仅出现在状态栏（tray icon）菜单中。当应用窗口在前台时，用户在屏幕顶部的菜单栏中找不到 MCP 选项。macOS 应用应在两处菜单中提供一致的功能入口。

## 修改文件

- `MacEverything/App/MacEverythingApp.swift` — 在 `.commands` 的 `CommandGroup(after: .appSettings)` 中，Content Settings 之后添加 `Menu("MCP Integration")` 子菜单，内含 3 个 `Toggle`（Claude Code / Cursor / Claude Desktop），通过 `Binding(get:set:)` 直接调用 `MCPConfigManager` 读写配置状态 (+11行)

## 实现细节

- SwiftUI `Toggle` + `Binding(get:set:)` 模式，每次打开菜单时从磁盘读取配置文件判断勾选状态
- `MCPClient` 枚举已满足 `Hashable`（自动合成），可直接用于 `ForEach`
- 无需新增文件或修改 `MCPConfigManager.swift`
