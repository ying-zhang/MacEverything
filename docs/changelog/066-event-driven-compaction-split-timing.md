# 066: Event-Driven Content Compaction + Split Startup Timing

## 问题

### P4: 内容索引 compaction 每 300s 固定轮询，绝大多数时候跳过（无变更）
- 日志分析显示 3.5h 会话中有 18+ 次 "no mutations since last compact" 跳过
- 内容索引变更极少（仅在文件内容变化时），固定轮询是资源浪费
- 与主索引（IndexPersistence）不同：主索引因 FSEvents 持续产生变更，自适应定时器是合理的

### P5: "Background replay succeeded" 日志把多个阶段耗时混为一谈
- 原始日志: `Background replay succeeded: 4740690 records in 15.4944s`
- 实际由三部分组成：索引加载 ~14.5s + WAL 重放 ~0.39s + FSEvents 重放 ~0.45s
- 无法区分哪个阶段是瓶颈

## 修复

### P4: ContentIndexPersistence 改为事件驱动 compaction

**根因**：ContentIndexPersistence 使用了 CompactionTimer（300s 固定间隔轮询），但内容索引变更频率极低。

**修复策略**：
- 移除 CompactionTimer 依赖，改用 GCD `dispatch_after()` + 原子 CAS 防重入
- `walAppendAdd()` / `walAppendRemove()` 写入 WAL 后自动调度 compaction
- 60s 防抖延迟（`kCompactionDelaySec`），最后一次变更后 60s 才触发
- 只有 WAL 有实际写入时才会调度，消除空轮询

**改动文件**：
- `ContentIndexPersistence.h` — 移除 `CompactionTimer`，新增 `dispatch_queue_t` + `std::atomic<bool>` + `scheduleCompaction()`
- `ContentIndexPersistence.cpp` — 重写 `startAutoCompaction()` / `stopAutoCompactionAndWait()`，新增 `scheduleCompaction()`

### P5: 拆分增量启动日志为独立阶段

**根因**：`incrementalStart` 时间戳在 `persistence->load()` 之前设置，但日志在 FSEvents replay 完成后才输出，将所有阶段合并为一个数字。

**修复策略**：
- 新增 `indexLoadDone` 时间戳，在 `persistence->load()` 完成后立即记录
- `backgroundSyncEngine` 新增参数 `indexLoadDone`
- 日志拆分为：`index load Xs, FSEvents replay Ys, total Zs`
- 同时修复 fallback full scan 路径的日志

**改动文件**：
- `ServiceEngine.h` — `backgroundSyncEngine` 签名增加 `indexLoadDone` 参数
- `ServiceEngine.cpp` — 记录 `indexLoadDone` 时间戳，传递到 `backgroundSyncEngine`，拆分两处日志

### IndexPersistence 分析结论
- IndexPersistence 使用自适应定时器（根据脏页比率和 WAL 大小动态调整间隔）
- FSEvents 持续产生变更，定时器几乎不会空转
- 不需要改为事件驱动，保持现状

## 测试

新增 `tests/test_event_driven_compaction.h`（Part 43，5 个测试用例）：
1. walAppendAdd 触发 WAL 写入 → compact 正常工作
2. walAppendRemove 保持 WAL dirty 状态
3. 无变更时 compact 被跳过（零浪费验证）
4. start/stop 生命周期无崩溃
5. 多线程并发 walAppendAdd + compact 正确性

全量测试：10770 tests passed, 0 failed

## 效果

- P4: 消除 18+/3.5h 的空 compaction 轮询，仅在有实际内容变更时触发
- P5: 启动日志可直接看出索引加载与 FSEvents 重放各自耗时
