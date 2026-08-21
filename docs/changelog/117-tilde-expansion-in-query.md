# 117 — 查询管道支持 ~ (Home目录) 展开

## 问题

搜索 `~/*/*.txt` 时返回 0 结果，但 `/Users/username/Downloads/f1.txt` 等文件应当匹配。

## 根因

查询管道中没有对 `~` 字符做 Home 目录展开。`~` 被当作字面字符参与 glob 匹配，
而索引中所有路径均为绝对路径（如 `/Users/username/downloads/f1.txt`），
因此模式 `~/*/*.txt` 永远无法匹配任何记录。

## 修复方案

在 `SearchEngine::query()` 入口处（`SearchEngineQuery.cpp`），于 `keyword.empty()` 
检查之后、`hasAdvancedSyntax` 判断之前，对以 `~` 或 `~/` 开头的关键词进行展开：

- `~` → `$HOME`（如 `/Users/username`）
- `~/xxx` → `$HOME/xxx`（如 `/Users/username/xxx`）
- 非开头的 `~` 不受影响（如 `foo~bar` 不展开）

展开后的字符串同时流入普通查询路径和高级查询路径（`queryAdvanced`），
确保所有查询模式（glob、结构化、布尔表达式等）都能正确处理 `~`。

## 变更文件

| 文件 | 变更 |
|------|------|
| `MacEverything/Core/SearchEngineQuery.cpp` | 在 `query()` 入口添加 tilde 展开逻辑 |
| `tests/test_tilde_expansion.h` | 新增 Part 65: 7 个 tilde 展开测试用例 |
| `test_all.cpp` | 注册 Part 65 测试并加入 --fast 集合 |

## 测试

- `~/*/*.txt` → 正确匹配 Home 下一级子目录中的 .txt 文件
- `~/Downloads/*.txt` → 精确匹配 Downloads 目录下的 .txt 文件
- `~/Pictures/*/*.jpg` → 匹配多层嵌套路径
- `~/*.jpg` → 验证 glob `*` 跨 `/` 边界行为
- `~` 单独使用 → 不崩溃
- `foo~bar` → 中间的 `~` 不展开
- HTTP 端点 `/api/search?q=~/*/*.txt` 验证通过
