# 075 — 绝对路径查询 Trigram 加速 + 日志标签修复

## 概述

两项修复：
1. **Fix #1（日志标签错误）**：`SearchEngine.cpp` 日志用 `hasSlash` 判断标签，但绝对路径查询 `hasSlash=true` 却没走 slash-split 路径，日志误标为 `trigram-split`
2. **Fix #4（绝对路径查询慢）**：`/usr/local/bin` 等绝对路径查询被 `isAbsPath` 排除在 slash-split 之外，回退到 O(N) 线性扫描 5M 记录，耗时 844ms

## 根因分析

### Fix #1：日志标签错误

`useSlashSplit` 变量声明在 Phase 2 局部作用域内，日志输出代码在函数末尾无法访问该变量，只能用 `hasSlash` 近似替代。但 `hasSlash` 对绝对路径查询也为 `true`，而这些查询并未走 slash-split 路径，导致日志标签不准确。

### Fix #4：绝对路径查询慢

`pathTrigramIndex_` 索引完整目录路径（含 `/` 字符），因此 `/usr/local` 的 trigrams（`/us`, `usr`, `sr/`, `/lo` 等）在索引中都存在。`isAbsPath` 排除是不必要的 — 绝对路径完全可以复用现有 slash-split 策略。

## 实现

### SearchEngine.cpp 修改

1. **提升 `useSlashSplit` 作用域**：从 Phase 2 局部变量提升到函数级变量，使日志代码可访问
2. **移除 `isAbsPath` 排除**：简化 slash-split 条件为 `!pathTrigramIndex_.empty() && hasSlash`
3. **修复日志标签**：`hasSlash` → `useSlashSplit`
4. **处理空 pathPart 边界**：`/etc` 拆分后 `pathPart=""`、`namePart="etc"`。文件名是 `hosts`/`passwd` 而非 `etc`，所以当 pathPart 为空且 lowerKey >= 3 时，使用完整 lowerKey 作为 pathPart 进行 pathTrigramIndex_ 查找

### 新增测试

在 `tests/test_slash_query.h` 新增 4 个绝对路径测试（Tests 7-10）：

| # | 测试 | 场景 |
|---|------|------|
| 7 | AbsPath slash query | `/usr/local` 匹配 `/usr/local/` 下 2 个文件 |
| 8 | AbsPath root-level | `/etc` 匹配 `/etc/` 下的 hosts 和 passwd |
| 9 | AbsPath very short | `/a` 回退线性扫描（两部分均 < 3 字符） |
| 10 | AbsPath deep query | `/usr/local/bin` 匹配 2 个文件，不匹配 `/usr/bin/gcc` |

## 性能预期

| 查询 | 优化前 | 优化后 |
|------|--------|--------|
| `/usr/local/bin` | ~844ms (线性扫描) | <50ms (trigram-split) |
| `tests/test_query_perf` | ~28ms | ~28ms (不变) |
| `homebrew` | ~29ms | ~29ms (不变) |

## 测试结果

全部 10,838 项 `--fast` 测试通过，0 失败。

## 变更文件

| 类型 | 文件 |
|------|------|
| 修改 | `MacEverything/Core/SearchEngine.cpp` |
| 修改 | `tests/test_slash_query.h` |
