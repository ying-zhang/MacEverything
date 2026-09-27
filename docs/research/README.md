# 研究与审查文档

## 当前文档

- [当前主分支审查](main-branch-review.md)：当前代码状态、已修复问题、剩余风险和验证要求。
- [第二轮修复审查](review-round2-of-codex-fixes.md)：对本轮修复的独立复核、剩余风险和测试缺口。
- [历史交叉检验快照](cross_check_findings.md)：保留历史审查证据；其中的当前状态以主分支审查为准。
- [FSEvents CI 失败分析](fsevents_ci_failures.md)：历史问题的根因、复现方式和修复背景。
- [竞品分析](competitive_analysis.md)：文件搜索产品和索引算法的背景研究。
- [Fork 对比分析](fork_analysis_joshua_wu.md)：与上游 fork 的历史功能对比。

## 文档边界

`main-branch-review.md` 是当前状态的唯一审查入口。历史研究和基准数据保留原始时间点，不应直接当作当前性能或缺陷结论。性能数据集中在 [`docs/benchmark/`](../benchmark/README.md)，功能变化集中在 [`docs/changelog/`](../changelog/README.md)。

历史交叉检验快照保留原始证据，避免丢失审查上下文；当前状态、修复结论和验证命令只维护在 `main-branch-review.md`。
