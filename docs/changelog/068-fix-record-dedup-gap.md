# 068 — 修复记录去重：消除搜索结果空白行

## 问题

搜索结果列表出现可见的空白行（幽灵行）。根因：C++ SearchEngine 允许同一文件路径存在多条活跃记录。SwiftUI ForEach 使用基于路径的 ID，重复 ID 导致"幽灵行"占据垂直空间但不渲染任何内容。

HTTP API 验证确认了该问题：搜索 "test" 返回 50 条结果，但仅有 30 个唯一路径（10 组重复）。

## 根因分析

三条代码路径可能产生孤立的重复记录：

1. **`addRecord()`** — 追加新记录并更新 `pathIndex_` 指向它，但从未对同路径的旧记录做墓碑标记。旧记录保持活跃状态（type != 0），但已无法通过 `pathIndex_` 访问。

2. **`loadRecords()`** — 以"后者胜出"的语义构建 `pathIndex_`，但将 `liveCount_` 设为 `records_.size()`，未排除重复路径产生的孤立记录。

3. **`compactRecords()`** — 第二阶段仅跳过墓碑记录（type == 0），导致孤立的"活跃但未被引用"的记录被复制到压缩输出中。

## 修复方案

### 1. `addRecord()` — 插入时去重（主要修复）

追加前检查 `pathIndex_` 是否已存在同路径记录。若存在，对旧记录做墓碑标记（设 type=0、清空字段、从 trigram 索引中移除、减少 liveCount、从最近文件缓存中移除）。此模式与 `replayWALEntries()` 中已有的逻辑一致。

### 2. `loadRecords()` — 构建 pathIndex 后去重

构建 `pathIndex_`（后者胜出）后，将所有"胜出者"的索引收集到集合中。扫描所有记录：任何活跃记录若其索引不在胜出者集合中，即为孤立记录 — 对其做墓碑标记。将 `liveCount_` 设为实际活跃记录数，而非 `records_.size()`。

### 3. `compactRecords()` — 第二阶段跳过孤立记录

在 COW 压缩循环中，检查 `type != 0` 之后，还需验证记录的索引是否匹配 `snapPathIndex[fullPath]`。活跃但未被路径索引引用的记录是之前重复插入产生的孤立记录 — 跳过它们。

### 测试更新

- `test_paged_persistence.h` P32-2：将预期 liveRecordCount 从 3073 更新为 3072，因为 `loadRecords` 现在正确地将墓碑记录排除在活跃计数之外。

## 测试

新增测试文件 `tests/test_record_dedup.h`（Part 45），包含 6 个测试用例、21 个断言：

1. addRecord 去重：同路径插入两次，验证 liveCount 和后者胜出
2. loadRecords 去重：4 条记录对应 2 个唯一路径
3. compactRecords 去重：孤立记录被移除，recordCount == liveRecordCount
4. addRecord 与 replayWALEntries 行为一致性
5. 查询不返回重复路径（70 条记录中 50 个唯一）
6. 连续对同路径 addRecord 5 次

## 变更文件

| 文件 | 变更内容 |
|------|--------|
| `MacEverything/Core/SearchEngine.cpp` | 在 addRecord、loadRecords、compactRecords 中添加去重逻辑 |
| `tests/test_record_dedup.h` | 新增：Part 45 的 6 个测试用例 |
| `tests/test_paged_persistence.h` | 更新 P32-2 的预期 liveCount |
| `test_all.cpp` | 注册 Part 45 |

## 验证

- Part 45: 21/21 通过
- 完整 --fast 回归测试: 10,803/10,803 通过
- Release 构建: 成功
