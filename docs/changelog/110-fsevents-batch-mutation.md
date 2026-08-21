# 110 — FSEvents Batch Mutation

## 问题

FSEvents 回调中每个文件事件独立调用 `removeByPath()` / `updateByPath()`，每次都获取 `unique_lock`。当一批 FSEvents 包含 N 个事件时，会产生 N 次独占锁获取，导致并发查询的 `lock_wait` 高达 166ms。

## 根因

`removeByPath()` 和 `updateByPath()` 各自持有 `unique_lock<shared_mutex>`。FSEvents 回调逐事件调用这些方法，反复获取/释放独占锁，与并发的查询线程（持有 `shared_lock`）频繁竞争。

## 修复方案

### 1. SearchEngine 重构

- 从 `removeByPath()` 和 `updateByPath()` 提取无锁变体 `removeByPathUnlocked()` / `updateByPathUnlocked()`
- 原方法变为薄 wrapper：获取 `unique_lock` → 调用 unlocked 变体
- 新增 `MutationOp` 结构体（REMOVE / UPDATE 联合类型）
- 新增 `batchMutate(vector<MutationOp>&&)`：在单次 `unique_lock` 下遍历执行所有 ops

### 2. ServiceEngine+FSEvents 重写

- `applyFSEvents()` 和 `startMonitoring()` 回调改为 collect-then-batch 模式：
  1. 遍历事件，构建 `ops` 向量和 `contentUpdates` 列表
  2. 先应用 content index 更新（使用自己的锁）
  3. 调用 `engine->batchMutate(std::move(ops))` 批量应用

### 3. 测试（Part 61）

6 组测试共 21 个断言：
- 空 ops 无副作用
- 混合 REMOVE + UPDATE 正确性
- 批量操作后查询一致性
- 删除不存在路径不崩溃
- 并发 batchMutate + query 线程安全
- WAL 日志记录完整性

## 变更文件

| 文件 | 变更 |
|------|------|
| `MacEverything/Core/SearchEngine.h` | 新增 MutationOp 结构体、batchMutate() 公有方法、*Unlocked() 私有方法 |
| `MacEverything/Core/SearchEngine.cpp` | 提取 unlocked 变体，新增 batchMutate() 实现 |
| `MacEverything/Core/ServiceEngine+FSEvents.cpp` | applyFSEvents() 和 startMonitoring() 回调改为 collect-then-batch |
| `tests/test_fsevents_batch.h` | Part 61 测试文件 |
| `test_all.cpp` | 注册 Part 61 |

## 性能影响

- N 个 FSEvents 事件：锁获取从 N 次降为 1 次
- 查询 lock_wait 预期从 ~166ms 降至接近 0
- 无额外内存开销（ops 向量为临时分配）

## 验证

- `./test_all --part 61`：21/21 PASS
- `./test_all --fast`：11509/11509 PASS，0 FAIL
