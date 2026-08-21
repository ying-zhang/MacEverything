# 118 — 提取 `preprocessQuery()` 函数

## 概述

将 `SearchEngine::query()` 中内联的波浪号展开代码重构为独立的静态函数 `preprocessQuery()`，将所有查询预处理逻辑集中到一处。

## 动机

在波浪号展开修复（#117）之后，展开逻辑内联在 `query()` 中。随着未来可能新增更多预处理步骤（如环境变量展开、别名解析），一个专用函数可以保持查询入口的整洁，并提供一个统一的输入规范化位置。

## 变更内容

### `MacEverything/Core/SearchEngineQuery.cpp`

- **新增** `static std::string preprocessQuery(const std::string& raw)` —
  执行所有路由前的查询规范化。当前处理：
  1. 前导 `~` 展开为 `$HOME`
- **修改** `SearchEngine::query()` — 将内联的波浪号展开替换为调用 `preprocessQuery(keyword)`，将处理结果（`processed`）传递给 `hasAdvancedSyntax()`、`toLower()`、`parseQuery()` 以及所有下游路径。

## 验证

- **单元测试**：Part 65（7 个波浪号展开测试）— 全部通过。
- **构建**：`xcodebuild` Release 构建成功。
- **HTTP**：`curl "localhost:19860/api/search?q=~/*/*.txt"` 返回预期结果（如 `/Users/username/Downloads/f1.txt`）。

## 风险

零风险 — 纯代码提取，无行为变更。所有现有测试继续通过。
