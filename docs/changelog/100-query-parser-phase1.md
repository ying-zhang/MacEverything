# 100 - Phase 1: Query Parser with Boolean Operators

## 概述

为 MacEverything 添加 Everything 风格的高级搜索语法支持（Phase 1）。实现了布尔运算符（AND/OR/NOT）、引号短语、分组表达式的解析和执行。

## 规划

按照总体计划（6 Phase），Phase 1 聚焦于：
- 词法分析器（Tokenizer）：将查询字符串分解为 token 流
- 递归下降解析器（Parser）：将 token 流构建为 AST
- AST 求值引擎：在 SearchEngine 中评估 AST 并返回匹配结果
- 零开销后向兼容：简单查询走原有 query() 路径

## 新增文件

| 文件 | 行数 | 职责 |
|------|------|------|
| `MacEverything/Core/QueryAST.h` | 117 | AST 节点类型定义（TERM, AND, OR, NOT, FILTER） |
| `MacEverything/Core/QueryTokenizer.h` | 183 | 词法分析器，支持 WORD, QUOTED, PIPE, BANG, LANGLE, RANGLE, FILTER |
| `MacEverything/Core/QueryParser.h` | 38 | 递归下降解析器声明 |
| `MacEverything/Core/QueryParser.cpp` | 162 | 解析器实现：or_expr → and_expr → not_expr → atom |
| `MacEverything/Core/SearchEngineAdvancedQuery.cpp` | 260 | AST 求值引擎 + trigram 预过滤 |
| `tests/test_query_tokenizer.h` | 154 | Part 54: 词法分析测试（15 组，59 个断言） |
| `tests/test_query_parser.h` | 184 | Part 55: 解析器测试（18 组，70 个断言） |

## 修改文件

| 文件 | 修改内容 |
|------|---------|
| `MacEverything/Core/SearchEngine.h` | 添加 `queryAdvanced()` 声明和 `#include "QueryAST.h"` |
| `MacEverything/Core/SearchEngineQuery.cpp` | 在 `query()` 入口添加 `hasAdvancedSyntax()` 路由 |
| `MacEverything.xcodeproj/project.pbxproj` | 注册新文件到 Xcode 项目 |
| `test_all.cpp` | 引入 Part 54/55 测试模块 |

## 支持的语法

| 语法 | 示例 | 含义 |
|------|------|------|
| 空格（隐式 AND） | `foo bar` | 文件名同时包含 foo 和 bar |
| `\|`（OR） | `foo \| bar` | 文件名包含 foo 或 bar |
| `!`（NOT） | `foo !bar` | 包含 foo 但不包含 bar |
| `< >`（分组） | `<foo \| bar> baz` | (foo OR bar) AND baz |
| `"..."`（引号短语） | `"hello world"` | 精确匹配 "hello world" |
| 过滤器 | `ext:cpp` | Phase 1 识别但不评估（always-true） |

## 技术实现

### 查询处理流水线
```
用户输入 → hasAdvancedSyntax() 快速检测
  ├─ 无高级语法 → 走现有 query() 路径（零开销）
  └─ 有高级语法 → Tokenizer → Parser → AST → queryAdvanced()
```

### 关键设计决策
1. **零开销检测**：`hasAdvancedSyntax()` 通过一次线性扫描检测特殊字符（|, !, <, >, "）和已知过滤器前缀
2. **Trigram 预过滤**：从 AND 级别提取最长 TERM，用 trigram 索引缩小候选集
3. **FILTER 节点透传**：Phase 1 中 FILTER 节点始终返回 true，留待 Phase 2 实现
4. **过滤器参数中的 < >**：`size:>100kb` 中的 `>` 被正确解析为过滤器参数的一部分

## 测试

- Part 54（QueryTokenizer）：15 个测试组，59 个断言全部通过
- Part 55（QueryParser）：18 个测试组，70 个断言全部通过
- 全量回归测试：11197 个测试全部通过

## 验证

- xcodebuild Release 构建通过
- HTTP API 验证：高级语法查询（`foo | bar`、`foo !bar`、`"hello world"`）正确返回结果
