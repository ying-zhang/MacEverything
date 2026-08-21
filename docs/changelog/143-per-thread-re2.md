# v143: Per-Thread RE2 Clones to Eliminate DFA Mutex Contention

## 背景

基准测试表明，RE2 的 DFA（确定性有限自动机）使用内部 Mutex 进行惰性构建。在 GCD `dispatch_apply` 多线程搜索中共享同一个 `RE2` 对象时，会产生严重的 DFA mutex 争用：

| 配置 | 耗时 |
|------|------|
| 单线程 | 561ms |
| 12线程共享 RE2 | 2497ms (反而慢 4.5x) |
| 12线程 per-thread RE2 | 149ms (加速 3.8x) |

## 问题根因

`SearchEngineAdvancedQuery.cpp` 中的 `RegexCache`（每个 regex AST 节点对应一个 `RE2` 对象）在 `dispatch_apply` 中被所有线程以 `const&` 共享。虽然 `RE2::PartialMatch` 是 const 方法，但 RE2 内部在首次匹配时会惰性构建 DFA 并加锁，导致多线程争用。

## 实施方案

### 新增 `cloneRegexCachePerThread()` 辅助函数

在 `dispatch_apply` 之前，为每个线程创建独立的 `RegexCache` 副本。每个副本包含从相同 pattern 编译的独立 `RE2` 对象，消除了 DFA mutex 争用。

```cpp
static std::vector<RegexCache> cloneRegexCachePerThread(
    const RegexCache& src, unsigned numThreads) {
    std::vector<RegexCache> perThread(numThreads);
    if (src.empty()) return perThread;
    for (unsigned t = 0; t < numThreads; t++) {
        for (const auto& [nodePtr, re] : src) {
            re2::RE2::Options opts;
            opts.set_case_sensitive(re->options().case_sensitive());
            opts.set_log_errors(false);
            perThread[t].emplace(nodePtr,
                std::make_unique<re2::RE2>(re->pattern(), opts));
        }
    }
    return perThread;
}
```

### 修改两个 `dispatch_apply` 站点

1. **Trigram 过滤路径**（大候选集）：用 per-thread cache 替换共享 regCache
2. **线性扫描路径**：同上

### 不变部分

- 单线程路径无需克隆（无争用）
- `regexCache` 为空时跳过克隆（大部分查询无 regex 项，零开销）
- `evalTerm` / `evalNode` 函数签名不变

## 开销分析

- RE2 编译非常快（每个 pattern 微秒级），远小于搜索节省的时间
- 内存：`numThreads × regex项数` 个额外 RE2 对象（典型 1-2 项 × 12 线程 = 12-24 个，每个 KB 级）

## 变更统计

```
MacEverything/Core/SearchEngineAdvancedQuery.cpp | 30 ++++++++++++++++++++++--
1 file changed, 28 insertions(+), 2 deletions(-)
```

## 测试

- 全部 11979 个快速测试通过
- xcodebuild Release 构建成功
