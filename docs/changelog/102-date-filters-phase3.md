# 102 — 第三阶段：日期过滤器（dm:、dc:、da:）

## 概述

实现 Everything 搜索语法计划的第三阶段：基于日期的过滤功能，使用 `dm:`（修改日期）、`dc:`（创建日期）、`da:`（访问日期）过滤前缀。同时支持长别名 `datemodified:`、`datecreated:`、`dateaccessed:`。

## 新增文件

- **`MacEverything/Core/QueryDateParser.h`**（约 305 行，纯头文件）
  - 将日期过滤参数解析为 `CompareOp` + epoch 时间戳，存储在 `QueryNode` 上
  - 支持的日期表达式：
    - 关键词：`today`、`yesterday`、`thisweek`、`lastweek`、`thismonth`、`lastmonth`、`thisyear`、`lastyear`
    - 相对日期：`last7days`、`last30days`、`last3months`、`last6months`、`last1year`
    - ISO 日期：`2024`、`2024-06`、`2024-06-15`
    - 比较运算：`>2024-01-01`、`>=today`、`<2024-06`、`<=yesterday`
    - 范围：`2024-01..2024-06`

- **`tests/test_query_date_filters.h`** — Part 57，71 个测试
  - Section A（A1-A19）：QueryDateParser 单元测试，覆盖所有日期格式
  - Section B（B1-B15）：集成 SearchEngine 测试，使用回溯日期的文件

## 修改文件

- **`MacEverything/Core/QueryFilterParser.h`** — 为 dm/dc/da 过滤器添加到 `QueryDateParser::parse()` 的路由
- **`MacEverything/Core/SearchEngineAdvancedQuery.cpp`** — 在 `evalFilter()` 中添加 dm/dc/da 分支，使用 `compareNumeric()` 对比 `rec.modTime`
- **`test_all.cpp`** — 添加 Part 57 的 include、调度和 `QueryFilterParser.h` 的 include
- **`MacEverything.xcodeproj/project.pbxproj`** — 将 `QueryDateParser.h` 添加到 Xcode 项目

## 设计决策

1. **纯头文件 QueryDateParser**：所有日期解析逻辑放在单个头文件中以保持简洁；除 `<ctime>` 外无外部依赖，不需要 .cpp 文件。
2. **基于 epoch 的比较**：所有日期转换为 `time_t`（Unix epoch）并存储为 `QueryNode` 上的 `numVal1`/`numVal2`。`evalFilter()` 中现有的 `compareNumeric()` 处理所有比较运算符。
3. **dc: 回退到 modTime**：macOS 支持 `birthtime`（创建时间），但 `FileRecord` 目前仅存储 `modTime`。在扫描流程添加 `birthtime` 之前，`dc:` 暂时回退到 `modTime`。
4. **周起始日为周日**：`thisweek`/`lastweek` 使用周日作为一周起始，与 `tm_wday` 约定一致。

## 测试结果

- Part 57: **71 通过，0 失败**
- 完整 `--fast` 套件: **11,348 通过，0 失败**
