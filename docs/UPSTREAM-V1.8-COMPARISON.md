# 上游 v1.8 解压方案 vs 本项目 v1.9.1

> 核查时间：2026-09-15 · 上游 `owendswang/ps5-web-file-manager`
> 来源：GitHub API 查询 + commit `b405721`（"Added support for 7zip helper"，2026-09-08）完整 patch（2301 行）
> 上游 v1.8 = tag `ad7d754`，v1.7 = `72341d6`（本项目 fork 的基线）

## 结论速览

**不是同一个层面的方案，各有明确胜负手。**

| | 上游 v1.8 | 本项目 v1.9.1 |
|---|---|---|
| 一句话 | **把 7-Zip 本体做成外部 helper 进程，靠 IPC 调用** | **自研 + vendor 解码库，全部内嵌在同一进程** |
| 最强的点 | 格式覆盖 **30 种**，解压核心是 7-Zip 本体 | **完整安全护栏** + 单文件部署 |
| 最弱的点 | **零安全护栏**，且 helper 缺失 = 功能全废 | 格式覆盖只有 **3 种** |

## 一、上游 v1.8 的实际架构

### 1.1 三个组件

| 组件 | 位置 | 职责 |
|---|---|---|
| `src/archive_extract.c`（124 行） | 本仓库 | 只做**后缀识别** + 输出目录名推导 |
| `src/archive_helper.c`（732 行） | 本仓库 | **IPC 客户端**：启动 helper + Unix socket 协议 |
| `wfm-7zip-helper.elf`（~百 MB 级） | `/data/wfm/`，**不在仓库里，单独分发** | 真正的解压 = **7-Zip 本体** |

README 原文：

> Extraction requires the separately distributed `wfm-7zip-helper.elf` helper at `/data/wfm/wfm-7zip-helper.elf`.

### 1.2 启动链路

```c
/* archive_helper_autostart() —— 仅 __SCE__（PS5）分支，Linux 直接返回 0 */
1. archive_helper_probe()     // 已有实例在跑就复用，绝不替换
2. stat("/data/wfm/wfm-7zip-helper.elf")   // 不存在 → 静默返回 0
3. 校验 ELF magic "\x7fELF"、大小 4B ~ 128MB
4. connect(127.0.0.1:9021)    // WFM_ELFLDR_PORT —— elfldr payload 加载器
5. 把 helper ELF 的**全部字节流**推过去
6. shutdown(SHUT_WR)
```

即：**通过 elfldr（PS5 homebrew 的 ELF 加载 payload）把 helper 拉起成一个独立进程。**

### 1.3 通信协议（自研二进制帧）

- 传输层：Unix domain socket —— PS5 走 `/system_tmp/wfm-7zip-helper.sock`，Linux 走 `/tmp/...`
- 帧格式：magic `"W7HP"` + 20 字节头（type / flags / request_id / payload_size，**大端序**）
- 上限：payload 1 MiB、路径 256 KiB、响应 64 KiB

消息类型：

| 方向 | 消息 |
|---|---|
| 主 → helper | `PING` `EXTRACT` `CANCEL` `LIST_TASKS` `ATTACH_TASK` `ACK_TASK` |
| helper → 主 | `PONG` `ACCEPTED` `PROGRESS` `CURRENT_FILE` `PASSWORD_REQUIRED` `DONE` `ERROR` `TASK_SNAPSHOT` `LIST_DONE` |

回调接口 `archive_helper_callbacks_t`：`cancel_requested()` / `progress(done,total)` / `current_file(path)`。

### 1.4 支持格式（30 种后缀）

```
.7z .001 .zip .zipx .rar .arj .bz2 .bzip2 .tbz .tbz2 .cab .gz .gzip
.tgz .tpz .lzh .lha .tar .xz .txz .z .taz .zst .tzst .xar .xip
.cpio .lzma .pmd
```

外加 `.partNN.rar`（只接受 `part1`，即必须从第一卷进入）。
分卷靠 7-Zip 原生能力：`.001` **无差别接受**（不校验卷集连续性）。

### 1.5 任务恢复（上游的亮点）

`filemgr_recover_extract_tasks()` 在 `main.c` 启动时调用：从 helper 拉 `TASK_SNAPSHOT` 列表，把还在跑的 job **reattach 回主进程的任务列表**。

因为 helper 是独立进程，**主 payload 被重启 / 浏览器重开，解压任务不会丢**。`archive_helper_probe()` 的注释也点明了这个设计的意图：

```c
/* Never replace a connected daemon, even if it is temporarily slow. */
```

### 1.6 ⚠️ 没有的东西（全 patch 逐行核查）

| 项目 | 上游 v1.8 | 说明 |
|---|---|---|
| 条目数上限 | ❌ | 无 `max_entries` 类逻辑 |
| 单文件/总大小上限 | ❌ | 无 |
| 压缩比筛查（防炸弹） | ❌ | 无 |
| 磁盘空间预检 | ❌ | 无 `statvfs` 调用 |
| 路径穿越防护 | ❌ | 未见 `..`/绝对路径校验，交给 7-Zip |
| 原子发布 | ❌（未见） | 直接解到目标目录，中断留半成品 |

`grep -i "ratio|max_entries|statvfs|bomb"` 的全部命中都是误报（`operations` 里含子串 `ratio`）。

**换句话说：上游把解压这件事整体外包给了 7-Zip，包括安全责任。**

## 二、本项目 v1.9.1 的架构

| 组件 | 职责 |
|---|---|
| `src/zip_extract.c` | ZIP：minizip-ng，含 zip64、三种分卷命名、`.z01` 真分盘语义 |
| `src/rar_extract.c` | RAR：vendor unrar 7.20.1（DLL 模式），v4/v5/多卷/加密 |
| `src/sevenz_extract.c` + `sevenz_chain.c` | 7z：自解析 folder + pull 式 codec 链 + 7zAES |
| `src/zipx_volume.c` / `zipx_volstream.c` / `sevenz_volstream.c` | 卷集识别 + 连续流抽象 |

**格式覆盖：`.zip` / `.rar` / `.7z` 三种**，各自支持单卷 / 分卷 / 密码。

### 已有的工程能力

| 项目 | 本项目 | 实现位置 |
|---|---|---|
| 条目数 / 总大小 / 单文件上限 | ✅ 两档 profile（20万~50万条目 / 2~4 TiB / 512 GiB~1 TiB） | `zipx_common.c` |
| 压缩比筛查 | ✅ `max_ratio` 500/1000，**1 GiB 下限豁免**小文件 | `zip_extract.c` |
| 磁盘空间预检 | ✅ `check_space()` 按**解压后总量**查 `statvfs` | `zip_extract.c:636` |
| 路径穿越防护 | ✅ 有专项测试（`path traversal variants`） | 测试矩阵 |
| 原子发布 | ✅ staging 目录 + 整 rename + 每 entry fsync | 三引擎统一 |
| 冲突策略 | ✅ FAIL / OVERWRITE / MERGE，目录碰撞递归下钻 | 三引擎统一 |
| 取消 | ✅ 条目粒度 | — |
| 任务恢复 | ❌ **没有** | — |
| 内存隔离 | ❌ 与主进程共享地址空间（LZMA2 字典须封顶） | — |

### 测试覆盖

ZIP 108 + RAR 27 + 7z 28 = **163 checks**，0 失败（MinGW host）+ PS5 真机构建通过。

## 三、逐维度对比

| 维度 | 上游 v1.8 | 本项目 v1.9.1 | 胜 |
|---|---|---|---|
| 格式覆盖 | **30 种** | 3 种 | 上游 |
| 解压核心正确性 | 7-Zip 本体（20 年验证） | 自研 7z 链 + 成熟 vendor 库 | 上游 |
| 分卷语义 | 靠 7-Zip 原生（`.001` 无差别） | 自研两套语义（byte-split / zip split disk） | 平手（我们更细，上游更省心） |
| `.rar.001` | ✅ 直接吃 | ⚠️ 需改名为 `.partN.rar` | 上游 |
| 部署 | **两个文件**，路径写死 `/data/wfm/` | **单文件**，零外部依赖 | 我们 |
| helper 缺失时 | **功能全废**（`archive_helper_not_running`） | 不适用 | 我们 |
| 防压缩炸弹 | ❌ 无 | ✅ ratio + 1 GiB 下限 | **我们** |
| 磁盘写满保护 | ❌ 无 | ✅ 预检解压后总量 | **我们** |
| 路径穿越 | ❌ 无 | ✅ 有防护 + 测试 | **我们** |
| 中断留残留 | ⚠️ 可能留半成品 | ✅ staging 隔离，失败即清 | **我们** |
| 任务恢复 / 跨重启 | ✅ 跨进程 reattach | ❌ | 上游 |
| 内存隔离 | ✅ 独立进程，峰值不影响主服务 | ❌ 共享地址空间 | 上游 |
| 主仓库构建成本 | 低（不编 7-Zip） | 首次 +3~5 min、ELF +98 KiB | 上游 |
| 错误信息详细度 | 中等（6 个 code） | 含条目名 / errno / 字节数 | 我们 |

## 四、该怎么评价

### 上游那步棋走对了什么

**把 7-Zip 当外部依赖，是性价比极高的工程决策。** 自己写解码器要几个月，`apt` 一个 7-Zip 就换来 30 种格式 + 20 年验证的正确性。而且顺手拿到了两个我们暂时没有的能力：跨进程任务恢复、内存隔离。

### 但它把安全责任也一起外包了

这是**实质缺陷**，不是风格问题。在 PS5 上跑的具体后果：

1. **压缩炸弹直接写满内置存储** —— 一个 10 KB 的 zip 可以声明 100 GB，没有任何拦截
2. **路径穿越** —— `../../` 条目可以写到解压目标之外（7-Zip 本身会做基本清理，但这属于"相信第三方"而非"自己保证"）
3. **磁盘写满** —— 不预检，写到 ENOSPC 才失败，此时已留下部分文件
4. **失败留残留** —— 没有 staging 隔离

我们在这四项上都有明确实现和测试。163 checks 里专门有一组 `path traversal variants` 和 `limits`。

### 但必须承认格式覆盖是短板

31 种格式的差距不是"多一点便利"，是**用户会觉得我们弱**：`.tar.gz`、`.xz`、`.zst`、`.bz2` 在 PS5 场景（游戏包、备份、Mod）里出现频率不低。

## 五、可借鉴 / 不建议照抄

### 建议做：补常见格式（性价比高）

按实际收益排序：

| 优先级 | 格式 | 实现路径 |
|---|---|---|
| 高 | `.tar` / `.tar.gz` / `.tgz` | tar 解析器自己写（格式极简，~300 行）+ zlib 已在手 |
| 高 | `.gz` / `.xz` / `.lzma` | gzip 用 zlib；xz/lzma 可 vendor liblzma 或复用 LZMA SDK 的 LzmaDec |
| 中 | `.bz2` / `.zst` | 单文件解码器，各 ~1000 行，可 vendor |
| 低 | `.cab` / `.arj` / `.lzh` / `.cpio` / `.xar` | 罕见，除非有具体需求 |

**注意**：`gzip`/`xz`/`zst` 是**单文件**格式（不是归档），解出来就是一个文件，输出路径语义需单独设计。

### 建议评估：任务恢复 / 进程隔离

**动机**：主 payload 被系统杀或用户重开浏览器时，正在跑的大包解压会整个丢失。上游靠 helper 独立进程解决了这点。

**但在我们架构下的成本**：需要引入子进程 + IPC（或至少状态持久化 + 重启后重扫 staging）。PS5 上 fork/exec 与 elfldr 强耦合，不是小改动。

**中间路线**：解压失败时保留 staging 目录 + 记录任务清单文件，重启后支持"续解"。改动量中等，能拿到大部分收益，不必引入 IPC。

### 不建议：照抄 helper 路线

理由：

1. **部署体验倒退** —— 用户要装两个文件，还得记住放 `/data/wfm/`；丢一个功能全废。现在单 ELF 是无状态交付，这是真实优势
2. **安全护栏会一起丢** —— 走 7-Zip 就意味着放弃我们对 entries/ratio/空间/穿越的控制
3. **helper 上游自己都不敢放进仓库**（"separately distributed"），大概率是体积或许可原因，跟着走会继承同样的问题
4. **我们已经付过的成本会沉没** —— 7z 引擎（自解析 folder + pull 链 + 7zAES）+ 三类分卷抽象共约 3,600 行零耦合代码

### 一句话总结

**上游赢在"格式广度 + 进程架构"，我们赢在"安全 + 部署 + 错误质量"。**

如果只想要功能广度，上游的路线更省力；如果要一个**能放心交付给用户、不会被一个恶意压缩包搞崩存储**的工具，我们的路线是对的，缺的只是格式覆盖 —— 而那是可以在现有架构里增量补的，不需要推倒重来。

## 附：核查方法备忘

```bash
# 拿某个 commit 的完整 patch（不要用 WebFetch，会被 AI 摘要截断）
curl -sSL --ssl-no-revoke -o up.patch \
  "https://github.com/<owner>/<repo>/commit/<sha>.patch"

# 沙箱内 curl 必须加 --ssl-no-revoke，否则 schannel 报
# CRYPT_E_NO_REVOCATION_CHECK (0x80092012)

# 提取单个文件的 diff
sed -n '/^diff --git a\/src\/foo.c/,/^diff --git a\/src\/bar/p' up.patch

# 只看新增行（去掉 diff 前缀）
... | grep '^+' | sed 's/^+//'
```
