# FSEvents 测试 CI 失败分析（2026-08-21）

> 背景：`.github/workflows/build-macos.yml` 的 weekly `full-core-tests` job 自 2026-08-02 引入以来持续失败，每次 schedule 运行都在 FSEvents 相关用例上报错。push 分支只跑 `make test-fast`，因此这些慢速用例从未在 CI 覆盖到，直到 weekly 定时任务首次跑到。

## 结论（两句话）

失败不是 CI 环境 flaky，而是三个 FSEvents 测试从未真正在 CI 跑通过。两个独立根因：**① `IgnoreSelf` 过滤同进程写入**；**② FSEvents 报真实路径、而扫描根用符号链接路径，字符串前缀比较不匹配**。均已在本地实证并修复。

## 根因 1：`IgnoreSelf` 过滤同进程写入

- `FileSystemWatcher::startInternal` 创建流时设置了 `kFSEventStreamCreateFlagIgnoreSelf`（`FileSystemWatcher.cpp:93-96`，commit `62303cd` 引入，用于打断「压缩→FSEvents→重扫」的 OOM 反馈循环）。该 flag 会让 FSEvents 忽略**当前进程自己产生**的文件事件。
- 但 `tests/test_fsevents.h`（Part 4）和 `tests/test_e2e.h`（Part 6）直接用 `std::ofstream` / `fs::rename` / `fs::remove` 在**同一进程内**建/改/删文件，事件全被过滤，watcher 收到 0 事件 → `waitForEvents` 超时。

**实证（本地直接测 FSEvents）：**

- 同进程写入 + 无 `IgnoreSelf` → 收到 2 事件
- 同进程写入 + `IgnoreSelf` → **0 事件**
- 子进程写入 + `IgnoreSelf` → 1 事件（子进程能绕过）

## 根因 2：符号链接路径不匹配

- FSEvents 报的是**解析过符号链接的真实路径**（实测）：
  - watch `/tmp/x` → 事件报 `/private/tmp/x`
  - watch `/var/folders/…/x` → 事件报 `/private/var/folders/…/x`
- `ServiceEngine::isPathAllowedByConfig`（`ServiceEngine.cpp`）用 `pathContainsOrEquals(scanRoot, eventPath)` 做**纯字符串前缀**比较，`/tmp/…` 对不上 `/private/tmp/…`，于是事件被判「不在扫描根内」直接丢弃。
- `tests/test_fsevents_search_latency.h`（Part 76）用 `fs::temp_directory_path()` = `/var/folders/…/T/`（符号链接），且 `/private/var/folders` 又命中 `isSystemFilteredPath` 的 `/private/var/` 前缀 → 事件被双重丢弃，100 个文件全部超时。

**生产影响：** 这是潜在生产 bug——任何扫描根含符号链接组件（`/tmp`、`/var` 等）时，FSEvents 事件会被静默丢弃，文件变更无法实时进索引。普通 `~/`、`/Users/…` 扫描不受影响（非符号链接）。

## 为什么 8/2 才开始报错

`0b6a2d1`（2026-08-02 "Harden indexing and release 1.7.30"）给 workflow 新增了 weekly `full-core-tests` job（`make test-all`，含慢速 FSEvents 用例）。此前 push 只跑 `make test-fast`，这些用例从未在 CI 跑过——不是 deepseek 改坏了代码，而是一直失败的测试第一次被 CI 跑到。

## 修复

- **生产代码（`ServiceEngine.cpp`）**：新增 `pathContainsOrEqualsReal`，前缀匹配失败时用 `std::filesystem::canonical` 解析 parent 再重试；`isPathAllowedByConfig` 的根路径与排除路径两处比较都套用。
- **测试**：
  - `tests/test_helpers.h`：新增 `runShellCommand`（`posix_spawn` + `/bin/sh -c`），供 FSEvents 测试从子进程建文件。
  - `tests/test_fsevents.h`（Part 4）：5 处文件操作改子进程（create/modify/rename/delete/batch）。
  - `tests/test_e2e.h`（Part 6）：create/rename/delete 改子进程。
  - `tests/test_fsevents_search_latency.h`（Part 76）：`fs::temp_directory_path()` → `fs::current_path()`（避免 `/var/folders` 的符号链接 + 系统路径双重问题）。

## 验证

- Part 4（FSEvents 集成）：7/7 通过（此前 5 个全挂）。
- Part 6（E2E）：7/7 通过（此前 3 个挂）。
- Part 76（搜索延迟）：7/7 通过，100 文件 0 超时，P99=306ms（此前 100 个全超时）。
- 全量 `./test_all`：12290 通过 / 0 失败（含本地补编 `MacEverythingMCP` 后的 Part 49）。

修复提交：`c17dce5`（FSEvents 相关）。相关 pinyin 修复：`a68ac58`。
