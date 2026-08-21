# 074 — MCP Server (Model Context Protocol)

## 概要

为 MacEverything 添加 MCP (Model Context Protocol) 服务器，使 Claude Code、Cursor 等 MCP 兼容的 LLM 客户端可以直接调用文件搜索能力。

## 架构设计

```
Claude Code / Cursor
       | (stdio: JSON-RPC 2.0, 换行分隔)
       v
  MacEverythingMCP (独立 CLI 可执行文件)
       | (HTTP GET)
       v
  MacEverything.app (localhost:19860)
```

采用 HTTP 代理模式：MCP Server 是一个轻量级 stdio 进程，将 MCP 工具调用转发到已运行的 MacEverything HTTP API。

## 暴露的 MCP Tools

| Tool | 描述 | 参数 | HTTP API |
|------|------|------|----------|
| `search_files` | 按文件名搜索 | `query` (必需), `limit` (可选) | `GET /api/search` |
| `search_content` | 全文内容搜索 | `query` (必需), `limit` (可选) | `GET /api/search/content` |
| `recent_files` | 最近修改文件 | `limit` (可选) | `GET /api/recent` |
| `index_status` | 索引状态 | 无 | `GET /api/status` |

## 实现细节

- **协议版本**: 2025-03-26
- **传输方式**: stdio (换行分隔的 JSON-RPC 2.0)
- **HTTP 客户端**: POSIX socket 直连 localhost
- **JSON 处理**: 手工解析/构建，与项目现有风格一致，无新依赖
- **错误处理**: HTTP 连接失败返回 `isError: true`，未知方法返回 `-32601`

## 新增文件

- `MacEverything/CLI/mcp_main.mm` — MCP Server 主程序（Objective-C++）
- `tests/test_mcp_protocol.h` — 协议级集成测试 (9 个测试场景, 29 个断言)

## 修改文件

- `MacEverything.xcodeproj/project.pbxproj` — 添加 MacEverythingMCP target
- `test_all.cpp` — 注册 Part 49 (MCP protocol tests)

## 使用方式

在 Claude Code 的 settings 中添加：

```json
{
  "mcpServers": {
    "maceverything": {
      "command": "/path/to/build/Release/MacEverythingMCP",
      "args": []
    }
  }
}
```

前提：MacEverything.app 必须在运行中 (HTTP API 在 localhost:19860 上)。

## 测试

- 29 个协议级断言全部通过 (Part 49)
- 覆盖: initialize, tools/list, ping, unknown method, notification, unknown tool, missing parameter, empty lines, invalid JSON
