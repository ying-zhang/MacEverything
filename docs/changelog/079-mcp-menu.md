# 079 — MCP Integration Menu

## 概要

在 MacEverything 状态栏菜单中添加 "MCP Integration" 子菜单，用户可一键勾选/取消各 LLM 客户端（Claude Code、Cursor、Claude Desktop），自动将 MCP 配置写入或移除对应客户端的配置文件。

## 动机

MCP Server (074) 已实现，但用户需要手动编辑 JSON 配置文件才能启用。本次变更消除了这一步骤，让非技术用户也能轻松使用 MCP 集成。

## 菜单结构

```
状态栏菜单:
  Show MacEverything
  ─────────────────
  Rebuild Index
  Shortcut Settings...
  Content Settings...
  MCP Integration ▸
    ☑ Claude Code
    ☐ Cursor
    ☐ Claude Desktop
  ─────────────────
  Launch at Login
  ─────────────────
  Quit MacEverything
```

## 客户端配置文件

| 客户端 | 配置文件路径 |
|--------|-------------|
| Claude Code | `~/.claude/settings.json` |
| Cursor | `~/.cursor/mcp.json` |
| Claude Desktop | `~/Library/Application Support/Claude/claude_desktop_config.json` |

启用时写入的 MCP 配置：
```json
{
  "mcpServers": {
    "maceverything": {
      "command": "<app bundle>/Contents/MacOS/MacEverythingMCP",
      "args": []
    }
  }
}
```

## 实现细节

- JSON 读写使用 `JSONSerialization`，合并写入不破坏文件中的其他配置
- 配置目录不存在时自动创建
- MCP 二进制路径从 `Bundle.main.executableURL` 推算，确保无论 app 安装位置如何路径都正确
- `MacEverythingMCP` 通过 Copy Files Build Phase 嵌入 app bundle 的 `Contents/MacOS/`
- 添加了 target dependency 确保先编译 MCP 再构建主 app
- 菜单状态在 `menuWillOpen` 时动态刷新

## 新增文件

- `MacEverything/App/MCPConfigManager.swift` — MCP 客户端配置管理 (92行)

## 修改文件

- `MacEverything/App/AppDelegate.swift` — 添加 MCP 子菜单 (+23行)
- `MacEverything.xcodeproj/project.pbxproj` — 添加文件引用、Copy Files 阶段、target dependency (+33行)
