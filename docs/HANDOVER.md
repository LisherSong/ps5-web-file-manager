# PS5 Web File Manager — v1.8 开发方案（已废弃）

> **这份文件已不再维护，不要拿它当参考。**
>
> 原文写于 **2026-09-04**，是一份 **v1.8（RAR 支持）开发计划**，对应代码版本 `aef4a44`
> （v1.7 时代）。其中大量结论已被后续版本推翻，例如它至今仍写着：
>
> - 「❌ RAR 分卷」「❌ 加密 RAR」
> - 「dmc_unrar 不支持 / 已移除」
> - 「7z 尚不支持」「加密解压尚未接线」
>
> 而 v1.9 已用 rarlab UnRAR 7.20.1 解决 RAR 分卷与加密，v1.9.2/v1.9.3 补齐了 ZIP/RAR/7z
> 三格式加密内容与 7z `-mhe=on` 加密头，dmc_unrar 也已整体移除。

## 当前权威文档（按优先级）

| 文档 | 用途 |
|---|---|
| 仓库根 [`../HANDOVER.md`](../HANDOVER.md) | **状态速览、真机待验证清单、坑清单 —— 以此为唯一准绳** |
| [`../README.md`](../README.md) / [`../README.zh-CN.md`](../README.zh-CN.md) | 功能范围与使用方式 |
| [`../CHANGELOG.md`](../CHANGELOG.md) | 逐版本变更 |
| [`EXTRACTION-PERF.md`](./EXTRACTION-PERF.md) | 解压性能实测与优化账目 |
| [`REAL-CONSOLE-PROFILE.md`](./REAL-CONSOLE-PROFILE.md) | 真机性能验证清单 |
| [`REWRITE-FEASIBILITY.md`](./REWRITE-FEASIBILITY.md) | 许可与拆库可行性分析 |
| [`SIZE-OPTIMIZATION.md`](./SIZE-OPTIMIZATION.md) | 二进制体积优化记录 |
| [`UPSTREAM-V1.8-COMPARISON.md`](./UPSTREAM-V1.8-COMPARISON.md) | 与上游 v1.8 helper 路线的对比 |

**任何关于「现在支持什么」的陈述，一律以根 `HANDOVER.md` 与源码为准。**

## 为什么归档

这份 1713 行（65 KB）的文件停留在 v1.7/v1.8 时代，却与根 `HANDOVER.md` 并行存在。
它的过时结论在多轮开发中**反复误导整仓 grep**（本项目自己就踩过数次：搜 "RAR 分卷"/"加密"
会命中这里，得到与现状完全相反的答案），因此 2026-09-23 把它移出主文档树。

保留它的唯一理由是**历史价值**：§9「坑清单」以及 v1.7/v1.8 时代的设计推导，
对理解「当时为什么这么取舍、踩过什么坑」仍有考古意义。

原文完整副本（内容一字未改，含归档前的 md5）：
**[`archive/HANDOVER-v1.8-planning.md`](./archive/HANDOVER-v1.8-planning.md)**
