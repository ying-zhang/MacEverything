# 007 - Bridge 层编译错误与 lint 检查

- **类型**: bugfix
- **严重级别**: P2-MEDIUM
- **日期**: 2026-04-14
- **Commit**: `cc18d53`

## 问题描述

`MacSearchBridge.mm` 中 `snippetResults` 变量在 Objective-C++ block 中被捕获时缺少 `__block` 修饰符，导致编译错误。同时，Bridge 层缺乏自动化编译检查，此类错误难以在测试阶段发现。

## 修复方案

1. **编译错误修复**：为 `snippetResults` 添加 `__block` 修饰符，修复 ObjC++ block 中的 const-capture 错误
2. **lint 检查**：新增 `lint-bridge` Makefile 目标，使用 `clang++ -fsyntax-only` 在 `test-fast` 阶段检查 Bridge 层编译错误

## 影响的文件

- `MacEverything/Bridge/MacSearchBridge.mm`
- `Makefile`

## 测试覆盖

新增 lint-bridge 编译检查纳入 test-fast 流程。
