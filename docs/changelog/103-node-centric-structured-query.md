# 103 - 以节点为中心的结构化查询系统

## 概述

实现了以节点为中心的查询系统，从根本上改变了含斜杠查询的工作方式。原先 `/abc/def` 被视为路径子串搜索，会返回大量噪声结果（包括子节点和无关路径）；现在系统将斜杠查询解释为结构化节点搜索：最后一个分量按名称标识目标节点，前面的分量作为路径约束。

## 查询模式

| 输入 | 模式 | 语义 |
|---|---|---|
| `abc` | PLAIN | 行为不变（名称/路径子串匹配） |
| `/abc/def` | SEGMENTS | 名称包含 "def"，父路径包含 "abc" |
| `abc/def` | SEGMENTS | 同上（前导斜杠可选） |
| `/abc/*/def` | SEGMENTS | 名称包含 "def"，祖先路径包含 "abc"（非相邻） |
| `/abc/def/` | DIR_EXACT | 精确匹配名为 "def" 且父路径包含 "abc" 的目录 |
| `/abc/def/*` | DIR_LIST | 列出 "abc" 下名为 "def" 的目录的直接子项 |

## 实现细节

### 新增文件
- **`MacEverything/Core/StructuredQueryParser.h`**：纯内联解析器，生成 `ParsedQuery` 结构体，包含 `QueryMode`、`namePattern` 和 `pathSegments`（带邻接跟踪）
- **`tests/test_structured_query.h`**：19 个测试用例（Part 58），覆盖解析器单元测试和 SearchEngine 集成测试

### 修改文件
- **`SearchEngineQuery.cpp`**：新增 `queryStructured()`、`queryDirList()` 和 `pathSegmentsMatch()` 实现，以及在 glob/trigram 路径之前路由结构化查询的调度逻辑
- **`SearchEngine.h`**：三个新方法的声明
- **`test_slash_query.h`**：重写 Part 48 测试以适配节点中心预期
- **`test_path_search.h`**：更新 Part 3b/3b-2 斜杠查询预期
- **`test_path_trigram.h`**：更新 Part 47 斜杠查询预期
- **`test_memory_optimizations.h`**：更新 Part 21 `/home/user` 预期
- **`test_all.cpp`**：注册 Part 58

### 关键设计决策
1. **trigram 加速名称搜索**：`queryStructured()` 使用 `nameTrigramIndex_` 查找名称候选项，然后通过 `simdContains()` 和 `pathSegmentsMatch()` 验证
2. **邻接感知的路径匹配**：`pathSegmentsMatch()` 从右到左遍历路径分量，遵循邻接约束（`*` 打破邻接关系）
3. **DIR_LIST 通过 pathLookup_ 实现**：`queryDirList()` 通过精确名称匹配查找目录记录，构建完整路径，在 `pathLookup_` 中查找 pathPool 索引，然后使用 `pathIdxToRecords_` 实现 O(1) 子项查找
4. **绕过 glob 处理**：结构化查询跳过 `isGlobPattern()` 检查，防止 `/abc/*` 被当作 glob 模式处理

### 实现过程中的缺陷修复
- 修复 C99 指定初始化语法（`{idx: ...}`）为合法的 C++20 聚合初始化
- 修复 `records_[dirIdx].name` → `namePool_.data(dirIdx)`（InternalRecord 没有 `name` 字段）
- 修复结构化查询中 `phase1Ms` 为负数的问题，显式设为 0.0

## 测试

- 11,398 个快速测试通过（包括 19 个新增 Part 58 测试）
- HTTP API 验证：SEGMENTS（`searchPath: structured`）、DIR_LIST（`searchPath: dir-list`）和 PLAIN（`searchPath: trigram`）均返回正确结果

## 性能

- SEGMENTS/DIR_EXACT：trigram 名称过滤 -> simdContains 验证 -> 路径段检查（比全路径扫描快得多）
- DIR_LIST：目录名查找 + 通过 `pathIdxToRecords_` 实现 O(1) 子项查找（原先为全线性扫描）
- PLAIN：完全不受影响
