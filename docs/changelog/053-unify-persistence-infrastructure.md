# 053: 统一持久化基础设施

## 背景

项目中两套持久化系统（`IndexPersistence` 和 `ContentIndexPersistence`）独立演化出了几乎相同的基础设施：

- WAL 生命周期管理（open/close/swap/rename）
- CRC32 校验与 fsync 批处理
- GCD 定时器驱动的自动 compaction
- 原子文件写入（tmp + fsync + rename）

但 `ContentIndexPersistence` 缺失多项关键能力：
1. WAL 内部 dirty/entryCount/size 追踪
2. compact 阈值跳过（避免频繁无效 compaction）
3. 自适应定时器间隔

此外，`ContentIndex::indexFile()` 存在根因 bug：对已索引且内容未变的文件仍返回 `true`，导致调用方误以为内容有变化，脏检测被架空。

## 设计原则

- **不做深层继承**：两套 WAL 序列化格式完全不同（FileRecord+path vs fileIndex+hash+trigrams），强行抽基类只增加复杂度
- **提取共享工具 + 对齐能力**：共享 GCD 定时器，将 IndexPersistence 的成熟策略回填到 ContentIndexPersistence
- **保持简洁**：不引入与修复无关的新功能

## 实施步骤

### Step 1: 修复根因 bug — `ContentIndex::indexFile()` 返回值

| 文件 | 改动 |
|------|------|
| `ContentIndex.cpp` | 内容未变时 `return true` → `return false` |
| `ContentIndex.h` | 更新注释说明返回值语义 |
| `tests/test_content_index.h` | 新增测试：同一文件两次 indexFile，第二次返回 false |

### Step 2: 创建 `CompactionTimer` 共享组件

新建 `MacEverything/Core/CompactionTimer.h` 和 `CompactionTimer.cpp`，封装 GCD dispatch_source_t + serial queue：

- `start(intervalSec, callback, queueLabel)` — 启动定时器
- `stopAndWait()` — 停止并等待回调完成
- `reschedule(intervalSec)` — 动态调整间隔
- `isRunning()` / `currentInterval()` — 状态查询

新增测试 `tests/test_compaction_timer.h`。

### Step 3: 给 `ContentIndexWAL` 补齐追踪能力

对齐 IndexWAL 已有的能力，在 ContentIndexWAL 中添加：

- `uint64_t entryCount_` — 累计条目数
- `size_t currentSize_` — 内存追踪文件大小（替换 fstat 系统调用）
- `std::atomic<bool> dirty_` — WAL 级别脏标记
- 公共访问器：`entryCount()` / `isDirty()` / `clearDirty()` / `currentSize()`

新增测试 `tests/test_content_wal_tracking.h`。

### Step 4: `ContentIndexPersistence` 多级脏检测 + compact 阈值

- 移除 ContentIndexPersistence 自身的 `std::atomic<bool> dirty_` 成员，改用 WAL 自身的 `isDirty()`
- `compact()` → `compact(bool force = false)`，对齐 IndexPersistence 签名
- 添加 `kCompactThreshold = 50`

compact 逻辑变为：
1. `wal_->isDirty()` 为 false → 跳过
2. `!force && wal_->entryCount() < kCompactThreshold` → 跳过
3. 执行 compaction

新增测试 `tests/test_content_compact_threshold.h`。

### Step 5: 集成 CompactionTimer 到 IndexPersistence

- 替换 `dispatch_source_t` + `dispatch_queue_t` + `rescheduleTimer()` 为 `CompactionTimer timer_`
- `startAutoCompaction` / `stopAutoCompactionAndWait` 简化为 CompactionTimer API 调用
- 代码从 ~40 行缩减到 ~15 行

### Step 6: 集成 CompactionTimer + 自适应间隔到 ContentIndexPersistence

- 同样替换原始 dispatch 代码为 CompactionTimer
- 添加自适应间隔常量和 `computeAdaptiveInterval()` 方法：
  - WAL size > 5MB → 60s（最短间隔）
  - entryCount > 100 → 300s（基础间隔）
  - 否则 → 600s（最长间隔）

### Step 7: 添加到 Xcode 项目 + 验证

- 在 `project.pbxproj` 中注册 CompactionTimer.cpp/.h（PBXBuildFile、PBXFileReference、PBXGroup、PBXSourcesBuildPhase）
- xcodebuild Release 构建通过
- `./test_all --fast` 全部 10,674 测试通过

## 涉及文件

| 类型 | 文件 |
|------|------|
| **新建** | `CompactionTimer.h`, `CompactionTimer.cpp` |
| **新建测试** | `test_compaction_timer.h`, `test_content_wal_tracking.h`, `test_content_compact_threshold.h` |
| **修改** | `ContentIndex.cpp`, `ContentIndex.h`, `ContentIndexPersistence.h/.cpp`, `IndexPersistence.h/.cpp`, `project.pbxproj`, `test_all.cpp`, `test_content_index.h`, `test_critical_high.h`, `test_dirty_compaction.h`, `test_wal_rename_chain.h` |

## 验证结果

- xcodebuild Release: **BUILD SUCCEEDED**
- test_all --fast: **10,674 passed, 0 failed**
- 17 files changed, +535 / -106 lines
