# 058 - 退出时强制 Content Index Compact

## 背景

日志分析发现：退出时 IndexPersistence 的 compact 传了 `force=true`，但 ContentIndexPersistence 的 compact 未传 force，导致退出时因 `dirty_=false` 跳过 compact。WAL 文件中的历史条目（131 条）无法被清理，每次重启都要重新回放。

## 根因

`MacSearchBridge.mm` 第 528 行 `contentPersistence->compact()` 缺少 `force=true` 参数，与第 524 行 `persistence->compact(lastEventId, /*force=*/true)` 不一致。

## 修复

```diff
- contentPersistence->compact();
+ contentPersistence->compact(/*force=*/true);
```

## 预期效果

- 退出时 content WAL 被完整 compact 到基础索引文件
- 下次启动时 content WAL 回放条目数为 0，启动更快
