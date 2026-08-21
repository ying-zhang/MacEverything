# 092: v5 Paged Persistence — Path Dictionary + namePool Direct Write

## 背景

v4 持久化格式中，每条记录冗余存储完整路径字符串。加载时需要：
1. 对所有文件名做 `toLower()` 构建 `namePool_`（约占加载时间 15%）
2. 通过 hash map 去重路径构建 `pathPool_` + `lowerPathPool_`（约 10-15%）

这两步在记录数量达到百万级时成为加载瓶颈。

## 方案

### v5 二进制格式

**`.ptable` 扩展（version 1→2）**：在页表条目之后追加两个路径字典（原始大小写 + 小写），每个字典包含：
- 连续字符缓冲区 + 条目数组（offset+length）
- CRC32 完整性校验

**`.pages` 每条记录格式**：
```
pathIndex(4) + origNameLen(2) + origName(N) + lowerNameLen(2) + lowerName(M)
+ type(1) + size(8) + modTime(8) + inode(8) + devId(4)
```

记录通过 `pathIndex` 引用路径字典中的条目，而非存储完整路径字符串。

### 预期收益
- 加载速度提升 ~25-30%（跳过 toLower + 路径去重）
- 磁盘空间减少 ~38%（路径去重存储）

## 实施

### 修改的文件

| 文件 | 变更 |
|------|------|
| `StringPool.h` | 新增 `loadRaw()` 方法，支持从磁盘二进制数据零拷贝构建 |
| `SearchEngine.h/cpp` | 新增 `loadRecordsV5()`、`forEachRecordInRangeV5()`、`pathPoolSnapshot()`/`lowerPathPoolSnapshot()` |
| `PagedIndexWriter.h/cpp` | v5 序列化/反序列化、路径字典 I/O、版本检测、v4→v5 惰性迁移 |
| `test_all.cpp` | 注册 Part 53 |
| `tests/test_paged_persistence_v5.h` | 10 个 v5 测试用例 |

### 核心实现

1. **StringPool.loadRaw()**：从磁盘直接加载字符缓冲区和条目数组，无需逐字符串插入
2. **SearchEngine.loadRecordsV5()**：接收预分离的 SOA 数据（records、lowerNames、pathIndices、pathDict、lowerPathDict），跳过 toLower 和路径去重
3. **PagedIndexWriter v5 序列化**：fullRewrite/flushDirtyPages 时快照 pathPool/lowerPathPool，使用 forEachRecordInRangeV5 遍历记录
4. **惰性迁移**：v4 文件正常加载（走 loadRecords），下次 flush/fullRewrite 自动写出 v5 格式

### 测试覆盖（Part 53，10 个测试）

1. 基本往返（2048 条记录）
2. 增量刷新 + 新路径
3. 路径字典完整性（1500 条唯一路径）
4. 路径字典 CRC 损坏检测
5. 页面 blob CRC 损坏检测
6. v4→v5 迁移
7. 墓碑记录保留
8. 文件名大小写保留
9. 空引擎往返
10. 死空间回收

## 验证

- Part 32（v4）+ Part 53（v5）全部 127 个测试通过
- 全量快速测试 11,037 个全部通过
- 应用构建、打包、启动成功
- HTTP 搜索 `localhost:19860` 功能正常
