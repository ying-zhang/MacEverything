# 129: Fix Test Quality — Tautological & Weak Assertions

## 背景

对全部 77 个测试文件（11808 个测试）的全面审查发现约 30 个测试质量问题：
- 永远不会失败的 `check(true, ...)` 断言
- 绕过 `check()` 框架的手动 `passed++` / `cout << "PASS"`
- 使用 `>=` 的弱断言（实际上已知确切数量）
- 逻辑恒真式（如 `A || !A`、`|| true`）
- 早期 `return` 屏蔽后续测试

所有 11808 个测试在修复前均通过——问题在于许多"通过"的测试实际上并未验证任何东西。

## 修改范围

共修改 22 个测试文件，分四个优先级：

### P0: 替换 `check(true, ...)` 为真实断言（9 个文件，20 处）

- `test_trigram_index.h` — 5 处基准测试 → 验证计时数据/删除计数
- `test_query_perf.h` — 7 处基准测试 → 验证结果集大小
- `test_wal_race.h` — 1 处 → 验证并发追加计数
- `test_wal_race_indexpersistence.h` — 1 处 → 验证记录数保留
- `test_destructor_safety.h` — 2 处 → 验证智能指针释放 + 修复作用域
- `test_event_driven_compaction.h` — 1 处 → 验证索引文件计数
- `test_recent_indices.h` — 1 处 → 验证基准延迟上界
- `test_parallel_snippets.h` — 1 处 → 验证计时数据
- `test_content_query_bench.h` — 1 处 → 验证查询匹配数

### P0: 修复框架绕过（5 个文件）

- `test_http_engine_swap.h` — 移除手动 `cout PASS/FAIL` → 全部使用 `check()`
- `test_service_engine.h` — 替换 `passed++` → `check()`
- `test_preprocess_unified.h` — 替换 3 处 `passed++` → `check()`
- `test_daemon_startup.h` — 移除未使用的 `allOk`；`check(true,...)` → `check(WIFEXITED(status),...)`
- `test_query_needs_analysis.h` — 移除 16 处无条件 `cout << "PASS"` 行

### P1: 修复逻辑恒真式和弱断言（7 个文件）

- `test_query_filters.h` — 移除 `|| true`；修正 depth 过滤器测试
- `test_query_simplification.h` — `A || !A` → `!empty()`
- `test_regex_trigram.h` — `hasAlpha || hasBeta` → `hasAlpha && hasBeta`
- `test_scanner_reentry.h` — `>= 5` → `== 5`；`>= 1` → `== 1`
- `test_ranking.h` — `>= 3` → `== 4`
- `test_paged_persistence_v5.h` — `> 0` → `== 1024`

### P2: 修复测试结构

- `test_tilde_expansion.h` — 移除早期 `return` 中断，改用条件 `check()`
- `test_whitespace_trim.h` — HOME 检查仅门控 tilde 相关测试，不再跳过整个套件

## 验证

- 构建测试二进制：通过
- `./test_all --fast`：11786 passed, 0 failed
- 所有修改后的断言都是可失败的（non-tautological）
