# 059 — Fix force-compact skip logic for WAL with stale entries

## 问题

应用退出时调用 `compact(force=true)` 来刷写 WAL，但如果 WAL 中的条目是上次会话的残留（在 `load()` 时重放过），本次会话没有新的写入操作，`isDirty()` 返回 false，导致 force-compact 被错误跳过。

表现：content WAL 文件在每次重启后持续保留 131 条旧条目，每次启动都需要重放，造成不必要的 I/O 和启动延迟。

## 根因分析

`isDirty()` 追踪的是**当前会话**的变更（通过 `appendAdd`/`appendRemove` 设置），而非 WAL 文件中是否有数据。应用重启后：

1. `load()` 重放 WAL 中的旧条目到内存索引
2. `attachWAL()` 以 `"ab"` 模式打开同一 WAL 文件（不截断）
3. `open()` 将 `dirty_` 设为 false，`entryCount_` 设为 0
4. 退出时 `compact(force=true)` 检查 `isDirty()` → false → 跳过
5. WAL 文件永远不会被清理

关键区别：
- `isDirty()` / `entryCount_`：会话级追踪，重启后重置
- `currentSize()`：文件级追踪，通过 `ftell()` 初始化，反映 WAL 文件的实际大小

## 修复方案

将 `force=true` 路径的跳过条件从 `isDirty()` 改为 `currentSize() <= kWALHeaderSize`：

- WAL header = 8 bytes（magic + version，各 uint32_t）
- 文件大小 > 8 bytes → 有数据（可能是新的或旧的）→ 执行 compact
- 文件大小 <= 8 bytes → 纯头部，无数据 → 跳过

非 force 路径保持不变（仍使用 `isDirty()` + `entryCount` 阈值）。

同时对 `IndexPersistence::flush()` 应用了相同的修复以保持一致性。

## 涉及文件

| 文件 | 改动 |
|------|------|
| `MacEverything/Core/ContentIndexPersistence.cpp` | compact() 多级跳过逻辑重构 |
| `MacEverything/Core/IndexPersistence.cpp` | flush() 多级跳过逻辑重构 |
| `tests/test_content_compaction_guard.h` | 新增 Test 4：验证 force-compact 在 WAL 有旧条目时正确执行 |

## 测试

- Test 4 模拟完整的重启场景：创建索引 → 写 WAL → 重新加载 → compact(false) 跳过 → compact(true) 执行
- 全量 `--fast` 测试通过（10686 tests, 0 failures）
- 独立 Part 19 性能测试确认无性能回归
