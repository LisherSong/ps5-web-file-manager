<div align="right">
  <a href="README.md">English</a> · <a href="README.zh-CN.md">简体中文</a>
</div>

# PS5 网页文件管理器（PS5 Web File Manager）

<p align="center">
  <a href="https://github.com/LisherSong/ps5-web-file-manager/releases/latest"><img src="https://img.shields.io/github/v/release/LisherSong/ps5-web-file-manager" alt="最新发布版"></a>
  <a href="LICENSE"><img src="https://img.shields.io/github/license/LisherSong/ps5-web-file-manager?color=blue" alt="许可证"></a>
  <img src="https://img.shields.io/badge/target-x86__64--sie--ps5-blue" alt="目标平台：x86_64-sie-ps5">
  <a href="https://github.com/LisherSong/ps5-web-file-manager/releases"><img src="https://img.shields.io/github/downloads/LisherSong/ps5-web-file-manager/total?color=green" alt="总下载量"></a>
</p>

<p align="center">
  <a href="https://github.com/LisherSong/ps5-web-file-manager/releases/latest"><img src="https://img.shields.io/badge/%E4%B8%8B%E8%BD%BD-ELF%20%E8%BD%BD%E8%8D%B7-2ea44f?style=for-the-badge" alt="下载 ELF 载荷"></a>
  <a href="docs/USER-GUIDE-zh-CN.md"><img src="https://img.shields.io/badge/%E6%96%B0%E6%89%8B%E4%BD%BF%E7%94%A8%E8%AF%B4%E6%98%8E-%E4%B8%AD%E6%96%87-2563eb?style=for-the-badge" alt="新手使用说明"></a>
</p>

> 面向已越狱 PS5 主机的自制 HTTP 文件管理器。通过同一局域网内的任意浏览器即可浏览、编辑、上传、
> 下载并解压压缩包——单个自包含 ELF 载荷，不需要任何外部 helper 文件，不上报任何遥测。

**版本：** v1.9.3M · **标题 ID：** `FMGR88888` · **许可证：** GPLv3+ · **目标平台：** `x86_64-sie-ps5`

**下载：** [最新发布版](https://github.com/LisherSong/ps5-web-file-manager/releases/latest) · **第一次用？** [《新手使用说明》](docs/USER-GUIDE-zh-CN.md)

---

## 这是什么

一个单文件载荷 ELF，在已越狱的 PS5 上跑起一个 HTTP 文件管理器。把它发给主机的 ELF 加载器，主机会在
`8888` 端口启动 HTTP 服务（该端口被占用时自动往上找下一个空闲端口）。在局域网内任意浏览器（包括
PS5 自带浏览器）打开 `http://<PS5_IP>:8888/`，即可管理外接 USB 存储与用户分区上的文件。

它只为把一件事做安全、做快而写：**把游戏 dump 文件夹从 USB 拷进内置存储。** 其余能力——浏览、排序、
改权限、原地编辑文本、预览图片、安装 PKG、多选复制/移动/删除、上传与下载——都是为了让这件事在
实际操作中行得通。在这之上，本仓又加了 **ZIP / RAR / 7z 的原生解压**，并配上了上游那套 helper 路线
所没有的安全护栏（防压缩炸弹、防路径穿越、防写满磁盘）。

同一套源码树也能编出 Linux 二进制，因此整个前端界面不依赖主机、也不需要 PS5 SDK 就能开发：

```sh
make linux && ./web-file-mgr-linux-v1.9.3M
```

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

**文件与目录**

- **浏览与排序** —— 列出文件与文件夹；按名称、类型、大小、修改时间或权限排序。上次选的排序方式
  持久化在 `localStorage` 里。
- **权限** —— 在权限列用复选框切换读 / 写 / 执行，或粘贴一个经过校验的四位八进制模式。
- **复制与移动** —— 两步式「剪贴板」流程：先选中要处理的项，再浏览到目标目录粘贴（复制）或移入
  （移动）。覆盖文件与合并文件夹时都会弹出冲突确认。
- **重命名** —— 就地重命名选中的单个条目。
- **删除** —— 递归且永久，没有回收站。
- **新建** —— 新建文件夹与新建空文本文件。
- **多选** —— 一次性复制、移动、删除或打包下载多个项目。
- **复制/移动后的文件会被 chmod 成 `0777`**（前提是文件系统支持 Unix 权限）。FAT/exFAT 类文件系统
  可能忽略 chmod——那是文件系统自己的答复，不是出错。

**内容**

- **文本编辑器** —— 对 ≤ 1 MiB 的文件做原地 UTF-8 编辑，覆盖一份精选扩展名列表：`.txt .json .xml
  .ini .cfg .conf .md .log .lua .js .css .html .htm .c .h .cpp .hpp .sh .csv .yaml .yml .shn`。
  非 UTF-8 与超大文件会被直接拒绝，而不是改坏。
- **图片预览** —— `.png .jpg .jpeg .gif .bmp .webp`，直接由主机串出。
- **PKG** —— 安装 `.pkg` 文件，并预览其元信息。

**数据的进出**

- **上传** —— 工具条上的「上传 ▾」菜单里选**单文件**或**文件夹树**；整页拖拽上传同样可用，页脚也
  写明了这一点。文件先写临时名，传输完成后重命名就位。在 PS5 浏览器中隐藏——它的用途是让你从
  另一台设备去驱动主机。
- **下载** —— 单文件按原始字节下载；文件夹或多选则打成流式 `.tar`，不会先写进主机存储。在 PS5
  浏览器中隐藏。
- **上传并解压** —— 选中一个压缩包并勾选「上传后解压」，上传一落盘就开始解压；若发现是加密包，
  密码框会立刻弹出。

**压缩包解压** —— 完整支持矩阵见 [压缩包支持](#压缩包支持)。一句话：ZIP、RAR、7z，明文或加密、
单卷或分卷，全都走同一套尺寸 / 压缩比 / 路径穿越 / 磁盘空间保护，而且全部实现在本载荷内部——
不需要再装第二个文件。

**其他**

- **任务浮层** —— 全屏浮层，延迟显示、实时进度、吞吐率、ETA、取消，并能在浏览器中途关闭重开、
  而载荷进程仍在运行时恢复活动任务的显示。
- **本地化** —— 英文与简体中文，依 `navigator.languages` / `navigator.language` 自动选择
  （`zh*` → 中文，其余 → 英文）。
- **移动端友好** —— 响应式布局，工具栏自动换行，文件列表可横向滚动。
- **启动通知与主屏启动器** —— 通知会显示应用名、版本与实际监听端口；首次启动时载荷会在 Media
  分类安装一个「PS5 Web File Manager」快捷方式，且不覆盖已存在的启动器文件。启动器图标与浏览器
  favicon 用的是同一份内嵌 `icon0.png`，所以图标在 ELF 里只存一份。
- **文件名不因编码混杂而丢失** —— 名字经 Web API 以 UTF-8 传输，但载荷也会保留挂载文件系统返回的
  字节序名称，因此一块装着 GBK 文件名的 U 盘仍能正确显示与操作。上游有同一套机制；本仓多出来的
  是错误提示也会解码，不至于在最需要看清条目的那一刻给出乱码名（见[备注](#备注)）。

## 压缩包支持

三个引擎，由 `src/extract.c` 按扩展名分派，共用同一条三阶段流水线
（`scan → 解压到 staging → 按 rename 发布`）与同一套限额档位、冲突策略。
vendoring 决策与逐库许可证立场见
[`third_party/unrar7/VENDORED.md`](third_party/unrar7/VENDORED.md) 与
[`THIRD_PARTY_NOTICES`](THIRD_PARTY_NOTICES)。

| | ZIP | RAR | 7z |
|---|---|---|---|
| 引擎 | `src/zip_extract.{c,h}` | `src/rar_extract.{c,h}` | `src/sevenz_extract.{c,h}` |
| 后端 | vendored minizip-ng 4.2.2 + zlib | vendored **rarlab UnRAR 7.20.1**（官方源码） | LZMA SDK 26.03 解码子集 + 自研 codec 链 |
| stored / deflated | ✅ | 不适用 | ✅（Copy / LZMA / LZMA2 / PPMd） |
| 64 位尺寸 | ✅ ZIP64 | ✅ | ✅ |
| 过滤器 / 转换器 | — | — | ✅ Delta、BCJ2、PPC / IA64 / ARM / ARMT / SPARC |
| 分卷 | ✅ 引擎自行找齐各卷 | ✅ unrar 按名拼接 | ✅ |
| 传统密码 | ✅ PKWARE「ZipCrypto」（`zip -e`） | ✅ `-p` | — |
| AES 加密 | ✅ WinZip AES-128/192/256 | ✅ | ✅ 7zAES（AES-256-CBC） |
| 加密文件名 | — | ✅ `-hp` 头加密 | ✅ `-mhe=on` 加密头 |
| 密码询问时机 | 失败后询问并重试 | 失败后询问并重试 | 解压前提前询问 |

**可识别的分卷命名**

| 格式 | 接受 | 说明 |
|---|---|---|
| ZIP | `name.zip.001…`（7-Zip）、`name.part1.zip…`（WinRAR）、`name.z01…` + `name.zip`（Info-ZIP） | 任意一卷都可选，引擎会自己在同目录找齐其余分卷 |
| RAR | `name.part1.rar` / `name.part01.rar`（首卷） | 请选**首卷**；其余卷在界面中置灰并带提示 |
| 7z | `name.7z.001…` | 任意一卷均可，引擎会遍历目录取齐其余分卷 |

### 尺寸与安全限额

两档档位。默认档位出厂即安全；大档案档位**仅**在请求携带 `large=1` 时才启用，而界面会通过一次
确认提示让用户做出这个选择。

| 限额 | 默认 | 大档案（`large=1`） |
|---|---|---|
| `max_entries` | 200 000 | 500 000 |
| `max_total_bytes`（未压缩） | 2 TiB | 4 TiB |
| `max_file_bytes`（单条目） | 512 GiB | 1 TiB |
| `max_ratio`（未压缩 ÷ 压缩） | 500 : 1 | 1000 : 1 |
| `max_depth`（文件夹嵌套） | 32 | 32 |
| `max_name_len` / `max_path_len` | 255 / 1024 | 255 / 1024 |

默认上限是按主机上的真实工作量定的：一个 3A 作品打成「单个约 300 GiB 文件」的归档，无需任何提示
即可解出。

### 安全检查

在创建任何一个输出文件**之前**，解压就会拒绝以下情况：

- **路径穿越** —— `..` 段、绝对 POSIX 路径、Windows 盘符、RAR 内把 `\` 当分隔符。
- **特殊文件** —— 符号链接、设备、FIFO、套接字（`ZIPX_ERR_SPECIAL`）。
- **重复条目**，以及同一归档内目录与文件同名冲突。
- **突破限额** —— 解压后总尺寸、条目数、嵌套深度、名称长度或压缩比超出当前档位。
- **磁盘空间** —— `check_space()` 在开始写 staging 之前就按**解压后总量**查 `statvfs`，因此一个
  不可能完成的解压根本不会启动。

提交密码**不会**跳过 scan 阶段：加密归档与明文归档受同一套限额约束。

### 冲突策略

通过 `/api/extract` 上的 `conflict=` 传入：

- `fail`（默认）—— 只要目标已存在就失败。
- `overwrite` —— 覆盖已存在文件，合并进已存在文件夹。
- `merge` —— 保留已存在文件，只新增其余文件。

### 密码处理

密码缺失或错误会返回 `ZIPX_ERR_PASSWORD`（界面上的 `err_extract_password`）。前端会弹出密码框，
并**按原请求**重新发起——冲突策略、大文件选配全部沿用——最多三次；取消或留空则回落到最初的失败
提示。首次失败时的文案说的是「此压缩包已加密」，而不会去责怪一个你压根还没被问过的密码。

7z 是例外：因为 `-mhe=on` 把文件名藏在加密头里，密码框会**提前**出现、早于 scan——否则一个加密
的 7z 会先白跑一遍扫描，才轮到有人问你要密码。

### 调整大文件提示阈值

前端阈值位于 `assets/main.js`：

```js
const LARGE_FILE_THRESHOLD_BYTES = 480 * 1024 * 1024 * 1024;   // 480 GiB
```

磁盘上大于该值的归档会触发确认提示。设为 `Infinity` 可静音提示，调低则更保守，或干脆删掉该调用
——无论前端如何，服务器始终遵循 `large=1`。

### 本构建刻意不做的部分

- **ZIP / RAR / 7z 之外的格式。** `.tar`、`.tar.gz` / `.tgz`、`.gz`、`.xz`、`.bz2`、`.zst`、`.cab`、
  `.arj`、`.lzh`、`.cpio`、`.xar` 以及长尾里的其他格式都不识别。上游是靠把一整个 7-Zip 当作外部
  helper 进程分发，从而覆盖约 30 种后缀；本仓刻意不走这条路——原因见
  [与上游的差异](#与上游的差异)。
- **ZIP 中 stored / deflated 之外的压缩方法**、7z 中使用了不受支持 coder 的 folder、早于 RAR 1.4 的归档。
- **命名成 `x.rar.001` 的 RAR 分卷集。** unrar 只认它自己的 `x.partN.rar` 命名；把分卷改名
  （`.rar.001` → `.part1.rar`、`.002` → `.part2.rar`……）即可正常解压。ZIP 与 7z 的分卷集可以直接吃
  `.001` 风格。
- **字典超过 4 GiB 的 RAR。** 这类归档会被独立地报成 `err_extract_dict_too_large`，文案同时给出
  归档需要的尺寸与构建支持的尺寸。放行意味着**一次性分配整个字典窗口**——正是 rarlab 自家 CLI
  默认拒绝、16 GB 共享内存的主机也承受不起的那件事。（RAR5 头字段本身卡在 4 GiB，所以这种情况只
  可能来自更新版的 RAR7 头格式。）密码错误**不属于**这一类，多卷归档也不属于。

## 快速上手

1. **构建**载荷：

   ```sh
   export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk   # SDK 配置见下方「构建」
   make
   ```

2. **发送**到主机（ELF 加载器常用端口 `9021`）：

   ```sh
   nc -q0 "$PS5_HOST" 9021 < web-file-mgr-v1.9.3M.elf
   ```

3. **读取**主机屏幕上的通知——它会打印实际监听端口（通常 `8888`）。
4. 在同一局域网内任意浏览器中**打开** `http://<PS5_IP>:<port>/`。
5. 首次启动时载荷还会写入一个 **Media** 分类的主屏启动器；已有的启动器文件不会被改动。

## 构建

需要 [ps5-payload-dev/sdk](https://github.com/ps5-payload-dev/sdk#quick-start)：

```sh
export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk
```

本项目链接 `libmicrohttpd`。`make` 会先检查它，缺失时自动运行安装器：

```sh
make
```

若构建主机没有外网，可提前放入源码包并手动装一次：

```sh
LIBMICROHTTPD_TARBALL=/path/to/libmicrohttpd-1.0.1.tar.gz \
  ./install-libmicrohttpd.sh
make
```

输出：

```text
web-file-mgr-v1.9.3M.elf        # x86_64-sie-ps5，约 882 KiB
```

版本号是 `VERSION_TAG` 的一部分，因此也是输出**文件名**的一部分——一次构建不可能悄悄顶替掉另一个
版本的产物。需要时可以直接覆盖：

```sh
make VERSION_TAG=v1.9.4M
```

只想做纯 UI / JS 开发、不需要 PS5 工具链时：

```sh
make linux
./web-file-mgr-linux-v1.9.3M
```

Linux 构建**不包含** PS5 主屏启动器安装器。

## 使用

在主机上启动一个 ELF 加载器（常用端口 `9021`），发送载荷：

```sh
export PS5_HOST=ps5_ip_address
nc -q0 "$PS5_HOST" 9021 < web-file-mgr-v1.9.3M.elf
```

启动后，通知会显示应用名、版本与实际监听端口。打开它打印的 URL：

```text
http://${PS5_IP_ADDRESS}:8888/
```

如果 `8888` 已被占用，载荷会往上走到下一个空闲端口——请以通知显示的端口为准，URL 并未硬编码。
首次启动时它会在需要时于 Media 分类安装一个 `PS5 Web File Manager` 快捷方式；缺失的启动器文件会
被写入，已存在的会被保留。

## 校验产物

```sh
ls -la web-file-mgr-v1.9.3M.elf                    # 约 882 KiB
sha256sum web-file-mgr-v1.9.3M.elf                 # v1.9.3M 应为 8ca47d5a…c9bb
file  web-file-mgr-v1.9.3M.elf                     # 期望 "ELF 64-bit LSB pie executable, x86-64"
od -An -tx1 -N20 web-file-mgr-v1.9.3M.elf | head -2 # 魔数 7f45 4c46 0201，e_machine 003e
```

`e_machine = 0x003e` 确认了 PS5 目标三元组 `x86_64-sie-ps5`；`e_type = 3`（`ET_DYN`）确认了 ELF
加载器期望的位置无关载荷。

前端资源（JS / CSS / HTML）是 **gzip 压缩后内嵌** 进 ELF 的，所以拿 `strings` 去搜 `assets/` 里的
任何东西都不会有命中——这是压缩所致，不是内容缺失。请改用附带脚本：

```sh
python3 .build/check-elf-gzip.py ./web-file-mgr-v1.9.3M.elf uploadMenu extractRetryKey
```

## 测试

一套 POSIX / 主机端 C 测试套件覆盖 ZIP、RAR 与 7z 三个引擎，可在任意 Linux / macOS / MSYS shell
下、无需 PS5 SDK 运行：

```sh
cd tests && bash run-tests.sh     # ZIP + RAR 套件
bash run-sevenz-tests.sh          # 7z 套件（需 MinGW gcc 与 7-Zip 二进制）
```

当前 `main`：**177 项检查**（140 ZIP + 37 RAR），0 失败；7z 套件另有 **27 项用例**，0 失败。覆盖：

- ZIP 条目解析（stored、deflated、ZIP64），并与真实归档做逐字节内容比对
- 路径穿越、绝对路径、反斜杠、Windows 盘符
- 符号链接、FIFO、坏 CRC、截断归档、非 ZIP 输入
- 全部限额（条目数、总字节、单文件字节、压缩比、深度、名称长度）
- 冲突策略 `fail` / `overwrite` / `merge`
- 每个阶段的取消，以及「失败绝不发布任何文件、并清理自己的 staging 树」这一保证
- **加密归档** —— 每个真实 fixture 各跑四种情况：无密码、空密码、错密码都得
  `ZIPX_ERR_PASSWORD`，正确密码则成功并做逐字节内容校验。另有两项证明「提供密码后限额仍然生效」。
  fixture：`enc-zipcrypto.zip`、`enc-aes256.zip`、`enc-aes256-store.zip`（ZIP）、
  `enc-v6.rar`（RAR）、`aeshe.7z`（7z，加密头）
- **大档案档位** —— `medium_bomb.zip`（压缩比 ≈ 238）在默认档位下被拒、在大档案档位下通过
- **格式分派** —— 改名的 ZIP 与垃圾数据块都会被拒

三份前端 / 真页面验证脚本位于 `.build/` —— 该目录整体被 gitignore 忽略、只放行白名单，
而这三份连同文档渲染检查脚本都在白名单内，因此它们受版本控制、清理临时文件时不会被误删：

| 脚本 | 覆盖内容 | 检查数 |
|---|---|---|
| `ui_retry_test.mjs` | 真实 `assets/main.js` 载入桩 DOM 后的密码重试流程：参数记忆、重试上限、取消 / 空密码的回落，以及「非 ASCII 目录必须仍弹口令框」的回归用例 | 40 |
| `ui_upload_menu_test.mjs` | 标记侧：`index.html` 里每个 `data-i18n` 键在两份语言文件中都存在、`main.js` 里 117 个 `t("…")` 键全部有译文、上传菜单接对了回调、用到的 class 确实有样式、菜单行高亮规则保住了面板作用域，以及解压按钮**绝不隐藏、只置灰** | 40 |
| `preview_check.mjs` | 真页面 + 桩 API + 无头 Chromium：菜单静止时隐藏 / 点击打开 / 焦点落位 / 真能点到 file input / 关闭，页脚布局，以及被钉死的工具栏换行阈值 | 12 项断言 |

## 项目结构

```
.
├── .build/                       # 构建脚本 + 验证脚本（目录其余部分被 gitignore）
├── Makefile                      # PS5 + Linux 构建（VERSION_TAG v1.9.3M）
├── install-libmicrohttpd.sh      # 一次性依赖安装器
├── gen-asset-module.py           # 将 assets/* 内联为 gzip 压缩的 C 数组
├── assets/                       # HTML / CSS / JS / 图标 / param.json
├── src/                          # C 载荷源码
│   ├── main.c  websrv.c  filemgr.c        # 入口、HTTP 前端、任务模型
│   ├── upload.c  download.c  text.c       # 流处理与原地编辑
│   ├── extract.c                          # /api/extract 分派器（ZIP + RAR + 7z）
│   ├── zip_extract.{c,h}  zipx_common.c   # ZIP 引擎（minizip-ng 后端）
│   ├── zipx_volume.c  zipx_volstream.c    # ZIP 分卷探测 + 拼接流
│   ├── rar_extract.{c,h}                  # RAR 引擎（rarlab UnRAR 7.20.1 后端）
│   ├── sevenz_extract.{c,h}  sevenz_chain.{c,h}   # 7z 引擎，自解析 codec 链
│   ├── sevenz_header.{c,h}                # 7z 头部读取 / `-mhe=on` 解密
│   ├── sevenz_mt.c  sevenz_volstream.c    # 多线程 LZMA2 + `.7z.001` 分卷
│   ├── app_installer.c  pkg_installer.c  pkg_info.c   # PS5 PKG 预览 / 安装
│   └── demangle_stub.c  cpu_support_stub.c            # 体积 / 可移植性桩
├── third_party/                  # vendored 库
│   ├── unrar7/                            # rarlab UnRAR 7.20.1 —— RAR 引擎
│   ├── minizip-ng/                        # 4.2.2，裁剪到只留读取路径
│   ├── 7z/                                # LZMA SDK 26.03 解码子集
│   └── zlib/                              # minizip 的压缩后端
├── tests/                        # POSIX / 主机端测试套件
│   ├── test_zip_extract.c  test_rar_extract.c  test_sevenz_extract.c
│   ├── sevenz_chain_e2e.c  sevenz_e2e.c  bigfile_e2e.c
│   ├── make_fixtures.py  make_sevenz_fixtures.py  make_split_fixtures.py
│   ├── run-tests.sh                       # 一次性运行器（ZIP + RAR）
│   ├── run-sevenz-tests.sh                # 7z 套件
│   ├── bench_driver.py  bench_formats.py  # 吞吐基准
│   ├── compat/                            # 小型 Win32 / MSYS 垫片
│   └── fixtures/  fixtures-7z/  fixtures-real/
├── docs/
│   ├── USER-GUIDE-zh-CN.md       # 新手使用说明（中文）
│   ├── DEVICE-TEST-v1.9.3M.md    # 发布前跑过的真机验收清单
│   ├── SIZE-OPTIMIZATION.md      # ELF 体积分析 + 逐符号台账
│   ├── EXTRACTION-PERF.md        # 解压基准
│   ├── REAL-CONSOLE-PROFILE.md   # 真机实测吞吐
│   ├── UPSTREAM-V1.8-COMPARISON.md  # 本仓 vs 上游 helper 路线
│   ├── REWRITE-FEASIBILITY.md    # 引擎抽取可行性研究
│   ├── UPGRADE-v1.7-zip-large-file-profile.md
│   ├── UPGRADE-v1.8-rar-support.md
│   └── screenshots/              # README 截图
├── CHANGELOG.md                  # 逐版本变更记录
├── THIRD_PARTY_NOTICES           # 捆绑库署名
├── HANDOVER.md                   # 现行开发交接文档
├── LICENSE                       # GPLv3+
└── README.md
```

## 与上游的差异

本项目 **fork 自 [owendswang/ps5-web-file-manager](https://github.com/owendswang/ps5-web-file-manager)**。
Web UI、任务模型与 PS5 打包方式均源自该项目；上游作者以 GPL-3.0 发布，是本衍生作品得以存在的前提。
从 v1.8 起，上游把解压**外包给一个独立 helper 进程**——一整个 7-Zip，做成
`wfm-7zip-helper.elf`，由用户自行安装到 `/data/wfm/`。本仓走的是相反的路：解码器 vendor **进**
载荷内部。

| | 上游 | 本仓 |
|---|---|---|
| 解压架构 | 外部 `wfm-7zip-helper.elf`（1,017,616 B，单独分发，路径写死 `/data/wfm/`），用 Unix socket IPC 协议驱动 | 引擎就在**载荷内部**；没有第二个文件，没有 IPC |
| 部署 | 两个文件合计 1,363,048 B；helper 缺失或放错位置，解压功能全废（`archive_helper_not_running`） | 单 ELF 903,448 B，零外部依赖 —— **小 33.7%**，且上游那个 helper 单独一个就比本仓整个载荷还大 |
| 格式 | 约 30 种后缀（`.tar`、`.gz`、`.xz`、`.bz2`、`.zst`、`.cab`、`.arj`、`.lzh`、`.cpio`……） | `.zip` / `.rar` / `.7z` 及其分卷形态——三种，但每一种都完整 |
| 防压缩炸弹 / 压缩比 | 无 | 条目数、总尺寸、单文件尺寸、压缩比筛查，并对 1 GiB 以下小文件豁免以免误判 |
| 磁盘空间预检 | 无 | 写 staging 前按解压后总量查 `statvfs` |
| 路径穿越防护 | 交给 7-Zip | 本仓实现，并有专项测试组 |
| 失败残留 | 可能留下解压了一半的目录 | staging 目录 + rename；失败或取消都会清理，且不发布任何文件 |
| 密码提示 | helper 的 IPC 协议里带 `PASSWORD_REQUIRED` 消息 | 失败后弹框重试（上限三次），统一报为 `extract_password`；7z 提前询问 |
| 载荷重启后的任务存活 | ✅ helper 是独立进程，解压任务不会丢 | ❌ 重启会丢掉正在跑的任务 |
| 内存隔离 | ✅ 解压在独立进程里 | ❌ 共享地址空间（改为对 LZMA2 字典封顶） |
| 版本标识 | 纯 `vX.Y.Z` | `vX.Y.ZM`——尾部的 `M` 标记本仓改版 |

这套取舍的实测数据与推理过程在
[`docs/UPSTREAM-V1.8-COMPARISON.md`](docs/UPSTREAM-V1.8-COMPARISON.md)。一句话：
**上游赢在格式广度与进程架构，本仓赢在安全、部署与错误质量。** 格式覆盖的差距是现有架构里可以
增量补的活，不构成推倒重来的理由。

## 备注

- 复制、移动、删除、上传、下载都作为单个后台任务运行。一个任务运行时，其它文件操作会被拒绝。
- 删除是递归且永久的，没有回收站。
- 复制 / 移动任务可取消。单个文件的部分拷贝会被移除；部分拷贝的**文件夹**会保留在原地，以免在
  合并进已存在的目标文件夹时误删既有文件。
- 上传任务可取消；尽可能移除部分上传的临时文件。
- 下载文件夹或多选会生成 tar 流，就地生成——不会先写入主机存储。
- 若浏览器在载荷进程仍在运行时被关闭重开，界面可恢复活动任务的显示。
- 文本编辑仅限上述扩展名列表；非 UTF-8 与超大文件会被拒绝。
- **文件名编码：** 名字经 Web API 以 UTF-8 传输，而挂载的文件系统可能返回遗留字节序列（比如一块
  GBK 的 U 盘）。为了不丢这些字节，API 会把每个 ≥ `0x80` 的字节映射成 `\u00XX`、回程再还原，
  前端显示时按 GBK / gb18030 解码。实际后果是：同一个目录在「页面手里」与「服务端手里」是两串
  不同的字符串——这就是为什么前端里任何东西都不能拿路径当跨请求的键。

## 常见问题

- **这是自制软件，不会有意修改系统进程或内核内存。** 若遇到内核崩溃（kernel panic），请确认使用
  较新的越狱方法与 ELF 加载器，或回到你惯用的稳定方案。
- **P2JB 用户** —— 若此载荷在该环境下触发内核崩溃，请勿在此环境使用。当每次重试代价都很高时，
  稳定性比便利更重要。
- **「准备阶段」在文件很多的目录下可能耗时较久** —— 它会累加目录大小并检查剩余空间，这正是让一个
  无法安全完成的复制 / 移动 / 上传 / 下载从一开始就不会启动的原因。
- **`err_extract_unsupported`** —— 这个包本机读不了：既非 `.zip` / `.rar` / `.7z` 的文件；ZIP 条目
  用了 stored / deflated 之外的压缩方法；7z 用了不支持的 coder；分卷命名不被识别（RAR 分卷若叫
  `x.rar.001`，需改名为 `x.part1.rar`、`x.part2.rar`……）；或早于 RAR 1.4 的归档。**加密归档与
  多卷归档不属于这一类**——两者都支持。界面会在括号里附上后端原文，指明具体原因。
- **`err_extract_entry_too_large`** —— 归档超出默认上限（单条目 512 GiB / 500:1 比率）。确认那个
  大文件提示（磁盘上 > 480 GiB 的归档会出现）、拆分归档，或直接向 API 传入 `large=1`。
- **`err_extract_dict_too_large`** —— RAR 归档声明的压缩字典超过本构建支持的上限（4096 MiB）。
  请在 PC 上用不超过 4 GiB 的字典（`-md`）重新压缩，或直接在 PC 上解压。
- **`err_extract_password`** —— 归档已加密，而密码缺失或错误。这也包括带加密头（`-mhe=on`）的 7z：
  文件名与条目尺寸都在头部里，头解密之前连条目列表都读不出来。

## 版本历史

逐版本的产物、摘要、段尺寸增量与测试计数见 [`CHANGELOG.md`](./CHANGELOG.md)。

| 版本 | 日期 | 一句话 |
|---|---|---|
| `v1.9.3M` | 2026-09-24 | 加密归档端到端打通（ZIP ZipCrypto + WinZip AES、RAR `-p`/`-hp`、7z 7zAES 含 `-mhe=on`）、字典超限独立报错，以及一轮 UI（上传菜单、拖拽提示、解压按钮常显置灰） |
| `v1.9.2` | 2026-09-05 | 只改版本号的重发版；重新打 tag 使 tag = 源码 = 二进制 |
| `v1.9.1` | 2026-09-05 | 7z 引擎、分卷、7zAES，以及 −15.8% 体积 / 吞吐优化 |
| `v1.9` | 2026-09-05 | RAR 引擎换成 rarlab UnRAR 7.20.1（RAR5「v6」、多卷） |
| `v1.8.3` | 2026-09-05 | 「上传并解压」开始接受 `.rar` |
| `v1.8.2` | 2026-09-05 | 为 3A 单文件归档放宽单条目上限；两个 PS5 专属构建修复 |
| `v1.8.1` | 2026-09-05 | 为系统备份归档放宽默认 ZIP 上限 |
| `v1.8` | 2026-09-05 | 首个 RAR 支持（dmc_unrar），共享解压协议 |
| `v1.7` | 2026-09-04 | ZIP 大文件档位（`large=1`） |

## 署名

本项目 **fork 自 [owendswang/ps5-web-file-manager](https://github.com/owendswang/ps5-web-file-manager)**（GPL-3.0）。Web UI、任务模型与 PS5 打包方式均源自该项目；上游作者以 GPL-3.0 发布，是本衍生作品得以存在的前提。

**怎么区分上游原版与本仓改版：** 自 v1.9.3 起版本号带 `M` 后缀（`vX.Y.ZM`），*M* 即 *Modified*（改版）；上游 owendswang 的发布版是纯 `vX.Y.Z`。因此 `v1.9.2` 是上游 / 本仓共用的编号，而 `v1.9.3M` 只可能出自本仓；这个字母同时出现在 ELF 文件名、PS5 启动通知、`/api/version` 与网页右下角。v1.9.3M 之前的发布早于该约定，保留原本的无后缀编号。

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
- **[rarlab UnRAR](https://www.rarlab.com/rar_add.htm)** —— v1.9 起 `/api/extract` 使用的 RAR 读取器（7.20.1，RARDLL 源文件集）。vendored 于 `third_party/unrar7/`。许可证：**UnRAR 免费软件许可**（见 `third_party/unrar7/license.txt`）。注意这是受限许可而非 FLOSS 许可：它允许用源码处理 RAR 归档，但禁止用它开发 RAR 兼容的压缩器。
- **[opello/unrar](https://github.com/opello/unrar)** —— vendored 的 rarlab 源码取自该镜像（提交 `97e1780`）。
- **[LZMA SDK](https://www.7-zip.org/sdk.html)**（7-Zip / Igor Pavlov）—— v1.9.1 起 `/api/extract` 使用的 7z 解码器，以解码子集形式 vendored 于 `third_party/7z/`。许可证：公有领域。
- **[DrMcCoy/dmc_unrar](https://github.com/DrMcCoy/dmc_unrar)** —— 仅 v1.8 使用的 RAR 引擎，v1.9 被 rarlab UnRAR 取代（它无法解码 RAR5「v6」归档，也不支持多卷）。已从树中移除；其许可证为 GPL-2.0-or-later。

## 许可证

本项目以 **GPLv3 或更高版本** 分发，与作为实现参考的 GPLv3+ 项目保持一致。见 [`LICENSE`](./LICENSE)。

第三方项目保留各自许可证。请勿在未保留相应许可证声明的情况下，将署名项目的资源或源码复制到其它发行版中。

若分发二进制，除本项目 GPL 许可外，还需遵守 `libmicrohttpd` 的 LGPL 条款。vendored 的 `zlib` 与 `minizip-ng` 源码以 zlib 许可分发；再分发用此特性构建的二进制时，保留 `third_party/zlib/LICENSE` 与 `third_party/minizip-ng/LICENSE` 中的版权声明。

vendored 的 `third_party/unrar7/`（rarlab UnRAR —— `src/rar_extract.c` 背后的 RAR 引擎）**不是** GPL：它依 UnRAR 免费软件许可分发（见 `third_party/unrar7/license.txt`），该许可禁止用它开发 RAR 兼容的压缩器。再分发时请保留该声明与限制。`THIRD_PARTY_NOTICES` 载有逐库完整摘要。

## 免责声明

非官方自制软件。仅在已越狱 PS5 主机上运行。使用风险自负——作者不对损坏、数据丢失、账号处罚或保修影响负责。请勿再分发 Sony 专有内容。依 GPLv3+，修改后的再分发必须公开其源码。
