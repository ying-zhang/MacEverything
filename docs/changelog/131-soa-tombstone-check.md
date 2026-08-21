# 131 — SoA Tombstone Check 优化

**日期**: 2026-04-19  
**分支**: `opt/linear-soa-tombstone`  
**合并**: `0f84094` Merge branch 'opt/linear-soa-tombstone'

## 背景

`advanced-linear-gcd` 搜索路径对 ~5.4M 条记录做全量线性扫描。扫描的第一步是跳过 tombstone（已删除记录），原实现使用 `records[idx].type == 0` 检查。

`records_` 是 Array-of-Structs（AoS），每个 `FileRecord` 约 88 字节（含两个 `std::string`）。仅为读取 1 字节的 `type` 字段就加载整个 88 字节缓存行，在 5.4M 记录上造成约 475MB 的无效缓存污染。

`types_`（`std::vector<uint8_t>`，SoA 列式存储）已经存在于引擎中，但 tombstone 检查未使用它。

## 变更

在 `SearchEngineAdvancedQuery.cpp` 中将所有 3 处 tombstone 检查从 AoS 改为 SoA：

1. **Trigram 过滤后并行扫描路径**（dispatch_apply 块内）：
   - `if (records[idx].type == 0) continue;` → `if (typesPtr[idx] == 0) continue;`
   - 新增 `const auto* typesPtr = types_.data();` 捕获

2. **小候选集单线程路径**：
   - 同上模式，新增 `const auto* smallTypesPtr = types_.data();`

3. **线性全量扫描路径**（`typesPtr` 已在上游捕获）：
   - `if (records[idx].type == 0) continue;` → `if (typesPtr[idx] == 0) continue;`

## 原理

- `types_[]` 是连续 1 字节数组，5.4M 记录仅占 ~5.4MB
- CPU 预取效率极高：一条缓存行（64B）覆盖 64 个 tombstone 检查
- 对比 AoS 访问（88 字节跨步），缓存利用率提升约 88x
- 只有通过 tombstone 检查的记录才访问 `records[]` 的完整字段

## 验证

- `./test_all --fast`：11815 个测试全部通过
- xcodebuild Release 构建成功
- 基准测试 R28b（预 flush 热缓存）：
  - advanced-linear-gcd avg: 158.1ms
  - linear_forced avg: 152.8ms
  - 全局 avg median: 35.1ms
- 优化对 trigram 查询无影响（这些查询本来就跳过大部分记录）
- 主要收益在 linear scan 路径，效果受 flush/VM 缓存状态影响较大

## 风险

- 极低风险：`types_[]` 与 `records_[]` 始终同步更新（同一把锁保护）
- 语义完全等价：`types_[idx] == 0` ⟺ `records_[idx].type == 0`
