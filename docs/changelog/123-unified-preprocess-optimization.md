# 122 - 统一查询预处理优化

## 概述

将所有 `me::toLower()` 调用合并为 `preprocessQuery()` 中的单次规范计算，消除查询管道中的冗余小写转换，修复分词器中的未定义行为 Bug，并移除死代码。

## 动机

在提取 `preprocessQuery()`（#118）之后，下游管道仍存在多处低效问题：

1. **三重小写转换**：`transformSlashTerms()` → `parseQuery()` → `makeTerm()` 各自对相同文本调用 `me::toLower()`
2. **分散的手写循环**：`QueryTokenizer`、`QueryFilterParser` 和 `QueryDateParser` 使用手写的 `std::tolower` 字符循环，而非 SIMD 加速的 `me::toLower()`
3. **未定义行为 Bug**：`QueryTokenizer.h` 中 `std::tolower(ch)` 未使用 `static_cast<unsigned char>`，对非 ASCII 输入属于未定义行为
4. **无用的小写转换**：`case:` 和 `regex:` 修饰符计算了 `textLower`，但其匹配路径并不使用该值
5. **死代码**：`isGlobPattern()` 自引入基于 AST 的 glob 处理后不再使用

## 变更内容

### PreprocessedQuery 结构体（`SearchEngineQuery.cpp`）
- `preprocessQuery()` 现在返回 `PreprocessedQuery{original, lower}` — 在入口点执行单次 `me::toLower()`
- `query()` 将两个字段传递给下游，消除重复计算

### parseQuery() 重载（`StructuredQueryParser.h`）
- 新增双参数重载 `parseQuery(rawQuery, lowered)` 接受预计算的小写文本
- 保留原有的单参数重载以确保向后兼容

### makeTerm() 重载（`QueryAST.h`）
- 新增三参数重载 `makeTerm(text, precomputedLower, mode)` 跳过 `me::toLower()`
- 用于 `transformSlashTerms()` 中名称模式已经是小写的场景

### transformSlashTerms()（`ASTStructuredTransform.h`）
- 将 `node->textLower` 传递给 `parseQuery()` 而非重新对 `node->text` 做小写转换
- 对名称词项使用新的 `makeTerm` 重载

### 未定义行为修复（`QueryTokenizer.h`）
- 将手写的 `std::tolower` 字符循环替换为 `me::toLower(name)` 进行过滤器名称的小写转换
- 修复查询包含非 ASCII 字符时的未定义行为

### 统一小写转换（`QueryFilterParser.h`、`QueryDateParser.h`）
- `path`/`nopath`/`parent` 过滤器：使用 `me::toLower(arg)`（已验证原逻辑正确）
- `parseExt()`：先整体 `me::toLower(arg)` 再按 `;` 分割
- `parseValueWithUnit()`：对单位后缀使用 `me::toLower(s.substr(numEnd))`
- `parseDateExpr()`：对关键词匹配使用 `me::toLower(expr)`
- `case:` 修饰符：跳过 `textLower` 计算（大小写敏感匹配使用 `text`）
- `regex:` 修饰符：跳过 `textLower` 计算（正则使用 `text` 配合 `icase` 标志）

### 死代码移除（`SearchEngineQuery.cpp`、`SearchEngine.h`）
- 移除 `isGlobPattern()` 的定义和声明 — 自 `ASTGlobTransform.h` 引入内联检查后不再使用

### 测试（`tests/test_preprocess_unified.h`，Part 68）
- 16 个测试用例、50 个 CHECK 断言，覆盖所有变更：
  - parseQuery 双参数和单参数重载
  - makeTerm 有无预计算小写的两种情况
  - transformSlashTerms 管道验证
  - case:/regex:/nocase: 修饰符行为
  - ext/path/size 过滤器的小写转换
  - 分词器非 ASCII 未定义行为修复
  - SearchEngine 集成测试（崩溃测试）
  - 全空白和波浪号展开
  - isGlobPattern 移除（编译时验证）

## 修改的文件

| 文件 | 变更 |
|------|------|
| `MacEverything/Core/SearchEngineQuery.cpp` | `PreprocessedQuery` 结构体，更新 `preprocessQuery()` 和 `query()` |
| `MacEverything/Core/SearchEngine.h` | 更新 `queryAdvanced` 声明，移除 `isGlobPattern` 声明 |
| `MacEverything/Core/SearchEngineAdvancedQuery.cpp` | 更新 `queryAdvanced` 签名 |
| `MacEverything/Core/StructuredQueryParser.h` | 双参数 `parseQuery` 重载 |
| `MacEverything/Core/QueryAST.h` | 三参数 `makeTerm` 重载 |
| `MacEverything/Core/ASTStructuredTransform.h` | 将 `textLower` 传递给 `parseQuery`，使用新 `makeTerm` |
| `MacEverything/Core/QueryTokenizer.h` | 修复未定义行为 → `me::toLower()` |
| `MacEverything/Core/QueryFilterParser.h` | 统一循环 → `me::toLower()`，跳过无用的小写转换 |
| `MacEverything/Core/QueryDateParser.h` | 统一循环 → `me::toLower()` |
| `tests/test_preprocess_unified.h` | Part 68 测试（16 用例，50 断言） |
| `test_all.cpp` | 注册 Part 68 |

## 验证

- `./test_all --part 68`：50/50 通过
- `./test_all --fast`：11768 通过，0 失败
- Release 构建 + DMG 打包
- HTTP 功能验证
