# 101 — Phase 2: Core Query Filters

## 概述

实现 Everything 搜索语法的 Phase 2：核心过滤器支持。在 Phase 1 布尔运算和查询解析器的基础上，添加了 10 种结构化过滤器，可与文本搜索和布尔运算自由组合。

## 支持的过滤器

| 过滤器 | 示例 | 说明 |
|--------|------|------|
| `ext:` | `ext:cpp;h;hpp` | 扩展名匹配，大小写不敏感，分号分隔多值 |
| `size:` | `size:>1mb`, `size:100kb..1mb` | 文件大小比较，支持 kb/mb/gb/tb 单位和范围 |
| `file:` | `file:` | 仅匹配文件 |
| `folder:` | `folder:` | 仅匹配目录 |
| `type:` | `type:file`, `type:folder` | file:/folder: 的简写形式 |
| `path:` | `path:subdir` | 完整路径包含指定子字符串 |
| `nopath:` | `nopath:subdir` | 完整路径不包含指定子字符串 |
| `parent:` | `parent:/Users/foo/Desktop` | 直接父目录精确匹配 |
| `depth:` | `depth:<3` | 目录深度过滤 |
| `len:` | `len:>20` | 文件名长度过滤 |

## 组合使用示例

- `ext:cpp size:>1kb` — 大于 1KB 的 C++ 文件
- `hello ext:cpp` — 文件名含 "hello" 的 .cpp 文件
- `!ext:cpp` — 所有非 .cpp 文件
- `ext:cpp | ext:h` — .cpp 或 .h 文件
- `path:subdir ext:cpp` — subdir 目录下的 .cpp 文件

## 架构设计

### 两阶段处理

1. **解析时（QueryFilterParser）**：在 AST 构建时，将原始 `filterArg` 字符串解析为结构化字段（`op`, `numVal1`, `numVal2`, `extList`）
2. **求值时（evalFilter）**：在查询执行时，用解析好的结构化字段对 FileRecord 进行快速比较

### 新增文件

| 文件 | 说明 |
|------|------|
| `MacEverything/Core/QueryFilterParser.h` | Header-only 过滤器参数解析器 |
| `tests/test_query_filters.h` | Part 56: 76 个测试断言 |

### 修改文件

| 文件 | 变更 |
|------|------|
| `MacEverything/Core/QueryParser.cpp` | 创建 FILTER 节点后调用 QueryFilterParser::parse() |
| `MacEverything/Core/SearchEngineAdvancedQuery.cpp` | 实现 evalFilter() 替换 Phase 1 的 `return true` 桩 |
| `test_all.cpp` | 注册 Part 56 测试 |
| `MacEverything.xcodeproj/project.pbxproj` | 添加 QueryFilterParser.h |

## 测试覆盖

Part 56 共 23 个测试组，76 个断言：
- 单扩展名 / 多扩展名 / 大小写不敏感
- size 比较 (GT, LT, 范围)
- file: / folder: 类型过滤
- len: 文件名长度
- depth: 目录深度
- 组合过滤 (ext + size, text + ext)
- path: / nopath: / parent: 路径匹配
- AST 级别解析验证
- NOT + filter, OR + filter
- type: 简写

## 验证

- 编译通过 (xcodebuild Release)
- 全部 76 个 Part 56 测试通过
- Part 54 (tokenizer) 和 Part 55 (parser) 无回归
- --fast 全部测试通过
