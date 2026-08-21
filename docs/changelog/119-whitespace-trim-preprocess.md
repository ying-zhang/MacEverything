# 119 — 在 `preprocessQuery()` 中添加空白字符修剪

## 概述

在 `preprocessQuery()` 的第一步添加了首尾空白字符去除功能，确保 `"  hello  "` 这样的查询与 `"hello"` 行为完全一致，不受输入来源影响。

## 动机

用户输入中可能包含意外的空白字符——来自搜索栏、HTTP API 或剪贴板粘贴。如果不进行修剪，这些空白字符可能影响波浪号展开（前导空格会阻止 `~` 检测）、glob 模式匹配以及基于斜杠的路径检测。将修剪逻辑集中在 `preprocessQuery()` 中可以确保所有查询路径的行为一致。

## 变更内容

### `MacEverything/Core/SearchEngineQuery.cpp`

- **新增** `preprocessQuery()` 的第 0 步：使用 `find_first_not_of` / `find_last_not_of` 去除首尾空白字符（`" \t\r\n"`）。全空白输入返回空字符串。
- **新增** `query()` 中的提前返回判断：`preprocessQuery()` 返回后检查是否为空，如果为空则立即返回 `{}`。用于处理原始输入非空但全为空白字符的情况。

### `tests/test_whitespace_trim.h`（Part 66）

新增测试文件，包含 7 个测试用例：
1. `"  hello  "` 与 `"hello"` 匹配数量相同
2. `"\t hello \n"` 与 `"hello"` 匹配数量相同
3. `"   "`（全空白）返回 0 个结果
4. `" "`（单个空格）返回 0 个结果
5. 内部空格保留：`"  hello world  "` 与 `"hello world"` 结果相同
6. 修剪 + 波浪号组合：`"  ~/Downloads/*.txt  "` 与 `"~/Downloads/*.txt"` 结果相同
7. 修剪 + 波浪号结果正确性：匹配 `f1.txt`

### `test_all.cpp`

- 添加 `#include "tests/test_whitespace_trim.h"`
- 在调度表和 `--fast` 套件中注册 Part 66
- 更新帮助文本

## 验证

- **单元测试**：Parts 65 + 66（14 个测试）— 全部通过。
- **快速套件**：11,685 个测试 — 全部通过，0 个失败。
- **构建**：`xcodebuild` Release 构建成功。
- **HTTP**：通过 `curl` 使用包含空白字符的查询进行验证。

## 风险

极低 — 纯输入规范化。所有现有测试继续通过。唯一的行为变更是全空白查询现在会立即返回空结果，而不是流经完整的查询管道。
