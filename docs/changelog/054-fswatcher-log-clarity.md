# 054: FSWatcher 日志可读性改进

## 背景

运行日志中 FSWatcher 在增量启动时出现 started → stopped → started 的模式，看起来像一个 watcher 被反复启停，实际是两个不同实例（replay 和 live）的正常生命周期。由于共用同一个日志 tag `"FSWatcher"`，无法区分。

此外，`stop()` 在 `stream_` 为 null 时仍输出 "Stopping" 日志，导致在 watcher 未启动时调用 `stop()` 也会产生误导性日志。

## 改动

### 1. 添加实例标签（label）

- `FileSystemWatcher` 构造函数新增 `std::string label` 参数（默认 `"default"`）
- 所有日志消息前缀 `[label]`，如 `[replay] Started watching: /` 和 `[live] Stopped`
- `MacSearchBridge.mm` 中 `_watcher` 传入 `"live"`，`watcherForReplay` 传入 `"replay"`

### 2. 跳过无意义的 stop 日志

- `stop()` 开头增加 `if (!stream_ && !queue_) return;`，未运行时不输出日志

## 改动前后日志对比

**改动前：**
```
FSWatcher: Started watching: /
FSWatcher: Stopping file system watcher
FSWatcher: Started watching: /
```

**改动后：**
```
FSWatcher: [replay] Started watching: /
FSWatcher: [replay] Stopped
FSWatcher: [live] Started watching: /
```

## 涉及文件

| 类型 | 文件 |
|------|------|
| **修改** | `FileSystemWatcher.h` — 新增 `label_` 成员和构造函数参数 |
| **修改** | `FileSystemWatcher.cpp` — 日志添加 `[label]` 前缀，`stop()` 添加 early return |
| **修改** | `MacSearchBridge.mm` — 传入 `"live"` 和 `"replay"` 标签 |

## 验证结果

- xcodebuild Release: **BUILD SUCCEEDED**
- test_all --fast: **10,674 passed, 0 failed**
- 3 files changed, +7 / -5 lines
