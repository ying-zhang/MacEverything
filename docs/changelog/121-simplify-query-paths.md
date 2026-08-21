# 121 — 简化查询执行路径：消除 Simple 查询路径

## 概要

移除了约 700 行重复的 Simple 查询路径代码，将所有查询统一路由到 `queryAdvanced()` AST 驱动管线。Simple 路径中的 `querySlashSplit()`、`queryPathTrigram()`、`queryLinearScanPath()`、`queryLinearScan()` 及相关匿名命名空间代码全部删除。

## 动机

三条查询路径（Simple、Advanced、Structured）存在大量重复逻辑：`globMatchImpl`、trigram 预过滤、GCD 并行扫描、dedup bitmap 等。随着 Advanced 路径已完全覆盖 Simple 路径的功能，保留两套实现增加了维护负担且容易出现行为不一致。

## 实施

### 1. `QueryNode` 添加 `textLower` 预计算（P1）
- **文件**: `MacEverything/Core/QueryAST.h`
- 在 `QueryNode` 中添加 `std::string textLower` 字段，`makeTerm()` 工厂方法中预计算
- 消除 `evalTerm()` 中每条记录调用 `me::toLower()` 的堆分配开销

### 2. 提取 `CompiledGlob` 到共享头文件（P4）
- **新文件**: `MacEverything/Core/CompiledGlob.h`
- 从 Simple 路径匿名命名空间中提取 `CompiledGlob`、`compileGlob()`、`compiledGlobMatch()`、`globMatchImpl()`
- 消除两处 `globMatchImpl` 重复实现

### 3. 添加 `transformGlobTerms()` AST 后处理（P4）
- **新文件**: `MacEverything/Core/ASTGlobTransform.h`
- 递归遍历 AST，将 SUBSTRING 模式中含 `*`/`?` 的 TERM 节点转为 GLOB 模式
- 在 `queryAdvanced()` 入口处集成，位于 `transformSlashTerms()` 之后

### 4. `evalTerm()` 优化与 glob 集成
- **文件**: `MacEverything/Core/SearchEngineAdvancedQuery.cpp`
- 4 处 `me::toLower(node.text)` 替换为 `node.textLower`
- GLOB 模式使用 `CompiledGlob` 编译后匹配

### 5. `query()` 入口简化
- **文件**: `MacEverything/Core/SearchEngineQuery.cpp`
- 移除 `hasAdvancedSyntax()` 门控
- DIR_LIST 模式在入口处预分发到 `queryDirList()`
- 所有非 DIR_LIST 查询直接路由 `queryAdvanced()`
- 集成 `preprocessQuery()` 统一处理空白裁剪和 `~` 扩展
- 删除 Simple 路径代码约 700 行

### 6. `SearchEngine.h` 声明清理
- 删除已移除的 Simple 路径私有方法声明
- 保留 `queryDirList()` 声明

## 语义变化

- **空格查询**: `"foo bar"` 从 7 字符子串匹配变为 `AND(TERM("foo"), TERM("bar"))`，与 Everything Windows 版一致
- **searchPath 标签**: Simple 路径标签（`"linear"`、`"glob-trigram"`、`"trigram"`）替换为 Advanced 路径标签（`"advanced-trigram"`、`"advanced-linear-gcd"` 等）

## 测试

- 新增 Part 67 (`tests/test_query_simplification.h`)：18 个测试用例覆盖 `textLower` 预计算、`CompiledGlob` 模式、`transformGlobTerms()` AST 变换、端到端查询（关键字/glob/AND/filter/OR/DIR_LIST）
- 更新 Part 8 (`tests/test_trigram_index.h`)：8 处 searchPath 断言适配新标签
- 更新 Part 3e/48/58/65 等多个测试文件适配行为变化
- 全量测试 11718 项通过

## 性能

- trigram 加速保持 12-146x
- `textLower` 预计算消除了每记录堆分配，简单关键字查询性能持平或略优

## 影响文件

| 文件 | 操作 | 净变化 |
|------|------|--------|
| `QueryAST.h` | 修改 | +3 行 |
| `CompiledGlob.h` | 新建 | +120 行 |
| `ASTGlobTransform.h` | 新建 | +30 行 |
| `SearchEngineAdvancedQuery.cpp` | 修改 | +20 行 |
| `SearchEngineQuery.cpp` | 大改 | -700 行 |
| `SearchEngine.h` | 修改 | -15 行 |
| `tests/test_query_simplification.h` | 新建 | +234 行 |
| 其他测试文件 (6个) | 修改 | ~+30 行 |
| **合计** | | **约 -319 行** |

## HTTP 验证

所有 6 类查询通过 HTTP API 验证：
- 基本关键字: `/api/search?q=test`
- Glob 模式: `/api/search?q=*.cpp`
- 路径查询: `/api/search?q=/usr/local`
- DIR_LIST: `/api/search?q=/usr/*`
- 空格 AND: `/api/search?q=hello+world`
- 过滤器: `/api/search?q=ext:h`
