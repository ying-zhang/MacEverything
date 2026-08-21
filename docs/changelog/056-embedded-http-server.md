# 055 — 内嵌 HTTP 服务器

## 概述

为 MacEverything 添加了内嵌 HTTP API 服务器，支持通过本地 HTTP 请求以编程方式访问搜索引擎。服务器监听 `127.0.0.1:19860`，提供 REST 风格的 JSON API，涵盖文件名搜索、内容搜索、最近文件、引擎状态和健康检查。

## 动机

外部工具、脚本和浏览器扩展需要在不通过 GUI 的情况下查询 MacEverything 索引。HTTP API 使任何 HTTP 客户端（curl、Python、Alfred 工作流等）都能访问索引，同时通过仅绑定本地地址确保安全性。

## 变更内容

### 新增文件

- **`MacEverything/Core/HttpServer.h`**（约 55 行）：声明 `HttpServer` 类，包含基于 POSIX socket 的 accept 循环、HTTP 请求解析和路由分发。
- **`MacEverything/Core/HttpServer.cpp`**（约 340 行）：完整实现，包括：
  - POSIX socket 生命周期管理（`socket` / `bind` / `listen` / `accept` / `close`）
  - 基于 `poll()` 的 accept 循环，500ms 超时轮询 `running_` 标志以实现干净关闭
  - HTTP 请求解析（方法、路径、查询字符串及 URL 解码）
  - JSON 响应生成与正确的字符串转义
  - CORS 头（`Access-Control-Allow-Origin: *`）支持浏览器客户端

### 修改文件

- **`MacEverything/Bridge/MacSearchBridge_Internal.h`**：添加 `#include "HttpServer.h"` 和 `std::shared_ptr<HttpServer> _httpServer` 成员变量。
- **`MacEverything/Bridge/MacSearchBridge.h`**：添加 `startHttpServer:` 和 `stopHttpServer` 方法声明。
- **`MacEverything/Bridge/MacSearchBridge.mm`**：实现 `startHttpServer:` / `stopHttpServer` 桥接方法；在 `prepareForTermination` 开头添加 `stopHttpServer` 调用。
- **`MacEverything/App/AppDelegate.swift`**：在 `applicationDidFinishLaunching` 中添加 `MacSearchBridge.shared().startHttpServer(19860)`。
- **`MacEverything.xcodeproj/project.pbxproj`**：将 HttpServer.h 和 HttpServer.cpp 注册到构建配置中。

## API 端点

| 端点 | 参数 | 说明 |
|---|---|---|
| `GET /api/search?q=&limit=` | q（必填），limit（默认 100，最大 10000） | 文件名子串搜索 |
| `GET /api/search/content?q=&limit=` | q（必填），limit（默认 100，最大 10000） | 全文内容搜索 |
| `GET /api/recent?limit=` | limit（默认 100，最大 10000） | 最近修改的文件 |
| `GET /api/status` | 无 | 记录计数和内容索引统计 |
| `GET /api/health` | 无 | 健康检查（返回 `{"status":"ok"}`） |

## 设计决策

1. **仅绑定本地地址**（`127.0.0.1`）：不暴露到网络，仅本地进程可连接。
2. **无第三方依赖**：使用 POSIX socket 和 `std::ostringstream` 手写 HTTP 解析和 JSON 序列化。
3. **同步逐连接处理**：对于低并发的本地 API 可接受，每个连接在 accept 线程中顺序处理。
4. **干净关闭**：accept 循环通过 `poll()` 以 500ms 超时检查 `running_` 标志，确保应用终止时服务器及时停止。
5. **端口 19860**：选用不常见的端口，避免与其他本地服务冲突。

## 测试

- 构建验证：项目在添加新文件后编译成功。
- 手动测试：应用启动后，`curl http://127.0.0.1:19860/api/health` 应返回 `{"status":"ok"}`。
