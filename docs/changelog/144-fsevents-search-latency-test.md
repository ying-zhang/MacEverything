# 144: FSEvents → Search Latency Test (Part 76)

## 概述
新增集成测试 Part 76，测量从文件创建到通过 SearchEngine::query() 可搜索的端到端延迟。

## 变更内容

### 新增文件
- `tests/test_fsevents_search_latency.h` — Part 76 测试实现

### 修改文件
- `test_all.cpp` — 集成 Part 76（include、help text、--fast/默认 part set、dispatch）

## 实施细节

### 测试流程
1. 创建临时目录，启动 ServiceEngine（含 full scan + FSEvents monitoring）
2. 通过 `posix_spawn` 在子进程中逐个创建 100 个文件
3. 每创建一个文件后，以 5ms 间隔轮询 `SearchEngine::query()` 直到找到或超时（5s）
4. 收集延迟数据，输出统计（min/max/mean/median/P90/P99）和分布直方图
5. 断言：P99 < 1000ms，Median < 600ms

### 关键发现：kFSEventStreamCreateFlagIgnoreSelf
初始实现中所有 100 个文件均超时（0/100 found）。根因分析：

`FileSystemWatcher.cpp` 使用了 `kFSEventStreamCreateFlagIgnoreSelf` 标志，macOS 会静默丢弃由同一进程产生的 FSEvents 事件。测试进程自身创建文件导致事件被忽略。

**修复方案**：使用 `posix_spawn` 从子进程创建文件，绕过 IgnoreSelf 过滤。

### 测试结果
- 100/100 文件均在超时前找到
- Min: ~45ms, Median: ~300ms, P99: ~349ms
- 延迟分布与 FSEvents 0.3s coalesce window 一致
- 全部 7 项 check 通过

## 测试命令
```bash
./test_all --part 76
```
