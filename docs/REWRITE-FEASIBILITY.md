# 重写可行性评估报告

> 评估日期：2026-09-15 · 评估对象：`LisherSong/ps5-web-file-manager`（v1.9.1）
> 目标问题：项目源自他人代码、怀疑授权不清；若从零重写，改动量多大？能否做得更好？
>
> ⚠️ 本文是**工程视角**的合规盘点，不是法律意见。若涉及商用/闭源决策，请咨询律师。

---

## 0. 结论先行

| 问题 | 答案 |
|---|---|
| 上游真的没有许可吗？ | **否。上游是 GPL-3.0**，我们自己也是 GPL-3.0，两者一致 |
| 现在能合法发布吗？ | **能**。GPL-3.0 允许修改和再分发，只需满足归因 + 源码可得 |
| 有没有真实风险？ | 原本 5 个，**4 个已于 2026-09-23 关闭、第 5 个经确认接受现状**（见 §2）；**从头到尾没有一个需要重写** |
| 全量重写要多少人日？ | **35–50 人日**（约 1.4–2 万行需重写，第三方 7.1 万行可直接复用） |
| 值得重写吗？ | **取决于目标**：想闭源/商用 → 必须重写；想开源分享 → **完全不必** |

**最反直觉的一点**：我们自建的 7z 引擎核心（3,661 行）**只依赖公共领域和 zlib 许可的第三方库，与上游 GPL 代码零耦合** —— 它是完全干净的资产，今天就能单独抽成 MIT 授权的独立库。

---

## 1. 许可现状核查（事实，非猜测）

### 1.1 上游授权 —— 你的前提是错的

通过 GitHub API 查询（2026-09-15）：

```
owendswang/ps5-web-file-manager
  license: GPL-3.0    stars: 78    forks: 6
  created: 2026-06-16    last push: 2026-09-08    archived: false
```

上游**有明确许可**，且是 GPL-3.0。我们的 `LICENSE` 同样是 GPL-3.0，在 root commit `5cb0b76`（Initial import）时加入 —— **两边一致，不存在"无授权"的灰色地带**。

### 1.2 授权链条

```
ps5-payload-dev/websrv (John Törnblom, GPLv3+)
        │  源码里仍保留 "Copyright (C) 2024/2025 John Törnblom"
        │  —— asset.c / asset.h / mime.h / websrv.h 四个文件
        ▼
owendswang/ps5-web-file-manager (GPL-3.0)
        ▼
LisherSong/ps5-web-file-manager (GPL-3.0)  ← 本项目
```

`src/asset.c:1` 等文件里的 Törnblom 版权声明**至今完整保留** ✅ —— 说明 GPL §5(a)「保留版权声明」这一条在最上游那一环是满足的。

---

## 2. 真实风险清单

按严重度排序。**注意：没有一条需要重写代码来解决。**

| # | 风险 | 严重度 | 具体位置 | 修法 | 成本 |
|---|---|---|---|---|---|
| 1 | ~~**归因缺失**~~ → **已修正（2026-09-23）**：README Credits 已补上 `owendswang` 与 rarlab UnRAR / opello 镜像、并把已删除的 `third_party/unrar/` 从 Credits 里清理掉；`THIRD_PARTY_NOTICES` 本就正确。**残留**：git 历史里的 `5cb0b76 Initial import` 无法追溯上游提交 | 🟡 中 → 🟢 低 | `README.md` Credits | 历史归属只能在 Release 说明与 Credits 里声明；如需彻底重建历史得重写仓库 | 已完成 |
| 2 | **unRAR 与 GPL-3.0 的附加限制冲突**：UnRAR 许可禁止"用于开发 RAR 兼容压缩器"，GPL-3.0 §7 禁止附加限制，严格讲不兼容 | 🟡 中 | `third_party/unrar7/` | 见 §2.1 | 0（接受） |
| 3 | ~~**ezremote 是 GPLv2**：README 只写 "GPLv2"，未标 "or later"。GPLv2-only 与 GPL-3.0 **不兼容**~~ → **已定性并关闭（2026-09-23）**：确为 **GPL-2.0-only**，但逐行比对确认**我们未取其代码** | ✅ 已关闭 | `src/pkg_info.c`（PKG 预览） | 无需重写；README 中英措辞已改准，代码加了来源注记 —— 见 §2.2 | 0 |
| 4 | ~~**二进制分发需提供源码**（GPL §6）~~ → **已补（2026-09-23）**：v1.9.2 Release 说明的 License 段原先只链了**上游**仓库，未链本仓库 | ✅ 已关闭 | Release 里的 ELF | 已用 `gh release edit` 加入 "Corresponding source for this binary" 段（资产未动、仍非 draft/pre、仍是 latest）；今后发版沿用 `.build/release-notes-*.md` 模板 | 0 |
| 5 | Title ID `FMGR88888` 与上游相同，可能与他人 payload 冲突 | 🟢 低 | `Makefile:21` | ~~换一个自定义 ID~~ → **2026-09-23 决定维持现状（用户确认）**。核对结论：`TITLE_ID` 只出现在两处 —— `src/app_installer.c:86`（PKG 安装目标目录 `/user/app/<TITLE_ID>`）和 `src/version.c:31`（`/api/version` 上报），**与解压 / 上传 / 浏览功能无关**，且本项目是上游 fork 的直接替代者（同 ID 便于覆盖安装）。代价：与上游 payload **不能共存**，同时装会撞目录 | 0（接受） |

### 2.1 关于 unRAR（风险 2 详解）

UnRAR 许可原文允许"在任何软件中处理 RAR 归档"，但**禁止用它开发 RAR 兼容的压缩器**。这是一个"附加限制"，与 GPL-3.0 §7 冲突。

实务上的三种处理：

| 方案 | 做法 | 代价 |
|---|---|---|
| **A. 接受现状（推荐）** | 明确声明 unrar 部分适用其自有条款，其余部分 GPL-3.0 | 0 —— rarlab 官方自己就这么分发，社区普遍接受 |
| B. 移除 RAR 支持 | 删掉 `rar_extract.c` + `third_party/unrar7` | 失去 RAR（含分卷/加密）—— 不划算 |
| C. 换实现 | 找自由许可的 RAR 解码器 | **市面上不存在可用的**，死路 |

**建议 A**。风险等级实际很低：你只做解压不做压缩，本来就不触碰被禁止的那一条。

### 2.2 关于 ezremote（风险 3 详解，2026-09-23 结案）

**结论：风险不成立 —— 确为 `GPL-2.0-only`，但我们一行代码都没取。**

#### 第一步：许可措辞（用 `gh api` 查上游，不是猜）

```
gh api repos/cy33hc/ps5-ezremote-client    → license.spdx_id = "GPL-2.0"
gh api .../contents/LICENSE                → GNU GPL v2 全文（June 1991）
gh api .../contents/source/actions.cpp     → 无任何版权 / GPL 声明头
gh api .../contents/source/clients/*.h     → 同上，一个声明头都没有
```

上游**全部源文件都不带许可声明**，唯一的许可陈述就是那份 GPLv2 全文。
GPLv2 的 "or later" 只能由版权人**明示**授予（LICENSE 全文本身不含该授予），
所以按其自身现状应认定为 **`GPL-2.0-only`**。

这一点很关键：**`GPL-2.0-only` 与 GPL-3.0 不兼容**（这正是 FSF 发明
"GPLv2 or later" 惯例的原因）。所以「到底抄没抄」不是学术问题 ——
抄了就必须重写这个模块。

#### 第二步：逐行比对（决定性的一步）

上游与 PKG 相关的**只有一个文件**：`source/sfo.cpp`（4,209 B / 141 行 / C++）。
上游**没有 `.pkg` 容器解析器** —— tree 里的 `Ps5_ezRemote_Client_2.00.pkg`
是一个已编译的 payload（10 MB），不是源码。

| 维度 | 上游 `sfo.cpp` | 我们的 `src/pkg_info.c` |
|---|---|---|
| 语言 | C++（`reinterpret_cast` / `std::map` / `namespace SFO`） | **C99** |
| 函数分解 | 三个独立函数 `GetString` / `GetParams` / `GetParamsFromParamJson` | 单个 `append_sfo_fields(strbuf_t *, ...)` 直接流式产出 JSON 片段 |
| 返回值 | `std::map<std::string,std::string>` | 写进 `strbuf_t`，无中间容器 |
| JSON | 依赖 **json-c**（`json_tokener_parse` / `json_object_object_get`） | **自写分词器**（`parse_json_tokens()` / `json_object_value()`，在 `json_util.c`） |
| 越界防护 | 仅两处 `size <` 检查，其余裸指针 + `reinterpret_cast` | 逐项校验（`count > SFO_ENTRY_MAX`、`index_end > size`、`key_offset < index_end`、`memchr` 找 NUL、`read_le32` 定长读） |
| 覆盖范围 | 只有 SFO + param.json | 另有 **`.pkg` 条目表**（`PKG_CNT_MAGIC` / FIH / LIH / 条目类型 `0x1000` / `0x1200` / `0x121f` / `0x2000`）、本地化图标选择、两个 HTTP 端点 |

**唯一重合的是格式事实**：SFO magic `0x46535000`、20 字节头 / 16 字节条目、
`keyofs`+`nameofs` 与 `valofs`+`dataofs` 的间接寻址。这些是 PS5 文件格式的客观规定，
也是解析它的**唯一办法**（等同合并原则），不构成可保护的表达。

⇒ **不存在代码衍生关系**，`src/pkg_info.c` 无需重写。

#### 第三步：已落地的处置

1. `README.md` / `README.zh-CN.md` 的 Credits 条目：由 "Preview PKG info. License: GPLv2"
   改为明确写出 **GPL-2.0-only、与本项目不兼容、未取其代码**，并指向本节 ——
   后来人不会再把它当成"我们的依赖"去理解授权链。
2. `src/pkg_info.c` 文件头补了来源注记（含比对理由与本节指引）。
   **纯注释，不改变编译产物** —— 已确认 `src/` 内没有 `__LINE__` / `__FILE__` 依赖，
   注释被预处理器丢弃后目标文件逐字节相同。
3. 本节即为 provenance 留档。

> 适用范围：这套「先查许可措辞 → 再做逐行比对 → 最后把结论写进代码注记」的流程，
> 对任何「README 里 credits 了某个项目」的情形都适用。因为 README 的 Credits 段落里
> 混着两类东西：**真正 vendored 的代码**（minizip-ng / zlib / UnRAR）和
> **只是参考了思路的项目**（websrv / ftpsrv / zftpd / etaHEN / ezremote）。
> 两者在授权义务上完全不同，但排版把它们放在同一张列表里 —— 这就是这个疑问的由来。

---

## 3. 代码归属盘点（决定重写成本的关键）

### 3.1 总量分布

| 类别 | 行数 | 重写时怎么办 |
|---|---:|---|
| **第三方 vendored** | **71,528** | ♻️ **直接重新 vendor，一行都不用写** |
| ├ zlib 1.3.1 | 20,106 | zlib 许可 |
| ├ unrar7 7.20.1 | 27,710 | UnRAR 许可 |
| ├ LZMA SDK 26.03 | 17,248 | **公共领域** |
| └ minizip-ng 4.2.2 | 6,464 | zlib 许可 |
| **第一方代码** | **20,844** | 这是唯一需要写的部分 |
| ├ v1.7 上游遗产 | 13,860 | 🔴 受 GPL 约束 |
| └ 我们新增 | 6,984 | 🟢 版权归我们 |

**这是整份报告最重要的数字**：项目里 **77% 的代码是第三方库**，重写时原样搬走即可。真正需要动手的只有 2 万行第一方代码，其中又只有不到 1.4 万行受 GPL 约束。

### 3.2 我们新增代码的独立性分析

v1.7 之后**新建**的文件（6,745 行），逐个检查其依赖：

| 文件 | 行数 | 依赖 | 能否独立授权 |
|---|---:|---|:---:|
| `sevenz_chain.c/.h` | 2,094 | 仅 LZMA SDK（**公共领域**） | ✅ **完全干净** |
| `zipx_volume.c/.h` | 658 | 仅自身 | ✅ **完全干净** |
| `zipx_volstream.c/.h` | 494 | minizip-ng（zlib）+ zipx_volume.h | ✅ **完全干净** |
| `sevenz_volstream.c/.h` | 415 | LZMA SDK + zipx_volume.h | ✅ **完全干净** |
| `rar_extract.c/.h` | 1,169 | `zip_extract.h`（共享协议） | ⚠️ 弱耦合，抽协议即可解绑 |
| `sevenz_extract.c/.h` | 1,790 | `zip_extract.h`（共享协议） | ⚠️ 弱耦合，抽协议即可解绑 |
| `zipx_common.c` | 99 | `zip_extract.h` | ⚠️ 内容仅限额/状态串，30 分钟可重写 |
| `cpu_support_stub.c` | 26 | 无 | ✅ 干净 |

**核心结论**：
- **3,661 行（7z 解码链 + 分卷流抽象）与 GPL 代码零耦合** —— 这是项目最有价值的部分（自解析 folder + pull 式 codec 链 + BCJ2 + 7zAES + 三种分卷语义），也是投入最多的部分。它们**今天就可以抽成独立的 MIT/Apache 库**，不受上游任何影响。
- 另有 2,959 行只通过 `zip_extract.h` 的**共享协议**（状态枚举、进度回调、限额结构）与上游耦合。把那 100 行协议定义抽成独立的 `archive_api.h` 就能解绑。

---

## 4. 三条路径对比

### 路径 A：维持 GPL-3.0 + 补齐合规（推荐）

| 项 | 内容 |
|---|---|
| 做什么 | README 补 owendswang 署名、加 NOTICE、确认 ezremote 许可、Release 附源码链接、换 Title ID（→ 末项已于 2026-09-23 决定维持现状，见风险 5） |
| 成本 | **0.5–1 人日** |
| 收益 | 合规闭环，零功能损失，保留全部现有能力 |
| 风险 | 无 |
| 适合 | **想开源分享、想让成果被保护** |

### 路径 B：架构重构（保留 GPL-3.0）

| 项 | 内容 |
|---|---|
| 做什么 | 分层重写：platform 层 / archive 引擎层 / HTTP 层 / 前端模块化；抽 `archive_api.h` 解耦；统一进度模型 |
| 成本 | **12–18 人日** |
| 收益 | 代码可维护性大幅提升，引擎可独立成库，修掉 §6 的已知缺陷 |
| 风险 | 中 —— 需真机回归，163 个 host check 要全绿 |
| 适合 | **觉得现在代码烂、想长期维护** |

### 路径 C：Clean-room 全量重写（换许可）

| 项 | 内容 |
|---|---|
| 做什么 | 不参考上游代码，按功能规格从零写 ~13,860 行受 GPL 约束的部分 |
| 成本 | **35–50 人日**（含真机调试） |
| 收益 | 完全自有版权，可任选许可（含闭源商用） |
| 风险 | **高** —— clean-room 必须严格隔离：写代码的人不能看过上游源码；否则法律上无效 |
| 适合 | **确定要闭源/商用** |

### 4.1 路径 C 的工作量拆解

| 模块 | 行数 | 难度 | 人日 |
|---|---:|---:|---:|
| HTTP 服务 + 路由（websrv/main/asset/mime/file_response） | ~700 | 低 | 3 |
| 文件管理核心（filemgr.c） | 2,458 | **高** | 8 |
| 上传 / 下载 / 断点续传 | 1,343 | 中 | 5 |
| 文件系统工具（fs_util/path_util/list/space/text） | 1,229 | 中 | 4 |
| PKG 安装 / 信息解析 / app_installer | 847 | 中 | 4 |
| 任务调度 + 通知（task/notify） | 253 | 低 | 1.5 |
| ZIP 引擎 + 分卷（zip_extract/zipx_*） | 3,091 | **高** | 8 |
| 前端（main.js / main.css / index.html / i18n） | 4,779 | 中 | 6 |
| 构建系统 + 资产内嵌 | ~200 | 低 | 1 |
| 测试矩阵（对齐现有 163 checks） | — | 中 | 5 |
| 真机调试 + PS5 平台适配 | — | **高** | 6 |
| **合计** | **~14,900** | | **≈ 51 人日** |

可复用的：7z 全套（4,299 行）+ zipx 分卷（1,193 行）+ 第三方（71,528 行）—— 这三项占了重头戏，所以才是 50 人日而不是 150 人日。

---

## 5. 重写能做到"比现在更好"的地方

如果真要走 B 或 C，这些是现在已知的技术债，**顺手一起解决才值得动**：

| # | 现有问题 | 位置 | 改进方案 |
|---|---|---|---|
| 1 | **进度口径三处不一致**：进度条按字节、文字按条目数、ETA 按字节速度，混合大包上体验割裂 | `main.js:1985` / `main.js:2014` / `task.c:129-177` | 统一为字节口径，ETA 用滑动窗口 |
| 2 | **PS5 `*at()` 族运行时损坏**（返回 -1 且 errno=0），现有绕行逻辑散落在 `zip_extract.c` | `zip_extract.c` | 抽 platform 层集中处理，写新代码不再踩 |
| 3 | 引擎与 HTTP 层耦合：解压协议定义在 `zip_extract.h` 里 | `zip_extract.h` | 抽 `archive_api.h`，引擎可独立成库 |
| 4 | `main.js` 2,652 行单文件，无模块拆分 | `assets/main.js` | 按 view / api / task 拆模块 |
| 5 | `filemgr.c` 2,458 行，路由 + 业务逻辑 + 平台调用混在一起 | `src/filemgr.c` | 分 handler / service / platform 三层 |
| 6 | 测试靠手工脚本，未接入 `make test` | `tests/` | 接 CI，覆盖率可量化 |
| 7 | ~~唯一功能缺口：7z `-mhe=on` 加密头~~ **已闭合**（2026-09-23，`src/sevenz_header.c`） | `sevenz_extract.c` | 无剩余格式缺口；重写时该项可删 |

---

## 6. 建议

### 6.1 我的推荐：路径 A，外加一条"资产剥离"

**不要全量重写。** 三个理由：

1. **GPL-3.0 对你有利，不是负担**。它保证别人拿走你的 7z 引擎成果后**必须同样开源**。换成 MIT，别人可以直接闭源拿去卖 —— 你花了大量精力做的 BCJ2 链、7zAES、分卷流抽象会被白嫖。
2. **重写的法律风险比不重写更高**。你已经看过上游源码了，clean-room 的前提已被破坏。真重写必须找没看过上游的人来做，还得隔离沟通 —— 成本远超 50 人日。
3. **你的核心资产本来就是干净的**。3,661 行的 7z 解码链 + 分卷流只依赖公共领域和 zlib 许可，**现在就能单独抽出来做 MIT 授权的独立库**，不需要动主项目一根指头。

### 6.2 立刻可做的三件事（共 1 天）

```
1. README.md Credits 补一行：
   - [owendswang/ps5-web-file-manager](...): base implementation. License: GPL-3.0.

2. 把 sevenz_chain.{c,h} + sevenz_volstream.{c,h} + zipx_volume.{c,h}
   + zipx_volstream.{c,h} 抽成独立仓库，MIT 授权，主项目作为 submodule 引用。
   → 3,661 行成果立刻获得独立身份，且证明这部分是你的原创。

3. ~~确认 ezremote 是 "GPLv2" 还是 "GPLv2 or later"；若是 v2-only，重写 pkg_info.c~~
   → **已结案（2026-09-23）**：是 `GPL-2.0-only`，但逐行比对确认**我们未取其代码**，
   **无需重写**。比对记录与处置见 §2.2。
4. ~~v1.9.2 Release 说明补本仓库链接（GPL §6 源码提供义务）~~
   → **已补（2026-09-23）**，见风险 4 行。
```

### 6.3 需要你回答的问题

**你重写的动机到底是什么？** 不同答案对应完全不同的方案：

| 如果你的目标是… | 应该走 | 成本 |
|---|---|---|
| 想闭源 / 商业化 | C（且必须找没看过上游的人写） | 50+ 人日 |
| 只是担心"没许可"不合法 | **A** —— 你的担心不成立 | 0.5 天 |
| 想让别人知道这是你写的 | **A** —— GPL 允许你在修改部分署名 | 0.5 天 |
| 觉得代码质量差、想重构 | B | 12–18 人日 |
| 想保护成果不被闭源 | **A** —— GPL-3.0 已经是最佳选择 | 0 天 |

---

## 附录：核查方法与数据来源

| 项 | 来源 |
|---|---|
| 上游许可 | GitHub API `repos/owendswang/ps5-web-file-manager`，2026-09-15 查询 |
| 本项目许可 | `LICENSE`（35,149 B，GPL-3.0 全文），root commit `5cb0b76` 引入 |
| 代码行数 | `wc -l` 于 v1.9.1 工作树；上游基线取 `git show 5cb0b76:<file>` |
| 文件归属 | `git ls-tree -r 5cb0b76 -- src assets` 与当前工作树的差集 |
| 依赖分析 | 逐个 grep `#include "` 于自有文件 |
| 已知缺陷 | 项目 `HANDOVER.md` 第四节 + `.workbuddy/memory/MEMORY.md` |
