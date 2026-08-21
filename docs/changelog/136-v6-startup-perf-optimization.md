# #136 v6 启动性能优化 — Phase 1 加速

## 概述

优化 v6 Flat SoA 格式的 Phase 1 启动路径，通过消除冗余数据结构、减少内存分配、直接 I/O 和硬件 CRC32 加速，大幅缩短启动到可搜索的时间。

## 问题分析

v6 格式实现后（#133），Phase 1 耗时 ~6.3s（5.5M 记录），远超目标。瓶颈分解：

| 步骤 | 根因 | 估算耗时 |
|------|------|----------|
| records_ 重建 | 5.5M string 堆分配 | ~1.5s |
| pathIndex_ 并行构建 | 16.5M string 分配 | ~1.5s |
| pathIndex_ 插入 + tombstone 去重 | unordered_set 开销 | ~1.5s |
| I/O + 中间拷贝 | sectionMap 双拷贝 | ~0.5s |
| CRC 校验 | 逐字节软件 CRC32 | ~1s |

## 实施方案

### 1. 消除 records_ 向量（节省 ~1.5s + ~440MB 内存）

删除 `std::vector<FileRecord> records_` 成员，所有代码改为直接访问 SoA 列：
- `records_[i].type` → `types_[i]`
- `records_[i].name` → `origNamePool_.view(i)`
- `records_[i].size` / `.modTime` / `.inode` / `.devId` → 对应 SoA 列

受影响文件：SearchEngine.h/cpp, SearchEngineQuery.cpp, SearchEngineStructuredQuery.cpp, SearchEngineAdvancedQuery.cpp, SearchEngineIndex.cpp, SearchEnginePersistence.cpp, SearchEngineV6.cpp, ContentIndex.h

新增 `recordCount()` 方法替代 `records_.size()`。`getRecord()` 改为从 SoA 列重建 FileRecord。`evalFilter()`/`evalTerm()` 参数改为 index-based。

### 2. 优化 pathIndex_ 构建（节省 ~2s）

- 并行构建改用 `string_view`：`lowerPathPool_.view()` + `namePool_.view()` 替代 `.str()` 分配
- `makeFullPath()` 签名改为 `(string_view, string_view)`，使用 `reserve + append` 
- 插入时跳过 tombstoned 记录（`types_[i] == 0`），避免无效 insert
- tombstone 去重：`vector<bool>` 替代 `unordered_set<uint32_t>`（O(1) 查找）

### 3. 直接 I/O + 移动语义 StringPool（节省 ~0.5s）

- `StringPool` 新增 `loadRaw(vector<char>&&, vector<Entry>&&)` 移动语义接口
- `FlatIndexWriter::load()` 重写：
  - 消除 `sectionMap` 中间层
  - Array sections 直接 fread 到目标 vector + 原地 CRC 验证
  - StringPool sections 解析后通过 move 交付，无二次拷贝
  - Section 查找改用固定大小数组替代 unordered_map

### 4. ARM 硬件 CRC32（节省 ~1s）

- Apple Silicon 使用 `__crc32d` 指令（ISO 3309 多项式，兼容已有数据）
- 8 字节对齐处理，尾部逐字节
- 非 ARM 架构使用 slicing-by-4 软件优化作为 fallback
- 无需文件格式变更（多项式相同）

## 修改文件清单

| 文件 | 变更 |
|------|------|
| SearchEngine.h | 删除 records_；新增 recordCount()；修改回调签名 |
| SearchEngine.cpp | SoA 全面替换；makeFullPath 改 string_view；pathIndex_ 优化 |
| SearchEngineV6.cpp | loadRecordsV6 删除 records_ 重建；pathIndex_ 优化 |
| SearchEngineQuery.cpp | records_[i].type → types_[i] |
| SearchEngineStructuredQuery.cpp | records_[i].type → types_[i] |
| SearchEngineAdvancedQuery.cpp | evalFilter/evalTerm 改 index-based |
| SearchEngineIndex.cpp | builder 函数参数改 SoA |
| SearchEnginePersistence.cpp | saveToFile 从 SoA 重建 FileRecord |
| StringPool.h | 新增 move-based loadRaw |
| FlatIndexWriter.cpp | load() 直接 I/O + 移动语义 |
| IndexWAL.cpp | ARM __crc32d + slicing-by-4 fallback |
| ContentIndex.h | 注释修正 |

## 测试

- 全量测试：11,905 tests passed, 0 failed
- v6 往返测试（12 个）：全部通过
- CRC32 兼容性：ARM 硬件实现与软件实现产生相同校验值
- 搜索功能验证：5.5M 记录加载后 API 搜索正常

## 预期效果

| 指标 | 优化前 | 优化后（预估） |
|------|--------|----------------|
| Phase 1 耗时 | ~6.3s | ~1s |
| 内存占用 | ~1.5GB | ~0.8GB |
| 搜索可用时间 | ~6.3s | ~1s |
