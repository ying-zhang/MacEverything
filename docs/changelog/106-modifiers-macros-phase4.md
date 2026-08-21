# 106 — Phase 4: 修饰符和宏 (Modifiers & Macros)

## 概述

实现 Everything 搜索语法的 Phase 4：修饰符（Modifiers）和宏（Macros），使高级查询引擎支持多种匹配模式和常用文件类型快捷搜索。

## 修饰符 (Modifiers)

修饰符将 FILTER 节点在解析阶段转换为 TERM 节点，携带特定的 MatchMode：

| 修饰符 | 行为 | 示例 |
|--------|------|------|
| `case:X` | 区分大小写的 substring 匹配 | `case:Hello` 匹配 "Hello_World.cpp" 但不匹配 "hello.txt" |
| `nocase:X` | 显式不区分大小写 | `nocase:TEST` 匹配 "test.cpp" |
| `regex:X` | 正则表达式匹配 | `regex:^test.*\.json$` |
| `ww:X` / `wholeword:X` | 整词匹配 | `ww:test` 匹配 "test_data.json" 但不匹配 "contest.txt" |
| `wfn:X` / `wholefilename:X` | 完整文件名匹配 | `wfn:README.md` 只匹配文件名完全为 "README.md" 的文件 |

## 宏 (Macros)

宏在解析阶段展开为 `ext:` 过滤器，提供常用文件类型的快捷搜索：

| 宏 | 扩展为的扩展名列表 |
|----|-------------------|
| `audio:` | mp3, wav, flac, aac, ogg, m4a, wma, alac |
| `video:` | mp4, avi, mkv, mov, wmv, flv, webm, m4v |
| `pic:` | jpg, jpeg, png, gif, bmp, tiff, tif, webp, svg, ico, heic, heif, raw |
| `doc:` | pdf, doc, docx, xls, xlsx, ppt, pptx, txt, rtf, odt, ods, odp, csv, md |
| `exe:` | app, dmg, pkg, sh, command, csh, action |
| `zip:` | zip, rar, 7z, tar, gz, bz2, xz, tgz, zst, lz4 |

## 实现细节

### QueryFilterParser.h
- 添加修饰符分支：将 `case:`, `nocase:`, `regex:`, `ww:`, `wfn:` 等 FILTER 节点转换为 TERM 节点
- 添加宏展开：`expandMacro()` 辅助函数将宏转换为 `ext:` FILTER 节点并调用 `parseExt()`

### SearchEngineAdvancedQuery.cpp — evalTerm() 重写
- **SUBSTRING**: 默认模式。caseSensitive=true 时使用 `rec.name` 原始大小写匹配；false 时使用 `namePool_` 小写 + `me::simdContains`
- **GLOB**: 复制 `globMatchImpl()` 到匿名命名空间，对小写文件名做 glob 模式匹配
- **REGEX**: 在 `queryAdvanced()` 入口预编译所有正则到 `RegexCache` (map)，evalTerm 中查找使用，避免每条记录重复编译
- **WHOLEWORD**: substring 匹配后检查前后字符是否为词边界（非 alphanumeric 字符）
- **WHOLEFILENAME**: 比较文件名长度 + 内容完全相等

### 性能考虑
- 正则表达式在查询开始时预编译一次，使用 `std::regex::optimize` 标志
- `bestTrigramTerm()` 跳过非 SUBSTRING 模式，避免错误的 trigram 预过滤
- 宏在 AST 解析阶段展开，搜索时直接作为 ext: 过滤器处理

## 修改文件

| 文件 | 变更 |
|------|------|
| `MacEverything/Core/QueryFilterParser.h` | 添加修饰符→TERM 转换、宏→ext: 展开 |
| `MacEverything/Core/SearchEngineAdvancedQuery.cpp` | evalTerm() 支持 5 种 MatchMode + caseSensitive + regex 预编译 |
| `tests/test_query_modifiers.h` | 新建 Part 59 测试（80 个测试用例） |
| `test_all.cpp` | 集成 Part 59 |

## 测试结果

- Part 59: 80 passed, 0 failed
- 全量快速测试: 11464 passed, 0 failed
- 构建: BUILD SUCCEEDED

## Bug 修复

- 词边界定义修正：`_` (下划线) 视为词边界字符（与 Everything 行为一致），使 `ww:test` 能匹配 `test_data.json`
