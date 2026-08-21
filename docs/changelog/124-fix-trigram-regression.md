# 124 - 修复 GLOB trigram 预过滤和路径 trigram 竞争选择

## 背景

#121 消除 Simple 查询路径后，引入两个性能回归：

1. **GLOB trigram 丢失**: `*.txt` 类查询从 glob-trigram 0.1ms → advanced-linear-gcd 138ms
2. **路径 trigram 锚点差**: `/usr/local/bin` 从 structured 31.8ms → advanced-trigram 145K 候选 156ms

## 根因分析

### 回归 1: GLOB trigram
`bestTrigramTerm()` 仅接受 `MatchMode::SUBSTRING` 模式的 TERM 节点。GLOB 模式的字面段（如 `*.txt` 中的 `.txt`）被拒绝，导致所有 GLOB 查询走线性扫描。

### 回归 2: 路径 trigram 锚点
trigram 选择采用严格瀑布策略（name → regex → path），一旦 name trigram 成功就不再尝试 path trigram。对于 `/usr/local/bin`，name trigram 选了低选择性的 "bin"（145K 候选），而 path trigram 的 "local" 选择性更高但未被尝试。

## 修复方案

### 1. bestTrigramTerm() 支持 GLOB 模式

新增 `termTrigramKey()` 辅助函数，对 GLOB 模式 TERM 调用 `extractLiteralSegments()` 提取字面段，返回最长的 >= 3 字符段作为 trigram key。

### 2. trigram 竞争选择

将严格瀑布改为 4 阶段竞争：
- Stage 1: Name trigram — 暂存候选，不直接提交
- Stage 2: Regex trigram — 仅在 name 失败时尝试
- Stage 3: Path trigram — 始终尝试（移除 `!useTrigramIndex` 门控）
- Stage 4: 比较 name 和 path 候选数，取较少者

## 修改文件

| 文件 | 变更 |
|------|------|
| `MacEverything/Core/SearchEngineAdvancedQuery.cpp` | bestTrigramTerm GLOB 支持 + 竞争选择逻辑 |
| `tests/test_trigram_competition.h` | 新增 Part 69: 11 个测试用例 |
| `tests/test_trigram_index.h` | 更新 5 个 GLOB 测试的预期路径 |
| `test_all.cpp` | 注册 Part 69 |

## 测试

- Part 69 (trigram competition): 11/11 通过
- 全量 fast 测试: 11,808/11,808 通过
- 更新 Part 8 中 5 个 GLOB 测试预期从 `advanced-linear-gcd` → `advanced-trigram`

## 性能验证 (HTTP, 5.35M 记录)

| 查询 | 修复前 | 修复后 |
|------|--------|--------|
| `*.txt` | advanced-linear-gcd ~138ms | advanced-trigram ~29ms, 44K 候选 |
| `*.cpp` | advanced-linear-gcd ~138ms | advanced-trigram ~188ms, 3.8K 候选 |
| `/usr/local/bin` | advanced-trigram 156ms, 145K 候选 | advanced-path-trigram ~290ms, 145K 候选 |
