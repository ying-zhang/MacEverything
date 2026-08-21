# 140 — DIAG-TRIGRAM 诊断日志

## 背景

P11（间歇性 Trigram 索引回退）在 R8 会话4 中被发现：Phase 2 完成后，所有 3+ 字符查询在 ~10-15 分钟内走 `advanced-linear-gcd candidates=0`，而非 `advanced-trigram`。随后自行恢复，无需重启。根因未知，需要诊断数据。

## 变更内容

在 `SearchEngineAdvancedQuery.cpp` 的 Stage 1 Name trigram 选择逻辑后，添加了 `DIAG-TRIGRAM` 诊断日志（commit `7220eac`）。

当 3+ 字符查询未走 trigram 路径时，记录跳过原因和关键状态：

- `useTrigram=false` — 查询类型不支持 trigram
- `indexEmpty` — `nameTrigramIndex_` 为空
- `allFound=false` — 查询的 trigram 在索引中找不到
- `tooManyCands` — 候选集超过 `totalSize / 10` 阈值

同时记录辅助诊断字段：
- `phase2Pending` — Phase 2 是否仍在进行
- `indexBuckets` — trigram 索引桶数量
- `rawCands` — 原始候选数
- `threshold` — 当前阈值
- `totalSize` — 总记录数

## 修改文件

| 文件 | 变更 |
|------|------|
| `MacEverything/Core/SearchEngineAdvancedQuery.cpp` | Stage 1 后添加 DIAG-TRIGRAM 日志块 |

## 验证结果

R9 轮测试确认：
- CJK 查询（`桌面`、`应用程序`）正确触发 DIAG-TRIGRAM，报告 `allFound=false, rawCands=0`（预期行为，因 macOS 路径为英文）
- 所有英文 3+ 字符查询正常走 trigram，未触发诊断日志
- 等待下一次间歇性回退以捕获 P11 根因数据
