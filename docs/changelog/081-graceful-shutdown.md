# 081 — Fix Graceful Shutdown: Ensure Paged Index Flush on App Quit

## 问题

`ServiceEngine::shutdown()` 从未在正常退出时执行。根因是 SwiftUI `@main` + `@NSApplicationDelegateAdaptor` 模式下 `applicationWillTerminate` 不可靠。

后果：
- WAL 条目在多次重启间累积（观察到 1901→3718→8959），启动时间退化
- paged index 从未 force-flush，每次启动都从同一个 stale eventId 加载

## 根因分析

1. **`applicationWillTerminate` 不可靠**：在 SwiftUI lifecycle 下，该方法可能不被调用
2. **`dispatch_group_wait(DISPATCH_TIME_FOREVER)` 无限阻塞**：即使 shutdown 被触发，如果后台 GCD 任务卡在 I/O，等待会无限阻塞，导致 macOS watchdog（~5s）强杀进程

## 修复方案

### 变更 1：AppDelegate.swift — 添加 `applicationShouldTerminate`

使用 `.terminateLater` 返回值，在后台线程执行 `prepareForTermination()`，完成后调用 `NSApp.reply(toApplicationShouldTerminate: true)`。

保留 `applicationWillTerminate` 作为兜底（shutdown 使用 `compare_exchange_strong` 保证幂等）。

### 变更 2：ServiceEngine.cpp — dispatch_group_wait 3s 超时

将 `DISPATCH_TIME_FOREVER` 改为 3s 超时。后台块都检查 `shuttingDown_` 标志并快速退出，通常 < 500ms 完成。超时仅作为 stuck I/O 的保险。

## 安全性

- shutdown 使用 `compare_exchange_strong` 保证幂等，多次调用安全
- 所有 7 个 `dispatch_group_async` 块检查 `shuttingDown_` 后快速退出
- `cancelContentIndexing_` 在 wait 之前设置
- GCD 块持有 `shared_ptr`，不会 use-after-free
- 总耗时 < 1.5s，远在 macOS ~5s watchdog 之内

## 变更文件

| 文件 | 变更 |
|------|------|
| `MacEverything/App/AppDelegate.swift` | 添加 `applicationShouldTerminate` 方法 |
| `MacEverything/Core/ServiceEngine.cpp` | `dispatch_group_wait` 从 FOREVER 改为 3s 超时 |

## 测试结果

- `./test_all --part 40`：15/15 PASS
- `./test_all --fast`：10838/10838 PASS
