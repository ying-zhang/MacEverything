# 013 - 10 个 CRITICAL/HIGH 级 Bug 修复 (C3, C4, H1, H3-H9)

- **类型**: bugfix
- **严重级别**: P0-CRITICAL / P1-HIGH
- **日期**: 2026-04-14
- **Commit**: `09bdf7f`

## 问题描述

Core 层存在 2 个 CRITICAL 和 8 个 HIGH 严重度 bug，涉及数据竞争、性能瓶颈和资源泄漏。

## 根因分析与修复方案

### CRITICAL
- **C3**: FileSystemWatcher 的 callback/stop 存在数据竞争 → 使用 `dispatch_sync` 排空回调队列
- **C4**: CRC32 表初始化存在多线程竞争 → 替换为 C++11 线程安全的 magic static

### HIGH
- **H1**: `toLower` 使用 CoreFoundation 开销大 → 添加 ASCII 快速路径，ContentIndex 中去重
- **H3**: 内容索引单线程处理 → 使用 `dispatch_apply` 并行化
- **H4**: 批量加载时排序向量 O(N²) 插入 → 改为 `push_back` + 最终排序
- **H5**: `generateSnippet` 读取完整 1MB 文件 → 改为 64KB 分块读取 + 边界重叠
- **H6**: pathIndex 构建单线程 → 并行化字符串计算
- **H7**: `query()` 在并行扫描期间持有 `shared_lock` → 快照后释放锁模式
- **H8**: `rebuildContentIndex` 泄漏旧 persistence timer → 替换前先 stop
- **H9**: compaction timer 中使用裸 watcher 指针 → 改用 `shared_ptr` 捕获

## 影响的文件

- `MacEverything/Core/SearchEngine.cpp` / `.h`
- `MacEverything/Core/ContentIndex.cpp`
- `MacEverything/Core/FileSystemWatcher.cpp`
- `MacEverything/Core/IndexPersistence.cpp`

## 测试覆盖

281 个 fast 测试通过。Xcode Release 构建成功。
