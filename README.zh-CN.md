<div align="right">
  <a href="README.md">English</a> · <a href="README.zh-CN.md">简体中文</a>
</div>

# PS5 网页文件管理器（PS5 Web File Manager）

> 面向已越狱 PS5 主机的自制 HTTP 文件管理器。通过同一局域网内的任意浏览器（包括 PS5 自带浏览器）即可浏览、编辑、上传、下载并解压 ZIP / RAR / 7z 压缩包——单个自包含 ELF 载荷，无外部服务、无遥测上报。

**版本：** v1.9.3M · **标题 ID：** `FMGR88888` · **许可证：** GPLv3+ · **目标平台：** `x86_64-sie-ps5`

---

## 概述

一个在已越狱 PS5 上运行的 HTTP 文件管理器载荷。从局域网内任意浏览器（含 PS5 浏览器本身）打开 `http://<PS5_IP>:8888/`，即可管理外接 USB 存储与用户分区的文件。设计初衷是安全地把游戏 dump 文件夹从 USB 拷贝到内置存储，但它同时也支持常规文件管理、原地文本编辑、PKG 预览/安装、图片预览，以及内置防 zip 炸弹保护的解压功能。

同一套源码树可构建出供开发用的 Linux 二进制，以及供部署的 PS5 载荷 ELF——见下方 `make linux`。

> **第一次用、不想看技术细节？** 直接看
> [《新手使用说明》](docs/USER-GUIDE-zh-CN.md)：怎么装、怎么传文件、怎么解压（含带密码与分卷的包）、
> 界面上每句话是什么意思，以及与上游原版的差别——全部用大白话写。

## v1.9.3M 新增内容

**加密归档现在可以端到端解压——ZIP（两种方案）、RAR，以及带头加密的 7z 都已打通。**

此前所有加密归档都会被提前拒绝，尽管密码输入框、失败提示与 `extract_password` 文案从 v1.9 起就已就位。真正的缺口在引擎侧，而不在 UI：

- **ZIP**：vendored 的 minizip-ng 在裁剪时把 crypto 后端一起裁掉了，于是（未被改动的）`mz_zip.c` 里那些 `-DHAVE_WZAES` / `-DHAVE_PKCRYPT` 分支没有实现可调。
- **RAR**：rarlab UnRAR 本身能解密，但 `RARSetPassword` 从未被调用。
- **7z**：`-mhe=on` 把文件名与 folder 表放进了加密头，归档连列出都做不到。

三者现在都已接线。密码缺失或错误会统一报为 `extract_password`（引擎层即 `ZIPX_ERR_PASSWORD`），也就是任务浮层已有的密码提示所响应的那个错误码。

### 变更

- **版本号加改版标记：`v1.9.3` → `v1.9.3M`。** 上游 owendswang 的发布版是纯 `vX.Y.Z`，因此这个 `M`（Modified，改版）就是「上游原版还是本仓改版」的判据。它是 `VERSION_TAG` 的一部分，所以 `/api/version`、PS5 启动通知、stdout 横幅、网页右下角**与 ELF 文件名**会一次性全部带上；网页右下角另加悬浮提示（`versionTooltip`，中英各一）解释这个字母的含义，免得没读过发行说明的人无从判断。产物名随之改变，也顺带让本仓产物再不可能与上游同版本号的资产同名相撞。

### 新增

- **加密 ZIP** —— 传统 PKWARE（「ZipCrypto」，即 `zip -e` 写出的格式）与 WinZip AES-128/192/256（压缩方法 `99` + `0x9901` 扩展字段，即 `7z -mem=AES256` 写出的格式），stored 与 deflated 条目均支持。
- **加密 RAR** —— `-p` 内容加密与 `-hp` 头加密。`RARSetPassword` 现在在 `RAROpenArchiveEx` 之后、首次 `RARReadHeaderEx` 之前调用，这正是 unrar 解密 RAR5 头所需的顺序。
- `/api/extract` 的 `password=` 现在对两个引擎都能真正走到解密路径。空值或缺失视为「无密码」，因此表单原值可以直接透传。
- `third_party/minizip-ng/src/mz_crypt_wfm.c` —— 为裁剪后的 minizip-ng 提供的本地 crypto 后端：SHA-1、HMAC-SHA1、AES-128/192/256；S-box 与 GF(2^8) 表在首次使用时推导，因此二进制不新增任何 `.rodata` 查表。PBKDF2 复用 vendored 的 `mz_crypt.c`；随机数直接读 `/dev/urandom`（不再是 `mz_os_rand()`），从而把 `rand`/`srand` 排除在导入表之外。以下文件按上游 4.2.2 原样恢复：`mz_strm_wzaes.{c,h}`、`mz_strm_pkcrypt.{c,h}`。
- **前端在密码失败后可直接重试**：`extract_password` 失败不再只是弹一个错误框，而是弹出密码输入框并按原参数（冲突策略、大文件选配）重新发起同一次解压，最多重试 3 次；取消或留空则回落到原有的失败提示。
- **加密 7z 头（`-mhe=on`）现在可以打开。** 这是最后一个已知的格式缺口：`-mhe=on` 时文件名、folder 表**与每个条目的尺寸**全都在加密头里，因此 vendored SDK 在能列出任何条目之前就以 `SZ_ERROR_UNSUPPORTED` 退出。新模块 `src/sevenz_header.c` 读出该头部记录，用它自己的那一个 folder 走项目自研的 7zAES 路径（`src/sevenz_chain.c`）解码，然后交给 SDK 一个虚拟流——把加密头所在区域替换成明文。SDK 于是照常解析它一向解析的那个归档，磁盘上的文件完全不被改动；头部只是被*压缩*（`-mhc=on`，默认）的归档完全不受影响；密码错误则与其它加密归档一样返回 `extract_password`。
- `tests/make-zip-enc-fixtures.bat`，以及 `tests/fixtures-real/` 下的三个真实 fixture（`enc-zipcrypto.zip`、`enc-aes256.zip`、`enc-aes256-store.zip`，密码 `secret123`）。

### 修复

- **编译选项变化现在会使目标文件失效。** `make` 察觉不到编译选项变化，因此加上 `-DHAVE_WZAES -DHAVE_PKCRYPT` 后旧的 `mz_zip.o` / `mz_crypt.o` 原样保留——又因为此时已没有任何代码引用新流，`--gc-sections` 会在链接「成功」的同时把加密代码再次丢掉（本次改动的第一次构建产物与已发布的 release 逐字节相同）。Makefile 现在把第三方编译选项集记录进 `ps5-obj/.third_party_cflags` / `linux-obj/.third_party_cflags`，只在真正变化时重编——这正是早年 `LzmaDec.o` 规则所规避的同一个陷阱，现已通用化。
- `ZIPX_ERR_UNSUPPORTED` 不再涵盖加密，现在仅表示「多卷或不受支持的压缩方法」。
- 顺带把 `tests/test_sevenz_extract.c` 里一处会导致截断告警的 `snprintf` 缓冲区调足。

### 测试

- `tests/test_zip_extract.c` 对每个加密 fixture 跑四种情况（无密码 → `PASSWORD`、空密码 → `PASSWORD`、错密码 → `PASSWORD`、正确密码 → `ZIPX_OK` 并逐字节校验内容），另加一项证明「提供密码后限额依旧生效」。
- `tests/test_rar_extract.c` 对 `enc-v6.rar` 做同样的四种情况验证，包括失败路径绝不发布任何文件。
- `tests/test_sevenz_extract.c` 对 `aeshe.7z` 跑三种情况：无密码 → `ZIPX_ERR_PASSWORD`、错密码 → `ZIPX_ERR_PASSWORD`、正确密码 → 成功且逐字节一致，并证明失败后不留下 staging 目录。
- 前端重试流程有一份无头检查（`.build/ui_retry_test.mjs`，把 `assets/main.js` 载入桩 DOM）：**40 项检查**，覆盖参数记忆、重试上限、取消与空密码的回落，以及「非 ASCII 目录下必须仍然弹出密码框」的回归用例。另有一份 `.build/ui_upload_menu_test.mjs`（**40 项检查**）盯标记侧：i18n 键在两份语言文件里都存在、上传菜单接对了回调、用到的 class 确实有样式、菜单行高亮规则必须带面板作用域（否则会输给通用按钮规则而静默失效），以及**解压按钮不许被隐藏、只许被置灰**（顺带扫 `main.js` 里 117 个 `t("...")` 键是否双语齐全）。
- 主机端合计：**140 ZIP + 37 RAR = 177 项检查**，0 失败。
- 请求的字典超过本构建支持上限的 RAR 归档不再被误报成「单条目过大」：它有独立的 `extract_dict_too_large` 编码，报错文案同时给出归档需要的字典与构建支持的上限。构建行为**未变** —— 这类归档仍然被拒，因为放行意味着一次性分配整个字典窗口，而这正是 rarlab 自家 CLI 默认拒绝、16 GB 共享内存的主机也承受不了的。
- 7z 套件：**27 项用例，0 失败**（`tests/run-sevenz-tests.sh`），且原先登记 `aeshe` 的 `KNOWN_GAPS` 列表现已**清空**——加密头 fixture 同时通过 folder 解码器与解压 façade 两条路径。
- 当前源码树构建产物 **903 448 B**，sha256 `8ca47d5aaca75085b32641300cce30fadb7df7749cb6b53d04f129bcecc286b7`，`e_machine` 为 `0x003e`；同一源码树构建两次逐字节一致。产物内已确认包含新的前端代码（前端资源是 gzip 内嵌的，需先解压才能在二进制里检索到）。段数仍为 20，**动态符号零新增**。加密归档那批改动净增 5 712 字节正文（`.text` +4 880、`.rodata` +640、`.eh_frame*` +192）；加 `M` 标记再让 `.rodata` 涨 0x100（256 B），修正 `err_extract_unsupported` 文案再涨 0x40（64 B），上传菜单再涨 0x980（2 432 B），第一轮修复的文案与 CSS 再涨 0x100（256 B），菜单行高亮的收敛规则再涨 0x180（384 B），解压按钮常显（去掉 `hidden`、换短标签、三条禁用理由、`.extract-action:disabled`）再涨 0x240（576 B），**其余段尺寸一个都没变**，因此文件总尺寸仍是 903 448 B。**尺寸没变不等于内容没变** —— 判断只看 `readelf -SW` 的段尺寸。

### 已完成

- 已在真机上做端到端验证（加密包上传即解压弹口令、菜单高亮、解压按钮灰/亮等全部通过）。

> 本节描述的是 **v1.9.3M**，该版本**已发布**：
> <https://github.com/LisherSong/ps5-web-file-manager/releases/tag/v1.9.3M>。
> 上一个发布版 `v1.9.2` 的二进制**不含**上述内容。

## v1.9.2 与 v1.9.1 新增内容

> **v1.9.2 与 v1.9.1 的功能完全相同，只换了内嵌版本号。** 原因是原先的 `v1.9.1` tag 指在产出发布二进制的提交**之前 4 个提交**，tag 与产物对不上（clone 该 tag 无法重建出发布的那份 ELF）；v1.9.2 重新从产出该二进制的提交上打，使 tag = 源码 = 二进制。

- **7z 解压引擎**（`src/sevenz_extract.{c,h}`）：自研解码子集 + 拉式 codec 链（`src/sevenz_chain.c`，覆盖 LZMA2 / BCJ2 等），由 `src/extract.c` 按扩展名分派，与 ZIP / RAR 共用同一套三阶段模型与限额档位。`.7z` 文件在文件列表中同样带「解压」按钮。
- **7zAES 内容解密**（AES-256-CBC）：引擎可解密带密码的 7z 内容，密码经 `/api/extract` 的 `password=` 传入；解压 7z 时前端会提前询问密码。ZIP / RAR 的加密当时尚未打通（引擎侧缺口，见顶部「v1.9.3M 新增内容」），v1.9.1 时对它们仍会报 `extract_unsupported`。
- **7z 分卷**：`.7z.001` / `.z01` 等链式分卷由 `src/sevenz_volstream.c` 按名拼接，打开首个分卷即可。
- **性能三项**（纯解码提速，不影响功能面）：
  - SDK 汇编 LZMA 解码器（`Asm/x86/LzmaDecOpt.asm` + jwasm，无 jwasm 自动回退纯 C）≈ 1.26×。
  - 纯 LZMA2 文件夹多线程解码（`src/sevenz_mt.c` + `Lzma2DecMt`，8 线程）≈ 1.37×。
  - 移除 ZIP 逐条目 fsync，减少 staging 重命名前的写盘开销。
- **当时唯一缺口**：7z `-mhe=on` 加密头（独立单元，读取需自研头解析器），其余 7z 特性均已支持。已在顶部「v1.9.3M 新增内容」中补上。

## v1.9 新增内容

- **RAR 引擎替换为官方 rarlab UnRAR 7.20.1**（`third_party/unrar7/`，取代 dmc_unrar）。这正是让 RAR 解压在真实文件上可用的一步：dmc_unrar 无法解码 **WinRAR 6.x/7.x** 写出的归档（RAR5「v6」压缩），也不支持多卷；两者现在都能工作。
- **RAR5「v6」归档可解压**（v1.8 时代在 WinRAR 6/7 文件上报「归档损坏」的问题已消失）。
- **多卷 RAR**（`.part01.rar` 链）：当完整卷集与被打开的卷放在同一目录时，unrar 按文件名拼接各部分。
- 引擎可解密加密 RAR（`RARSetPassword`）——发 v1.9 时密码 UI / API 接线尚未完成，加密归档会被拒绝；该接线已在顶部「v1.9.3M 新增内容」中补齐。
- 主机测试现用真实归档（v6 / 加密 / 3 卷 fixture，提交于 `tests/fixtures-real/`）：**70 ZIP + 24 RAR = 94 项检查**。

## v1.8 新增内容

- **单卷 RAR 解压**，基于内置的 FLOSS 库 [`dmc_unrar`](https://github.com/DrMcCoy/dmc_unrar)（GPL-2.0-or-later）。支持 RAR 1.5、2.x、3.x、4.x、5.x 归档。`.rar` 文件出现在文件列表中且「解压」按钮可用；`.part02+.rar` 子卷上的按钮置灰，提示「请选择主卷」——v1.8 无法拼接多卷 RAR（见下方 [RAR 解压](#rar-解压) 章节）。
- 新引擎 `src/rar_extract.c` 与既有 `src/zip_extract.c` 之间**共享解压协议**：相同的 `zipx_status_t` 状态码、相同的 `zipx_limits_t` 档位（默认 / `large=1`）、相同的三阶段模型（`scan → extract → publish → cleanup`）、相同的 staging 目录布局、相同的冲突策略、相同的错误映射到任务 UI。`src/extract.c` 中的分派器只是一个微小的 `ends_with_ci(…)` 判断。
- **14 个新增主机端 C 测试**（`tests/test_rar_extract.c`）接入现有 `tests/run-tests.sh`。覆盖：格式分派、每个影响 RAR 用户的 `DMC_UNRAR_*` 错误码翻译、限额档位交接。主机检查总数：**69 ZIP + 14 RAR = 83**。
- **文档**：[`CHANGELOG.md`](./CHANGELOG.md)、[`docs/UPGRADE-v1.8-rar-support.md`](./docs/UPGRADE-v1.8-rar-support.md)，以及 `third_party/unrar7/VENDORED.md` 中的 vendoring 决策树（v1.8 时为 `third_party/unrar/VENDORED.md`）。

## v1.8.1 新增内容

- **放宽默认 ZIP 限额**（配合 v1.7 的大档案档位）。v1.7 默认单条目上限为 64 GiB，对典型 PS5 系统备份 ZIP（200–300 GiB）过于激进。v1.8.1 将默认档位提高到 **总量 1 TiB / 单条目 256 GiB / 500:1 比率**，保留 `large=1` 选项为 2 TiB / 1 TiB / 1000:1。前端阈值从 60 GiB 提升到 240 GiB，使常见系统备份归档不再触发确认提示。
- RAR 解压继承这些新默认值（`rar_extract.c` 直接从引擎透传 `c->limits`，无需改引擎）。
- 理由：真正的防 zip 炸弹防线是 `check_space()`（staging 前基于 statvfs 的真实磁盘空间检查）+ `max_ratio`（声明的压缩比上限）。尺寸上限只是 UX 护栏，而非安全边界。

## v1.8.2 新增内容

- **再次放宽默认 ZIP 限额**，针对 3A 游戏单文件场景。v1.8.1 仍会静默拒绝归档内单个约 300 GiB 的未压缩文件（默认扫描在请求到达前端确认提示之前就返回 `ZIPX_ERR_LIMIT_FILE_SIZE`）。v1.8.2 将默认档位提高到 **总量 2 TiB / 单条目 512 GiB / 500:1 比率**，`large=1` 选件提到 4 TiB / 1 TiB / 1000:1。前端阈值从 240 GiB 提升到 480 GiB。
- **两个 PS5 专属构建修复**，在交叉编译 PS5 目标时发现。主机端测试套件（`tests/run-tests.sh`）曾静默接受二者，因为它链接相同源码但使用 gcc 而非 clang 18，且包含路径不同：
  - `Makefile` CFLAGS：加入 `-Ithird_party/unrar`，使 `src/rar_extract.c` 能找到项目自有的 `dmc_unrar_api.h` 门面头文件。
  - `src/extract.c`：把 `extract_progress()` 定义移到 `extract_dispatch()` 之前，避免被 `-Werror=implicit-function-declaration` 标记（PS5 SDK 的 clang 18 比测试用的主机 gcc 更严格）。
- v1.8.2 发布产物：`web-file-mgr.elf` —— 509 704 字节，sha256 `1b2c3d68b35e32737105f17d14a80a3c159ceca0cabd274ee168cbcd81906f65`，ELF 64 位小端，e_machine `0x003e`（x86_64-sie-ps5）。
- 测试：**84 项主机端检查**（70 ZIP + 14 RAR），0 失败。PS5 交叉编译端到端成功。

## v1.7 新增内容

- **ZIP 大文件档位**（通过在 `/api/extract` 传入新的 `large=1` 参数选配启用）：放宽的限额为 **总量 2 TiB** / **单条目 1 TiB** / **1000:1 压缩比**。当磁盘上归档大于 **60 GiB** 时前端会提示确认；仅当用户明确同意时服务器才启用该档位。
- **更严格的默认 ZIP 档位**保持安全：**总量 1 TiB** / **单条目 256 GiB** / **500:1 比率**。一个 4 MiB 压缩包解压到 800 GiB 仍会在打开任何输出文件之前被拒绝。
- **69 项主机端 C 测试**（`tests/run-tests.sh`）现已覆盖路径穿越、ZIP64、加密拒绝、压缩比、冲突策略与新增大文件档位（`tests/test_zip_extract.c`）。
- 更早的细化——见 v1.6 以来的 `git log`。

## 截图

<p>
  <a href="docs/screenshots/20260617_231827.376.jpg" target="_blank"><img src="docs/screenshots/20260617_231827.376.jpg" width="31%" alt="PS5 网页文件管理器截图 1"></a>
  <a href="docs/screenshots/20260619_131432.399.jpg" target="_blank"><img src="docs/screenshots/20260619_131432.399.jpg" width="31%" alt="PS5 网页文件管理器截图 2"></a>
  <a href="docs/screenshots/20260617_232348.855.jpg" target="_blank"><img src="docs/screenshots/20260617_232348.855.jpg" width="31%" alt="PS5 网页文件管理器截图 3"></a>
  <a href="docs/screenshots/20260619_131811.644.jpg" target="_blank"><img src="docs/screenshots/20260619_131811.644.jpg" width="31%" alt="PS5 网页文件管理器截图 4"></a>
  <a href="docs/screenshots/20260619_131535.239.jpg" target="_blank"><img src="docs/screenshots/20260619_131535.239.jpg" width="31%" alt="PS5 网页文件管理器截图 5"></a>
  <a href="docs/screenshots/20260620_232728.533.jpg" target="_blank"><img src="docs/screenshots/20260620_232728.533.jpg" width="31%" alt="PS5 网页文件管理器截图 6"></a>
</p>

## 功能

- **浏览** —— 列出文件与文件夹；按名称、类型、大小、修改时间或权限排序。上次排序方式持久化在 `localStorage`。
- **权限** —— 用复选框切换读/写/执行，或粘贴经过校验的四位八进制模式。
- **操作** —— 复制、移动、删除（递归、无回收站）、重命名、创建文件与文件夹。
- **编辑器** —— 针对 ≤ 1 MiB 的文件，跨精选扩展名列表的原地 UTF-8 文本编辑器：`.txt .json .xml .ini .cfg .conf .md .log .lua .js .css .html .htm .c .h .cpp .hpp .sh .csv .yaml .yml .shn`。
- **多选** —— 一次性复制、移动、删除或打包下载多个项目。
- **上传** —— 从局域网内任意设备上传单文件或文件夹树（在 PS5 浏览器中隐藏）。原子化的临时文件 + 重命名。
- **下载** —— 单文件以原始字节下载，或文件夹/多选以流式 `.tar` 下载。在 PS5 浏览器中隐藏。
- **任务** —— 全屏覆盖层，带延迟显示、实时进度、吞吐率、ETA、取消，以及浏览器中途关闭重开后的恢复能力。
- **归档解压** —— ZIP、RAR、7z 三种引擎，均带防 zip 炸弹 / 路径穿越 / 压缩比保护。ZIP 覆盖 stored / deflated / ZIP64 以及**加密**条目（ZipCrypto 与 WinZip AES-128/192/256）；RAR 覆盖 RAR4 + RAR5（含 WinRAR 6/7「v6」）、多卷，以及 `-p` / `-hp` 加密；7z 覆盖 LZMA / LZMA2 / PPMd、Delta 与 BCJ2、`.7z.001` 分卷、7zAES 与 `-mhe=on` 加密头。详见下方 [ZIP 解压](#zip-解压)、[RAR 解压](#rar-解压)、[7z 解压](#7z-解压)。
- **加密归档重试** —— 解压遇到加密归档时，会弹出密码输入框并按原参数自动重试（最多 3 次）；也可以在解压 7z 时提前输入密码以免白跑一次扫描。
- **PKG** —— 安装并预览 `.pkg` 文件。
- **图片** —— 预览 `.png .jpg .jpeg .gif .bmp .webp`。
- **本地化** —— 英文 + 简体中文，根据 `navigator.languages` 自动选择。
- **移动端友好** —— 响应式布局，工具栏自动换行，文件列表可横向滚动。

## 快速上手

1. **构建** ELF：

   ```sh
   export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk   # 见「构建」章节的 SDK 配置
   make
   ```
2. **发送** 载荷到 PS5（默认 ELF 加载器端口 `9021`）：

   ```sh
   nc -q0 "$PS5_HOST" 9021 < web-file-mgr.elf
   ```
3. **读取** PS5 屏幕上的通知——它会打印实际监听端口（默认 `8888`）。
4. 在**同一局域网**内的任意浏览器中打开 `http://<PS5_IP>:<port>/`——PS5 浏览器也可以。
5. 首次运行时，载荷还会写入一个 **Media** 分类的主屏启动器；已有的启动器文件不会被覆盖。

## 构建

需要 [ps5-payload-dev/sdk](https://github.com/ps5-payload-dev/sdk#quick-start)：

```sh
export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk
```

本项目链接 `libmicrohttpd`。`make` 在构建前会检查它，缺失时自动运行安装器：

```sh
make
```

若构建主机无网络访问，可提前放入 libmicrohttpd 源码包并手动运行安装器：

```sh
LIBMICROHTTPD_TARBALL=/path/to/libmicrohttpd-1.0.1.tar.gz \
  ./install-libmicrohttpd.sh
make
```

输出：

```text
web-file-mgr.elf   （约数百 KiB，v1.9.1 含 unrar7 + 7z 后更大；x86_64-sie-ps5）
```

若只想做纯 UI/JS 开发而不需要 PS5 工具链：

```sh
make linux
./web-file-mgr-linux
```

Linux 构建**不包含** PS5 主屏启动器安装器。

## 使用

在 PS5 上启动一个 ELF 加载器（端口 `9021` 常见）。发送载荷：

```sh
export PS5_HOST=ps5_ip_address
nc -q0 "$PS5_HOST" 9021 < web-file-mgr.elf
```

载荷启动后，PS5 通知会显示应用名、版本与实际监听端口。打开它打印的 URL，例如：

```text
http://${PS5_IP_ADDRESS}:8888/
```

若载荷不得不回退到其它端口（如 `8889`），请以通知显示的端口为准——URL 并未硬编码。

首次启动时，载荷会在需要时于 Media 分类安装一个 `PS5 Web File Manager` 快捷方式。已有的启动器文件会被保留；只补写缺失的文件。

## ZIP 解压

支持普通 ZIP 与加密 ZIP——stored / deflated / ZIP64，传统 PKWARE（ZipCrypto）与 WinZip AES-128/192/256 两种加密方案。引擎是一个独立的三阶段模块（`scan → extract → publish → cleanup`），位于 `src/zip_extract.{c,h}`，配有独立的主机端 C 测试套件。每个条目先写入 staging 目录（`*.wfm-part-*`），再原子重命名到目标位置。解压路径里**刻意不做逐条目 `fsync`**——整条流水线是「不 sync、只 rename」，因为 publish 只是 rename、也没有续解功能需要保护（8000 文件档实测 ≥14×，见 `docs/EXTRACTION-PERF.md`）。归档中途任何失败都会回滚部分改动；取消与致命错误总会清理 staging。

### 限额

| 限额 | 默认档位 | 大档案档位（`ZIPX_LIMITS_LARGE`） |
|---|---|---|
| `max_entries` | 200 000 | 500 000 |
| `max_total_bytes`（未压缩） | 2 TiB | 4 TiB |
| `max_file_bytes`（单条目） | 512 GiB | 1 TiB |
| `max_ratio`（未压缩 / 压缩） | 500 : 1 | 1000 : 1 |
| `max_depth`（文件夹嵌套） | 32 | 32 |
| `max_name_len` / `max_path_len` | 255 / 1024 | 255 / 1024 |

**默认档位**出厂即安全：一个解压到 800 GiB 的 4 MiB 压缩块会在打开任何输出文件之前被拒绝。**大档案档位**仅在请求携带 `large=1` 时才启用——当磁盘上归档大于 `LARGE_FILE_THRESHOLD_BYTES`（默认 480 GiB；可在 `assets/main.js` 配置）时，解压对话框会自动提示用户。确认提示即为用户的明确选配；服务器自身不会额外记录任何内容。

### 安全检查

引擎拒绝解压以下归档：

- 路径穿越（`..` 段、绝对 POSIX 路径、Windows 盘符）。
- 符号链接、设备、FIFO、套接字（`ZIPX_ERR_SPECIAL`）。
- 同一归档内的重复条目或目录/文件名冲突。
- 解压后尺寸、条目数、嵌套深度、名称长度或压缩比突破当前档位。

加密条目**不再**属于拒绝项：密码通过 `/api/extract` 的 `password=` 传入，缺失或错误时返回 `zipx` 层的 `ZIPX_ERR_PASSWORD`（前端对应 `err_extract_password`），由界面提示后重试。scan 阶段对加密条目同样生效——限额不会因为提供了密码而被跳过。

### 冲突策略

通过 `/api/extract` 上的 `conflict=` 传入：

- `fail`（默认）—— 拒绝覆盖任何已存在的目标。
- `overwrite` —— 替换已存在文件；合并进已存在文件夹。
- `merge` —— 保留已存在文件，新增其余文件。

### 调整阈值

480 GiB 的前端阈值位于 `assets/main.js`：

```js
const LARGE_FILE_THRESHOLD_BYTES = 480 * 1024 * 1024 * 1024;
```

设为 `Infinity` 可静音提示，调低则更保守，或干脆删掉该调用——无论阈值如何，服务器始终遵循 `large=1`。

## RAR 解压

RAR 解压引擎（`src/rar_extract.{c,h}`）由 **官方 rarlab UnRAR 源码** 支撑（`third_party/unrar7/`，版本 7.20.1，编译为静态库并通过其 C 兼容的 DLL API 驱动）。扩展名为 `.rar` 的文件与 `.zip` 文件一样拥有**解压**按钮；引擎由 `src/extract.c` 按扩展名分派。

> v1.9 替换了 v1.8 的引擎（dmc_unrar 1.7.0）。dmc_unrar 无法解码 WinRAR 6.x/7.x 写出的归档（RAR5「v6」压缩）且不支持多卷；unrar 原生支持两者。

### 支持范围

| 格式 | 支持 | 备注 |
|---|---|---|
| RAR 1.5 → 4.x（含 2.9 / 3.6 / 4.0） | ✅ | |
| RAR 5.0 及 **5.0「v6」**（WinRAR 6.x / 7.x） | ✅ | v1.9 的触发点 |
| Solid 块、最大 1 GiB 字典 | ✅ | |
| PPMd 解压（RAR 3.0+） | ✅ | |
| **多卷**（`.part01.rar` + `.part02.rar` + …） | ✅ | 当完整卷集与被打开的卷同处一目录时，unrar 按名拼接。选择首个卷（`name.part1.rar` / `name.part01.rar`）；非首卷在 UI 中仍置灰并给出提示。 |
| **加密 RAR** | ✅ | `-p` 内容加密与 `-hp` 头加密均可。密码经 `/api/extract` 的 `password=` 传入引擎（`RARSetPassword` 在 `RAROpenArchiveEx` 之后、首次 `RARReadHeaderEx` 之前调用）；缺失或错误返回 `ZIPX_ERR_PASSWORD`，界面提示后重试。 |
| 符号链接 / FIFO / 套接字 / 设备 | ❌ | 以 `ZIPX_ERR_SPECIAL` 拒绝（与 ZIP 行为一致） |
| RAR 1.3（1.4 之前） | ❌ | 被 unrar 上游拒绝 |

当某归档被拒绝时，用户会收到 `extract_unsupported` 失败，文件名作为详情参数。前端已用典型的双语重试指引显示该错误。

### 限额

RAR 引擎原样复用 ZIP 的限额表——其上并无额外的 RAR 档位表。默认值与 `large=1` 选配完全相同：

| 限额 | 默认档位 | 大档案档位（`large=1`） |
|---|---|---|
| `max_entries` | 200 000 | 500 000 |
| `max_total_bytes`（未压缩） | 2 TiB | 4 TiB |
| `max_file_bytes`（单条目） | 512 GiB | 1 TiB |
| `max_ratio`（未压缩 / 压缩） | 500 : 1 | 1000 : 1 |
| `max_depth`（文件夹嵌套） | 32 | 32 |
| `max_name_len` / `max_path_len` | 255 / 1024 | 255 / 1024 |

大档案档位的 RAR 解压使用与 ZIP 相同的 `LARGE_FILE_THRESHOLD_BYTES`（480 GiB）提示——前端对 `.rar` 与 `.zip` 的提示处理相同，且服务器仅在请求携带 `large=1`（选配）时才启用大限额。

### 安全检查

RAR 引擎应用与 ZIP 引擎相同的检查——复用 `zipx_status_t` 状态码，因此任务 UI 的 `err_extract_unsafe_name`、`err_extract_too_deep`、`err_extract_ratio` 等会一致触发：

- 路径穿越（`..` 段、绝对 POSIX 路径、Windows 盘符、`\` 在 `Rar!\x1a\x07…` 头之后被视为路径分隔符等）。
- 符号链接、FIFO、套接字、设备。
- 归档内重复条目或目录/文件名冲突。
- 归档尺寸、条目数、深度、名称长度或压缩比突破当前档位。

### Vendoring 与许可

`third_party/unrar7/` 是官方 **rarlab UnRAR 源码**（7.20.1）的逐字副本，由 [`opello/unrar`](https://github.com/opello/unrar) 在提交 `97e1780` 处镜像。它依 **UnRAR 免费软件许可** 分发（见 `third_party/unrar7/license.txt`）：可于任何软件中用于处理 RAR 归档，但不得用于开发 RAR 兼容的*归档器*或重新实现 RAR 压缩算法。项目自有的门面 `third_party/unrar7/unrar_c_api.h` 携带项目自身许可。

> v1.8 引擎 `third_party/unrar/dmc_unrar.c`（DrMcCoy/dmc_unrar 1.7.0，GPL-2.0-or-later）已在 v1.9 移除；其声明留存于 git 历史。

### 加密 RAR

密码通道现已完整接通：`/api/extract` 的 `password=` 会被透传给引擎，并在 `RAROpenArchiveEx` 之后、首次 `RARReadHeaderEx` 之前通过 `RARSetPassword` 交给 unrar（这个顺序是解密 `-hp` 加密头的前提）。密码缺失或错误一律返回 `ZIPX_ERR_PASSWORD`（前端 `err_extract_password`），界面据此弹出密码框并按原参数重试，最多 3 次。

## 7z 解压

7z 解压引擎（`src/sevenz_extract.{c,h}`）基于 SDK 解码子集（LZMA2 / LZMA / BCJ2 等）加上项目自研的拉式 codec 链（`src/sevenz_chain.c`，位于 `src/sevenz_chain.h`）。扩展名为 `.7z` 的文件与 ZIP / RAR 一样拥有**解压**按钮；引擎由 `src/extract.c` 按扩展名分派，并复用同一套三阶段模型、限额档位与冲突策略。

> v1.9.1 新增。SDK 自带的 `SzArEx` 路径仅覆盖 4 个 coder 的文件夹，不足以装下 BCJ2 的 5 coder；本项目改为自研 folder 解析 + 拉式 codec 链，从而原生支持 BCJ2 与多 coder 组合。

关于头部：7-Zip 把归档头放在文件末尾，并在头部变大时把它压缩（`-mhc=on`，默认行为），所以头部区域通常以一条 `k7zIdEncodedHeader` 记录开头、描述一个 folder。`-mhe=on` 时那个 folder **也被加密**，而它装着文件名、folder 表与每个条目的尺寸，于是 vendored SDK 在能列出任何条目之前就对整个归档放弃。`src/sevenz_header.c` 负责这种情况：读出该记录，用与内容完全相同的那条 7zAES 路径解出它的 folder，再把一个「头部区域是明文」的虚拟流交给 SDK——磁盘上的归档从不被写入，仅仅被*压缩*过头的归档也完全不受影响。

### 支持范围

| 格式 | 支持 | 备注 |
|---|---|---|
| LZMA2 / LZMA（含 ZIP64 式大尺寸） | ✅ | 单 coder 纯 LZMA2 走多线程解码（`src/sevenz_mt.c`，8 线程） |
| BCJ2（x86 反汇编后处理） | ✅ | 经自研拉式链；SDK `SzArEx` 装不下 5 coder 时由本项目承载 |
| 多 coder 组合文件夹 | ✅ | 自研 `sevenz_chain.c` 解析 |
| **分卷**（`.7z.001` / `.z01` 链） | ✅ | `src/sevenz_volstream.c` 按名拼接；打开首个分卷 |
| **内容加密**（7zAES，AES-256-CBC） | ✅ | 引擎可解密；解压 7z 时前端会**提前**询问密码（避免为无密码归档白跑一次扫描 + folder 解析），密码经 `password=` 传给引擎，缺失或错误返回 `ZIPX_ERR_PASSWORD` 并可重试 |
| **`-mhe=on` 加密头** | ✅ | `src/sevenz_header.c` 自行解码该头部记录（复用同一条 7zAES 路径），再把一个携带明文的虚拟流交给 SDK；密码错误返回 `ZIPX_ERR_PASSWORD`，与其它加密归档一样由提示重试 |
| `-mhc=off`（未压缩头） | ✅ | 明文头一向可读；现在按字节探测后完全不做干预 |

当某归档被拒绝时，用户同样收到 `extract_unsupported` 失败，UI 显示双语重试指引。

### 限额

7z 引擎复用与 ZIP / RAR 完全相同的限额表；默认档位与 `large=1` 选配一致（见 [ZIP 解压 → 限额](#限额)）。

### 安全检查

7z 引擎复用相同的 `zipx_status_t` 错误码与检查集合：路径穿越、特殊文件、重复条目/名冲突、以及突破当前档位的尺寸/条目数/深度/名称长度/压缩比。coder 的 `out_size` 取自 `coder_unpack_sizes[index]`（而非文件夹尺寸），`SzArEx` 失败时重置 `blockIndex` 以避免伪 CRC。

## 校验

`make` 之后，对生成的 ELF 做健全性检查：

```sh
ls -la web-file-mgr.elf                            # v1.9.1 因含 unrar7 + 7z 体积更大；v1.8.3 约 509 KiB
sha256sum web-file-mgr.elf                         # 把摘要记录进你的发布说明
file  web-file-mgr.elf                             # 期望 "ELF 64-bit LSB pie executable, x86-64"
od -An -tx1 -N20 web-file-mgr.elf | head -2        # 魔数 7f45 4c46 0201 + e_machine 003e
```

`e_machine = 0x003e` 确认了 PS5 目标三元组 `x86_64-sie-ps5`。`e_type = 3`（`ET_DYN`）确认了 ELF 加载器期望的位置无关载荷。

## 测试

一套 POSIX / 主机端 C 测试套件覆盖 ZIP、RAR 与 7z 三个引擎，可在任意 Linux / macOS / MSYS shell 下、无需 PS5 SDK 运行：

```sh
cd tests && bash run-tests.sh          # ZIP + RAR 套件
bash run-sevenz-tests.sh               # 7z 套件（需 MinGW gcc 与 7-Zip 二进制）
```

输出为逐用例的 `check` 风格报告。当前 `main` 上为 **177 项检查**（140 ZIP + 37 RAR），0 失败；7z 套件另有 **27 项用例**，同样 0 失败。覆盖：

- ZIP 条目解析（stored + deflated + ZIP64）
- 路径穿越、绝对路径、反斜杠、Windows 盘符
- 符号链接、FIFO、坏 CRC、截断归档、非 ZIP 文件
- 限额：`entries`、`total_bytes`、`file_bytes`、`ratio`、`depth`、`name_len`
- 冲突策略：`fail` / `overwrite` / `merge`
- 每个阶段的取消
- **加密档案** —— `tests/fixtures-real/` 下的真实归档各跑四种情况（无密码 / 空密码 / 错密码 → `ZIPX_ERR_PASSWORD`；正确密码 → 成功并逐字节校验内容）：ZIP 侧覆盖 `enc-zipcrypto.zip`、`enc-aes256.zip`、`enc-aes256-store.zip`，RAR 侧覆盖 `enc-v6.rar`；另验证「提供密码后限额依旧生效」与「失败路径绝不发布文件」
- **大文件档位** —— `medium_bomb.zip`（比率 ≈ 238）在默认限额下被拒、在大档位下通过；降低后的大档位仍生效
- **RAR 引擎**（`tests/test_rar_extract.c`，37 项检查）—— 格式分派（改名的 ZIP / 垃圾数据均被拒）、每个可达引擎错误码的翻译、限额交接（`large=1` 原样传入 `rar_extract()`）、超过上限的字典路径，以及真实归档覆盖
- **7z 引擎**（`tests/test_sevenz_extract.c` + `tests/run-sevenz-tests.sh`）—— 真实 `.7z` fixture 逐字节比对、加密头（三种情况：无密码 / 错密码 / 正确密码）、各策略下的冲突、取消、限额、缺失目标父目录，以及失败时绝不发布且 staging 树被清理的保证

前端还有一份无头检查 `.build/ui_retry_test.mjs`（`node .build/ui_retry_test.mjs`）：把 `assets/main.js` 载入桩 DOM，验证加密失败后的密码重试流程——参数记忆、重试上限、取消与空密码的回落，共 27 项检查。它位于 `.build/`（gitignore 白名单之外），属于开发期验证脚本。

## 项目结构

```
.
├── Makefile                      # PS5 + Linux 构建（VERSION_TAG v1.9.3M）
├── install-libmicrohttpd.sh      # 一次性依赖安装器
├── gen-asset-module.py           # 将 assets/* 内联为 gzip 压缩的 C 数组
├── assets/                       # HTML / CSS / JS / 图标 / param.json
├── src/                          # C 载荷源码
│   ├── main.c  websrv.c  filemgr.c        # 入口、HTTP 前端、任务模型
│   ├── upload.c  download.c               # 流处理
│   ├── extract.c                          # /api/extract 分派器（ZIP + RAR + 7z）
│   ├── zip_extract.{c,h}  zipx_common.c   # ZIP 引擎
│   ├── zipx_volume.c  zipx_volstream.c    # ZIP 分卷探测 + 拼接流
│   ├── rar_extract.{c,h}                  # RAR 引擎（unrar7 后端）
│   ├── sevenz_extract.{c,h}               # 7z 引擎
│   ├── sevenz_chain.{c,h}                 # 7z 拉式 codec 链（BCJ2 等）
│   ├── sevenz_header.{c,h}                # 7z 头部读取 / `-mhe=on` 解密
│   ├── sevenz_mt.{c,h}                    # 7z 多线程 LZMA2 解码
│   ├── sevenz_volstream.{c,h}             # 7z 分卷流拼接
│   └── app_installer.c                    # PS5 Media 启动器安装器
├── third_party/                  # vendored：zlib、minizip-ng、unrar7、7z(SDK 子集)
│   ├── unrar7/                   # rarlab UnRAR 7.20.1，静态库 + C API 门面
│   ├── minizip-ng/               # ZIP 读取器
│   ├── zlib/                     # minizip-ng 的压缩后端
│   └── 7z/                       # LZMA SDK 解码子集
├── tests/                        # POSIX / 主机测试套件
│   ├── test_zip_extract.c
│   ├── test_rar_extract.c
│   ├── test_sevenz_extract.c     # 7z 用例驱动
│   ├── make_fixtures.py          # 重新生成测试 fixture
│   ├── run-tests.sh              # 一次性运行器（ZIP + RAR）
│   ├── run-sevenz-tests.sh       # 7z 运行器
│   ├── compat/                   # 小型 Win32 / MSYS 垫片
│   └── fixtures/ fixtures-7z/ fixtures-real/  # 生成的测试归档
├── docs/
│   ├── HANDOVER.md               # v1.8 时代的开发手册（历史存档，现行见根目录 HANDOVER.md）
│   ├── UPGRADE-v1.7-zip-large-file-profile.md
│   ├── UPGRADE-v1.8-rar-support.md
│   └── screenshots/             # README 截图
├── THIRD_PARTY_NOTICES           # 捆绑库署名
├── LICENSE                       # GPLv3+
└── README.md
```

## 备注

- 复制、移动、删除、上传、下载作为单个后台任务运行。一个任务运行时，其它文件操作会被拒绝。
- 删除是递归且永久的。没有回收站。
- 复制/移动任务可取消。单个文件的部分拷贝会被移除，但部分拷贝的文件夹会保留在原地，以避免在合并进已存在目标文件夹时误删既有文件。
- 上传任务可取消。尽可能移除部分上传的临时文件。
- 下载文件夹或多个选中项会生成 tar 流。tar 归档由载荷生成，不会先写入 PS5 存储。
- 若浏览器在载荷进程仍在运行时被关闭重开，UI 可恢复活动任务显示。
- 文本编辑仅限于上述精选扩展名列表。非 UTF-8 与超大文件会被拒绝。
- 文件名通过 Web API 以 UTF-8 传输。载荷也会保留挂载文件系统返回的遗留字节序名称，以便混合 USB 文件名编码仍能正确显示与操作。

## 常见问题

- **这是自制应用，不应故意修改系统进程或内核内存。** 若遇到内核崩溃（kernel panic），请确保使用较新的越狱方法与 ELF 加载器，或回退到你惯用的稳定方法。
- **P2JB 用户** —— 若此载荷触发内核崩溃，请避免在该环境下使用。当每次重试代价高昂时，稳定性比便利更重要。
- **准备阶段可能耗时较久** —— 当文件夹含大量文件时，它会累加文件夹大小并检查剩余空间，这有助于避免启动一个无法安全完成的复制 / 移动 / 上传 / 下载。
- **`err_extract_entry_too_large`** —— 默认归档上限为单条目 512 GiB / 500:1 比率（覆盖典型 3A 游戏归档中单个约 300 GiB 未压缩文件）。若超过默认，请确认大文件提示（磁盘上 > 480 GiB 的归档会出现），拆分归档，或直接向 API 传入 `large=1`。
- **`err_extract_unsupported`** —— 这个包本机读不了：既非 `.zip` / `.rar` / `.7z` 的文件；ZIP 条目用了 stored / deflated 之外的压缩方法；7z 用了不支持的 coder；分卷命名不被识别（RAR 分卷若叫 `x.rar.001`，需改名为 `x.part1.rar`、`x.part2.rar` ……）；或早于 RAR 1.4 的归档。**加密归档与多卷归档不属于这一类**——两者都支持。界面会在括号里附上后端原文，指明具体原因。
- **`err_extract_dict_too_large`** —— RAR 归档声明的压缩字典超过本构建支持的上限（4096 MiB），且 unrar 请求允许超额。报错文案会同时给出归档需要的尺寸与构建允许的尺寸。这是**刻意拒绝**：另一条路是一次性分配整个字典窗口，rarlab 自家 CLI 默认也会拒绝，16 GB 共享内存的主机更是承受不起。请在 PC 上用不超过 4 GiB 的字典重新压缩（`-md`），或在 PC 上解压。注意 RAR5 格式本身把这个字段卡在 4 GiB，所以这种情况只可能来自更新版 RAR7 头格式写出的归档。
- **`err_extract_password`** —— 归档已加密，而本次提交的密码缺失或错误。这也包括带加密头（`-mhe=on`）的 7z：文件名与条目尺寸都在头部里，头解密之前连条目列表都读不出来。ZIP / RAR（以及现在的 7z 加密头）在失败后会弹出密码框（取消或留空即放弃），可用正确密码按原参数重试，最多 3 次；解压 7z 时仍会提前询问一次密码。

## 署名

本项目 **fork 自 [owendswang/ps5-web-file-manager](https://github.com/owendswang/ps5-web-file-manager)**（GPL-3.0）。Web UI、任务模型与 PS5 打包方式均源自该项目；上游作者以 GPL-3.0 发布，是本衍生作品得以存在的前提。

**怎么区分上游原版与本仓改版：** 自 v1.9.3 起版本号带 `M` 后缀（`vX.Y.ZM`），*M* 即 *Modified*（改版）；上游 owendswang 的发布版是纯 `vX.Y.Z`。因此 `v1.9.3M` 只可能出自本仓，而这个字母同时出现在 ELF 文件名、PS5 启动通知、`/api/version` 与网页右下角。v1.9.3M 之前的发布早于该约定，保留原本的无后缀编号。

本项目另参考了以下项目构建：

- **[ps5-payload-dev/websrv](https://github.com/ps5-payload-dev/websrv)：** HTTP 服务器结构、静态资源内联思路、PS5 浏览器/websrv 行为与 PKG 安装函数。许可证：GPLv3+。
- **[ps5-payload-dev/ftpsrv](https://github.com/ps5-payload-dev/ftpsrv)：** PS5 载荷约定、主屏启动器/安装流程参考、进程处理风格与启动安装参考。许可证：GPLv3+。
- **[seregonwar/zftpd](https://github.com/seregonwar/zftpd)：** PS5 TCP socket 缓冲调优与高吞吐传输行为参考。许可证：MIT。
- **[itsPLK/ps5-payload-manager](https://github.com/itsPLK/ps5-payload-manager)：** 载荷构建行为。许可证：GPLv3。
- **[libmicrohttpd](https://ftp.gnu.org/gnu/libmicrohttpd/)：** 用作内嵌 HTTP 服务器库。由 GNU 以 LGPL 许可；本载荷以 SDK 提供的静态库链接它。
- **[ps5-payload-dev/sdk](https://github.com/ps5-payload-dev/sdk)：** 载荷构建基础。许可证：GPLv3+。
- **[etaHEN](https://github.com/etaHEN/etaHEN)：** 退出前用于返回 PS5 主屏的 ShellUI URI 导航。许可证：GPLv3。
- **[ezremote](https://github.com/cy33hc/ps5-ezremote-client)：** PKG 预览功能的参考出处。许可证：**GPL-2.0-only** —— 其源文件未声明 "or later"，因此**无法**与本项目的 GPL-3.0 代码组合。**未取其任何代码**：`src/pkg_info.c` 是独立的 C99 实现（它还负责 `.pkg` 条目表与 `param.json` 字段，而 ezremote 根本没有 `.pkg` 解析器；JSON 走的是 `src/json_util.c` 里自写的分词器，不是 json-c）。详见 `docs/REWRITE-FEASIBILITY.md` §2.2。
- **[zlib-ng/minizip-ng](https://github.com/zlib-ng/minizip-ng)：** `/api/extract` 端点使用的 ZIP 读取器。vendored 于 `third_party/minizip-ng/`。许可证：zlib。
- **[zlib](https://www.zlib.net/)：** minizip-ng 的压缩后端。vendored 于 `third_party/zlib/`。许可证：zlib。
- **[rarlab UnRAR (opello/unrar)](https://github.com/opello/unrar)：** v1.9 起 `/api/extract` 使用的 RAR 读取器（7.20.1）。vendored 于 `third_party/unrar7/`。许可证：UnRAR 免费软件许可。

## 许可证

本项目以 **GPLv3 或更高版本** 分发，与作为实现参考的 GPLv3+ 项目保持一致。见 [`LICENSE`](./LICENSE)。

第三方项目保留各自许可证。请勿在未保留相应许可证声明的情况下，将署名项目的资源或源码复制到其它发行版中。

若分发二进制，除本项目 GPL 许可外，还需遵守 `libmicrohttpd` 的 LGPL 条款。vendored 的 `zlib` 与 `minizip-ng` 源码以 zlib 许可分发；再分发用此特性构建的二进制时，保留 `third_party/zlib/LICENSE` 与 `third_party/minizip-ng/LICENSE` 中的版权声明。vendored 的 `unrar7`（RAR 引擎）依 UnRAR 免费软件许可分发；再分发用 v1.9 或更高版本构建的二进制时，保留 `third_party/unrar7/license.txt` 中的声明，且不得用其开发 RAR 兼容归档器或重新实现 RAR 压缩算法。

## 免责声明

非官方自制软件。仅在已越狱 PS5 主机上运行。使用风险自负——作者不对损坏、数据丢失、账号处罚或保修影响负责。请勿再分发 Sony 专有内容。依 GPLv3+，修改后的再分发必须公开其源码。
