# v142: RE2 Regex Benchmarks in string_search_bench

## 概述

在现有的大内存字符串扫描基准测试程序 `string_search_bench.cpp` 中增加了 RE2 正则表达式性能测试，便于对比 RE2 与各种字符串搜索算法（std::string::find、memmem、KMP、Boyer-Moore-Horspool、Rabin-Karp、NEON SIMD）的吞吐量。

## 变更内容

### 新增功能

1. **RE2 基准测试**：新增 `run_re2_bench()` 函数，使用 `RE2::Match()` API 进行全文匹配计数
2. **8 种 RE2 测试模式**：
   - 字面量匹配（literal）
   - 大小写不敏感（case-insensitive）
   - 2 路 / 4 路交替匹配（alternation）
   - 字符类前缀（char-class prefix）
   - `.*` 前缀（dot-star prefix）
   - 有界重复（bounded repeat）
   - 模拟单词边界（word boundary simulation）
3. **CLI 参数增强**：
   - `--size <N>` — 可配置数据大小（支持 K/M/G 后缀），默认 1GB
   - `--re2-only` — 仅运行 RE2 测试
   - `--no-re2` — 跳过 RE2 测试
   - `--help` — 显示用法

### 改进

- 移除硬编码的 5GB 数据大小，改为运行时可配置（默认 1GB）
- RE2 结果与字符串搜索结果合并到统一的 Summary 表格中

## 测试结果（256MB，查询 "hello"）

| 算法 | 吞吐量 (GB/s) |
|------|---------------|
| NEON 2x MT | 53.30 |
| RE2 case-insensitive | 3.20 |
| RE2 literal | 1.55 |
| std::string::find | 1.44 |
| RE2 dot-star prefix | 0.21 |

## 编译方式

```bash
clang++ -std=c++20 -O2 -Wall -Wextra \
  -I/opt/homebrew/opt/re2/include \
  -L/opt/homebrew/opt/re2/lib -lre2 \
  string_search_bench.cpp -o string_search_bench
```

## 技术细节

- 使用 `RE2::Match()` + `RE2::UNANCHORED` 逐步推进匹配位置，确保计数所有出现
- 对零长度匹配进行安全推进（advance=1），防止无限循环
- RE2 模式中的特殊字符通过 `RE2::QuoteMeta()` 转义
