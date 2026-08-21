# 060 - Fix FSEvents lastEventId=0 恶性循环 + replay 诊断增强 + 自适应超时

## 问题

### P0: lastEventId=0 恶性循环

全盘扫描完成后，`startMonitoringFrom:` 以 `kFSEventStreamEventIdSinceNow` 启动 live watcher，然后立即读取 `getLastEventId()` 返回 0（因为还没有任何 callback 触发过），将 0 持久化。下次启动时 replay 从 eventId=0 开始，必然失败，触发全盘扫描，如此循环。每次启动都要约 97s 全盘扫描，Two-phase instant startup 完全失效。

### 诊断不足

replay 失败时日志只有一行 `"FSEvents replay failed — background full scan"`，无法区分是超时、journal 截断还是其他原因。

### 超时策略不合理

replay 超时硬编码 10s。journal 截断时仍要等满 10s 浪费时间；事件量大但 replay 正常进行时 10s 可能不够。

## 根因分析

**根因**：`lastEventId_` 是 `std::atomic`，只在 `fseventsCallback` 中更新。当 live watcher 以 `kFSEventStreamEventIdSinceNow` 启动时，在首个 callback 到达之前 `lastEventId_` 始终为 0。而 Bridge 层在 `startMonitoring` 后立即读取并持久化了这个 0 值。

这不是一个时序竞争问题，而是一个设计假设错误：**不能用 stream-local 的 lastEventId 来代表系统级别的 event ID checkpoint**。

## 修复方案

### Fix 1: 核心修复 — 使用系统 API 获取 eventId

新增 `FileSystemWatcher::getCurrentSystemEventId()` 静态方法，调用 `FSEventsGetCurrentEventId()` 获取系统级当前 event ID，不依赖任何 stream。

在 `MacSearchBridge.mm` 的两处持久化位置（首次启动完成、background fallback 完成）将 `self->_watcher->getLastEventId()` 替换为 `FileSystemWatcher::getCurrentSystemEventId()`。

### Fix 2: replay 诊断日志增强

- 新增 `totalEventsReceived_` 原子计数器，统计 callback 中收到的原始事件数
- 在 `startInternal` 中重置计数器
- replay 失败日志改为分类诊断：
  - 超时（30s 到期）：报告超时 + 收到事件数 + journal 状态
  - journal 截断（`kFSEventStreamEventFlagMustScanSubDirs`）：报告截断 + 事件数
  - 其他原因：报告 replayDone 状态 + 事件数

### Fix 3: 自适应超时 — journal 截断提前中止

- 新增 `setEarlyAbortSemaphore(void* sem)` 方法，接受 dispatch_semaphore
- 参数使用 `void*` 避免 C++ 与 Objective-C++ 的 `dispatch_semaphore_t` ABI 不兼容问题
- journal 截断时立即 signal semaphore，replay 等待立刻中止
- 超时从 10s 延长到 30s（截断场景不会实际等 30s）

## 修改文件

| 文件 | 变更 |
|------|------|
| `MacEverything/Core/FileSystemWatcher.h` | 新增 `getCurrentSystemEventId()`、`totalEventsReceived()`、`setEarlyAbortSemaphore(void*)`；新增 `totalEventsReceived_`、`earlyAbortSem_` 成员 |
| `MacEverything/Core/FileSystemWatcher.cpp` | 实现上述新方法；callback 中累计 `totalEventsReceived_`；journal 截断时 signal 信号量 |
| `MacEverything/Bridge/MacSearchBridge.mm` | 两处 eventId 持久化改用 `getCurrentSystemEventId()`；replay 失败日志改为分类诊断；设置 earlyAbort 信号量；超时 10s→30s |
| `tests/test_fswatcher_eventid.h` | Part 37：4 个单元测试 |
| `test_all.cpp` | 注册 Part 37 |

## 测试

- Part 37（4 项）：`getCurrentSystemEventId()` 非零、单调递增、初始 `lastEventId=0`、初始 `totalEventsReceived=0`
- 全量 `--fast` 测试：10690 项全部通过
- Xcode Release 构建通过

## ABI 问题记录

`dispatch_semaphore_t` 在纯 C++ 编译中 mangled 为 `P20dispatch_semaphore_s`（指向 C struct），在 Objective-C++ (ARC) 中 mangled 为 `PU32objcproto21OS_dispatch_semaphore8NSObject`（NSObject 指针）。同一个头文件中声明 `dispatch_semaphore_t` 参数会导致 C++ `.cpp` 和 Obj-C++ `.mm` 编译单元之间的链接符号不匹配。解决方案：参数改为 `void*`，在使用侧通过 `static_cast`/`__bridge` 转换。
