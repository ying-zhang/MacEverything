# 058 — HTTP 管理 API 扩展

## 概述

扩展内嵌 HTTP 服务器，新增管理类 API 端点，使自动化工具和脚本可通过 HTTP 控制索引重建和内容索引配置。

## 新增端点

| 端点 | 方法 | 说明 | 响应 |
|---|---|---|---|
| `POST /api/index/rebuild` | POST | 触发文件名索引重建（异步） | 202 Accepted |
| `POST /api/content/rebuild` | POST | 触发内容索引重建（异步） | 202 Accepted |
| `GET /api/content/config` | GET | 返回当前内容索引配置 | 200 OK |
| `POST /api/content/config` | POST | 更新内容索引配置（支持部分更新） | 200 OK |

### 请求/响应示例

```bash
# 获取内容索引配置
curl http://127.0.0.1:19860/api/content/config
# {"extensions":["h","cpp","swift","py"],"maxFileSize":1048576}

# 更新配置（支持部分更新）
curl -X POST -H "Content-Type: application/json" \
  -d '{"extensions":["h","cpp","swift"],"maxFileSize":2097152}' \
  http://127.0.0.1:19860/api/content/config

# 触发重建
curl -X POST http://127.0.0.1:19860/api/index/rebuild
curl -X POST http://127.0.0.1:19860/api/content/rebuild
```

## 架构设计

### AdminCallbacks 回调模式

HTTP 服务器是纯 C++ 实现，无法直接调用 ObjC++ Bridge 方法。解决方案使用 `std::function` 回调：

1. `HttpServer::AdminCallbacks` — 在 `HttpServer.h` 中声明的包含 5 个 `std::function` 回调的结构体
2. `HttpServer::setAdminCallbacks()` — Bridge 在 `start()` 之后调用的设置方法
3. Bridge 注入闭包来调用对应的管理操作：
   - `onRebuildIndex` → 发送 `NSNotification @"rebuildIndex"`（由 SearchViewModel 处理）
   - `onRebuildContentIndex` → 在 utility 队列上调度 `[self rebuildContentIndex]`
   - `onSetContentConfig` → 通过线程安全访问器调用 `ContentIndex::setExtensions()` / `setMaxFileSize()`
   - `onGetContentExtensions` / `onGetContentMaxFileSize` → 通过线程安全访问器返回当前配置

### POST 请求支持

新增 HTTP POST 请求体读取：
- `handleConnection()` 现在解析 `Content-Length` 头，并在请求体被拆分到多个 TCP 段时读取额外数据
- `parseRequest()` 提取请求体（`\r\n\r\n` 之后的内容）
- `route()` 现在同时分发 GET 和 POST 方法到对应的处理函数
- 针对固定结构 `{"extensions":[...],"maxFileSize":N}` 的简易 JSON 解析器

## 变更文件

| 文件 | 变更内容 |
|---|---|
| `MacEverything/Core/HttpServer.h` | 添加 `AdminCallbacks` 结构体、`setAdminCallbacks()`、`HttpRequest` 中的 `body` 字段、4 个新处理函数声明 |
| `MacEverything/Core/HttpServer.cpp` | POST 请求体读取、解析器中的 body 提取、GET/POST 双路由分发、4 个新管理处理函数含 JSON 解析、202 状态文本 |
| `MacEverything/Bridge/MacSearchBridge.mm` | `startHttpServer:` 现在在 `start()` 之后注入 `AdminCallbacks` 闭包 |

## 测试

```bash
# 构建验证
xcodebuild -scheme MacEverything -configuration Release build

# 手动 API 测试（应用运行中）：
curl http://127.0.0.1:19860/api/health
curl http://127.0.0.1:19860/api/content/config
curl -X POST http://127.0.0.1:19860/api/content/config -d '{"maxFileSize":2097152}'
curl -X POST http://127.0.0.1:19860/api/content/rebuild
curl -X POST http://127.0.0.1:19860/api/index/rebuild
```
