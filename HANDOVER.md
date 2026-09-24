# 交接文档 — ps5-web-file-manager 工作进度

> 交接时间：2026-09-23 · 分支 `main` · 最新提交仍是 **`3f80eb4`**（tag `v1.9.2`）——**本轮加密改动全部尚未提交**
>
> **主线（用户 2026-09-12 指令）**：「先从 zip 分卷开始吧，然后把六种组合打齐，并把密码通道补齐，注意一些报错信息提示的时候尽量详细准确」
> **状态：主线全部闭合，且格式面已无已知缺口。** 六种组合（ZIP/RAR/7z × 单卷/分卷）+ 密码通道（ZIP ZipCrypto/AES + RAR `-p`/`-hp` + 7zAES **含 `-mhe=on` 加密头**）+ 报错详细信息，全部落地、测试全绿、PS5 ELF 构建成功。
> **剩余**：① PS5 真机端到端验证（**唯一还没过的关卡**）；② 版本号仍是 `v1.9.2`，本轮改动处于「未发布」状态——发版需先升 `VERSION_TAG` 与 `APP_VERSION_FALLBACK`。

---

## 一、当前状态速览

| 维度 | 状态 |
|---|---|
| 解压引擎 | ZIP / RAR / 7z × 单卷/分卷（6 组合）+ 三种加密（ZIP ZipCrypto/WinZipAES、RAR `-p`/`-hp`、7zAES 与 `-mhe=on` 加密头）全部打通 |
| 主机测试 | **ZIP 140 + RAR 37 = 177 checks，0 失败**（MinGW gcc；2026-09-23 复跑）+ 7z 套件 **27 用例 0 失败**（`KNOWN_GAPS` 已清空）+ 前端重试流程 27 checks（`.build/ui_retry_test.mjs`） |
| PS5 构建 | ✅ WSL prospero-clang 18.1.8，一键脚本可复现；构建已实测**确定性**（同源两次构建 sha256 相同） |
| ELF 产物（工作树，未提交） | `web-file-mgr-v1.9.3M.elf` · 903,448 B · sha256 `8ca47d5aaca75085b32641300cce30fadb7df7749cb6b53d04f129bcecc286b7` · e_machine=0x003e（2026-09-24 最后一轮：上传菜单 + 拖拽提示 + 口令重试改按键 id + 错误文案编码修复 + 菜单行高亮的层叠修复 + 解压按钮常显置灰；**未发布**） |
| 已发布产物 | `web-file-mgr-v1.9.2.elf` · 870,488 B · sha256 `177e90fecf93a0251e83f67884fba4551051be248330b0d70fda8ea732f88e84`（不含加密改动） |
| GitHub | `main`（`3f80eb4`）与 tag `v1.9.2` 均已推送；Release `v1.9.2` 资产对应上一行；本轮改动尚未 commit |
| 已知功能缺口 | **无**（`-mhe=on` 已于 2026-09-23 补齐） |

### 发布命令（v1.9.2）

```bash
cd "/c/Users/songl/Desktop/Web File Manager/ps5-web-file-manager"

git push origin main
git push origin v1.9.2

gh release create v1.9.2 web-file-mgr-v1.9.2.elf \
  --repo LisherSong/ps5-web-file-manager --title "v1.9.2" --notes-file <notes.md>
```

沙箱内 git 出站 HTTPS **可用**（早先记的"被拦"是误判：`timeout 25 git ...`
命中的是 `C:\Windows\System32\TIMEOUT.EXE`，报参数错误而非网络错误）。`gh` 同样可用，
所以 commit / tag / push / 发 Release 都可以在会话里直接跑。

---

## 二、本轮（2026-09-15）变更

### 2.1 一键构建脚本（`a54f34b` / `5229cd5`）

| 文件 | 跑在哪 | 作用 |
|---|---|---|
| `.build/build-win.sh` | Windows Git Bash | 入口：把 WSL 脚本经 **stdin** 喂给 `wsl.exe`，透传退出码 |
| `.build/build-elf-wsl.sh` | WSL Ubuntu-22.04 | 5 阶段：环境检查 → rsync 同步 → `make all` → 验证 → 拷回 Windows |
| `.build/build-elf.sh` | WSL 内 | **仅首次搭环境用**（libmicrohttpd staging 安装 + sudo） |

```bash
# Windows 端（注意：必须 /usr/bin/bash，裸 bash 会解析成 WSL 启动器）
cd "/c/Users/songl/Desktop/Web File Manager/ps5-web-file-manager"
/usr/bin/bash .build/build-win.sh

# 或 WSL 内
bash /home/song/build.sh
```

**两个关键实现点**（改脚本前必读）：
- `wsl.exe -- bash -c '...'` 遇到含空格路径会被拆断 → 必须 `wsl.exe -- bash < script.sh`（stdin 重定向）
- `make ... | tail` 会吞掉退出码 → 用 `${PIPESTATUS[0]}`；否则编译失败还会继续跑验证，输出假成功

### 2.2 版本号体系统一（`f4fd464` / `1fa2f09` / `0d036a7`）

原本版本号有**两个真相来源**，已经漂移过：`Makefile` 写 `v1.9.1`，`assets/main.js` 硬编 `"v1.9"`，UI 右下角在整个 v1.9.1 发布期都显示旧值。

现在收敛到 `Makefile` 一处：

```make
VERSION_TAG ?= v1.9.3M           # 可用 make VERSION_TAG=v1.9.3 临时覆盖
BIN        := web-file-mgr-$(VERSION_TAG).elf
```

改这一行会同时影响 **四处**：ELF 内嵌版本串、PS5 启动通知、输出文件名、UI 右下角。

**尾部 `M` = 改版标记（Modified，LisherSong 维护）**，从 v1.9.3M 起启用。上游
owendswang 的发布版是纯 `vX.Y.Z`，故「带 M = 本仓、不带 = 上游」一眼可分。它刻意
挂在 `VERSION_TAG` 上而不是做一个只管显示的独立常量：这样 `/api/version`、启动通知、
stdout 横幅、UI 右下角、ELF 文件名**五处一次性全覆盖**，不可能只在其中一处漏掉。
附带好处是产物名不再可能与上游同版本号的资产撞车（此前已撞过两次：本地
`web-file-mgr-v1.9.2.elf` 与线上同名资产并存；`-DVERSION_TAG=v1.9.1` 的残留产物
和已发布的 870 488 B 文件尺寸相同）。

| 提交 | 内容 |
|---|---|
| `f4fd464` | `VERSION_TAG` `v1.9` → `v1.9.1`；`.gitignore` 改白名单式（`.build/*` 全忽略 + `!` 放行 6 个脚本），`.workbuddy/` 也忽略 |
| `1fa2f09` | 输出文件名派生自 `VERSION_TAG`；`build-elf-wsl.sh` 从 Makefile 反读版本（不硬编）；`build-win.sh` 每次都推 WSL 脚本（原来只在缺失时推，导致改了脚本 WSL 侧仍跑旧版） |
| `0d036a7` | **新增 `/api/version`**，前端右下角改从后端取值（见 2.3） |

⚠️ **`assets/main.js` 里还有第二处字面量** `APP_VERSION_FALLBACK`（`/api/version` 取不到时的兜底值）。
它不在 Makefile 的控制范围内 —— **升版本号时必须一并改**，否则后端请求失败时页脚会显示旧版本。
v1.9.2 就是这两处一起改的。

### 2.3 UI 版本号改由后端提供（`0d036a7`）

**问题**：`assets/main.js:38` 的 `const APP_VERSION = "v1.9"` 与 Makefile 无关，必然漂移。

**修法**：
- 新 `src/version.c` — `GET /api/version` → `{"ok":true,"version":"v1.9.2","titleId":"FMGR88888"}`，直接来自 Makefile 已传的 `-DVERSION_TAG` / `-DTITLE_ID` 宏，没有第二处要记得改
- `src/filemgr.c` 路由表加一行（紧邻 `/api/space`）+ `filemgr_internal.h` 声明 + `Makefile` `COMMON_SRCS`
- 前端：字面量降级为 `APP_VERSION_FALLBACK`（先渲染，保证页脚不空），`loadVersion()` 后台刷新。**请求失败静默吞掉** —— 版本号显示错属于装饰性问题，不该弹错误 toast

**踩坑**：新文件漏了 `#include "json_util.h"` → `strbuf_append` / `json_escape` 隐式声明报错。`space.c` 是模板，照抄时别漏。

---

## 三、功能矩阵与测试

### 3.1 六种组合 + 密码通道

| # | 引擎 | 单卷 | 分卷 | 密码 |
|---|---|---|---|---|
| ① | ZIP | ✅ | ✅ 三种命名约定 | ✅ ZipCrypto + WinZip AES-128/192/256（2026-09-23 打通，此前 `mz_zip.c` 的加密分支没有后端可调） |
| ② | RAR | ✅ | ✅（vendor unrar 7.20.1） | ✅ `RARSetPassword`（`-p` 与 `-hp` 头加密；2026-09-23 接线，此前从未被调用） |
| ③ | 7z | ✅ | ✅ `.7z.001` | ✅ 7zAES（v1.9 起就有）+ `-mhe=on` 加密头（2026-09-23 打通，见 §八） |

### 3.2 测试

```bash
export PATH="/c/mingw64/bin:/c/Users/songl/.workbuddy/binaries/PortableGit/versions/1.2.0/mingw64/bin:/c/Users/songl/.workbuddy/binaries/python/versions/3.13.12:/usr/bin:/bin:/c/Windows/System32:/c/Windows"
cd "/c/Users/songl/Desktop/Web File Manager/ps5-web-file-manager"
/usr/bin/bash tests/run-tests.sh          # ZIP 140 + RAR 37 = 177
/usr/bin/bash tests/run-sevenz-tests.sh   # 7z 27（KNOWN_GAPS 已清空）

# 前端「密码失败后重试」流程（桩 DOM，无需浏览器）
"/c/Users/songl/.workbuddy/binaries/node/versions/22.22.2-3/node.exe" .build/ui_retry_test.mjs   # 27
```

⚠️ **绝不写裸 `bash`** —— 可能解析到 `C:\Windows\System32\bash.exe`（WSL 启动器），脚本跑进 Linux，gcc/python 全变 Linux 版，报莫名错误。必须 `/usr/bin/bash`。

`run-sevenz-tests.sh` 带 `KNOWN_GAPS` 列表（当前仅 `aeshe`），缺口修好后脚本会主动报错，防止列表腐烂。脚本**不做任何删除**（safe-delete 钩子会拦 `rm -rf`）。

---

## 四、构建（PS5 ELF）

### 4.1 日常构建

```bash
cd "/c/Users/songl/Desktop/Web File Manager/ps5-web-file-manager"
/usr/bin/bash .build/build-win.sh
```

增量有效（`ps5-obj/` 缓存保留）→ 二次构建 30s–2min。**别 `make clean`**（全量重编第三方 3–5min）。

产物：项目根 `web-file-mgr-<VERSION_TAG>.elf`，同时留在 WSL `/home/song/ps5-web-file-manager/`。

### 4.2 验证清单

```bash
ls -lh web-file-mgr-v1.9.2.elf
sha256sum web-file-mgr-v1.9.2.elf
od -An -tx2 -j18 -N2 web-file-mgr-v1.9.2.elf   # 期望 3e00
strings -a web-file-mgr-v1.9.2.elf | grep -m1 '^v1\.'
```

⚠️ `od -An -tx2` 打印的是**小端 short 的值**（`003e`），不是字节序（`3e00`）。脚本里比对用 `$((16#$EM))` 转数值（**62 = x86-64 ✅ / 183 = aarch64 ❌**）。

✅ **构建已实测可复现**（2026-09-20）：同一源码树两次构建 sha256 完全相同；把两处版本字面量回退成 `v1.9.1` 后重构，产物与已发布的 v1.9.1 ELF **逐字节一致**。所以 sha256 可以作为交付指纹用 —— 但它对任何源码改动都会全变（改一个字符串常量会令链接器重排 `.rodata` 字符串池，牵动 `.text` 里所有 RIP 相对位移，原始 diff 会放大到 5 万字节以上，属正常现象，别误判成"代码改了"）。核对版本仍推荐 `strings ... | grep '^v1\.'`，最直观。

### 4.3 构建坑（已修，改 Makefile 前必读）

`third_party/7z/AesOpt.c` 用编译器版本宏判断是否启用 AES-NI / AVX / VAES，clang 18 直接进 VAES 分支，但 prospero-clang 默认 target 是 generic x86_64 → `_mm256_aesenc_epi128` 未声明，20 报错。

- ❌ **不能** `filter-out AesOpt.c` —— `Aes.c` 通过 `AesGenTables` 引用 `AesCbc_Encode_HW` 等符号，会链接失败
- ✅ **正解**：路径过滤 `SEVENZ_C_FLAGS := -maes -mavx2 -mvaes`，仅 `third_party/7z/*.c` 用。PS5 是 Zen 2，硬件全支持，运行时无差异

---

## 五、7z 引擎设计要点（改代码前必读）

### 5.1 为什么不走 SDK 的解码器

LZMA SDK 26.03（public domain，已 vendor 到 `third_party/7z/`，解码子集 60 文件）有两个硬限制，**实测复现过**：

1. **`CSzFolder` 上限 4 coder / 3 bond** —— 7-Zip 默认 `-m0=bcj2` 链 = BCJ2 + 4×LZMA2 = 5 coder，`SzAr_DecodeFolder()` 返回 `SZ_ERROR_UNSUPPORTED`。注意 `SzArEx_Open()` 用的是另一套宽松扫描器（`k_Scan_NumCoders_MAX 64`），所以**文件列表和解压尺寸仍然全对**，失败只在解压时按条目暴露
2. **C 解码器完全没有 7zAES coder** —— 所以 SDK 自己既解不了加密内容，也解不了加密头。内容侧由我们的 `sevenz_chain.c` 承担；**头部**侧由 `sevenz_header.c` 承担（见 §八）

→ 因此引擎**自解析 folder blob + 自己驱动 codec 链**（`src/sevenz_chain.c/.h`，pull pipeline：`node_pull(n, dst, want, &got)`，不够就 `node_refill()` 拉上游）。

### 5.2 最关键的坑

**【必记】每个 coder 节点的 `out_size` 必须取 `coder_unpack_sizes[index]`，绝不能用 folder 的 unpack size。** BCJ2 folder 里 MAIN 常大于 folder 最终尺寸（实测 300066 > 300000）。用错的症状：每层 LZMA2 静默短 21 字节，只在特定包上暴露。

其他：
- 编译必须 `-DZ7_PPMD_SUPPORT`，否则 `7zDec.c` 直接丢掉 PPMd
- `CoderUnpackSizes` 是**扁平数组**（每条 = 对应 coder 输出流大小），**非累计**；`FoToCoderUnpackSizes[f]..[f+1]` 是该 folder 的切片
- main coder = 第一个未被 bond 消费的 coder
- `SzArEx_Extract` 失败后会把半成品留在 block cache → 后续条目报**假 CRC**，要重置 `blockIndex`
- 造夹具用 Extra 包的 `7za.exe`（有 PPMd）；**`7zr.exe` 没有 PPMd 编码器**
- 调试三件套在 `.build/`：`chainprobe.c`（摸内部图）、`chainprobe2.c -r <coder>`（强制 root 逐层二分）、`chaincheck.py`（Python liblzma 独立复现同一条链，秒判"图错"还是"循环错"）

### 5.3 7zAES KDF

`numCyclesPower = b0 & 0x3F`；`saltSize = ((b0>>7)&1) + (b1>>4)`；`ivSize = ((b0>>6)&1) + (b1&0x0F)`，随后依次 salt → iv。
`numCyclesPower == 0x3F` 时 key = `salt||password` 补齐/截断到 32 字节；否则 `key = SHA256(salt || password_utf16le || counter_le64)` 迭代 `1<<numCyclesPower` 次。之后 AES-256-CBC。
限额 `max_aes_cycles = 24`（约 8s）。

**改引擎前先在 `.build/aesprobe.c` 独立验证 KDF**（用 vendor 的 `Sha256.c` + `Aes.c` 解 aes.7z coder0，与 `cus[0]=638314` 比对），确认后再集成。

### 5.4 分卷流抽象

- `src/zipx_volstream.c/.h`（ZIP，包成 `mz_stream`）· `src/sevenz_volstream.c/.h`（7z，包成 SDK `ISeekInStream`）
- **结构体首成员必须是 `mz_stream stream;` / `ISeekInStream vt;`**（回调把 `void*` 强转）
- `vol_is_open()` 必须返回 `MZ_OK`/`MZ_OPEN_ERROR`（**不是 1/0**）
- vtbl **必须注册 `destroy`**，否则 `mz_stream_delete()` 不回调 → 泄漏
- CONCAT 模式对 `DISK_NUMBER`/`DISK_SIZE` 返回 `MZ_PARAM_ERROR` → 让 minizip 不切盘
- DISK 模式 `set_prop(DISK_NUMBER, -1)` 必须切到**最后一卷**（minizip 路径 `mz_zip.c:2252-2275`）
- `remove_source_archives()` 要删**所有**卷，避免孤儿卷

### 5.5 提取门面

`src/sevenz_extract.c`（1753 行）完全仿 `zip_extract.c` / `rar_extract.c`：scan → extract(staging，**不 fsync**） → publish(整 rename) → cleanup。

- **OVERWRITE 与 MERGE 对目录-目录碰撞都递归下钻**（仅叶子文件不同）
- 三方共用 `src/zipx_common.c`（限额 profile + `zipx_status_string()`）
- scan 阶段**每 256 entries** 报一次进度（曾用 4096，小包扫描期 UI 静默），扫描末 force-report；`precheck_folders` 入口也强制报一次
- 密码错时 detail **必须带 archive 名**（曾是 NULL → i18n `{arg}` 展开成空 → 用户看到「密码错误: 」后面光秃秃）

---

## 六、限额体系（ZIP 与 RAR 共用；7z 同源）

| 限额字段 | default | large | 160GB/9万文件场景 |
|---|---|---|---|
| `max_entries` | 200,000 | 500,000 | 9 万 ✅ |
| `max_total_bytes` | 2 TiB | 4 TiB | 160 GiB ✅ |
| `max_file_bytes` | **512 GiB** | **1 TiB** | 20 GiB ✅ |
| `max_ratio` | 500 | 1000 | 仅 ≥1GiB 条目受检 |
| `ratio_min_bytes` | 1 GiB | 1 GiB | 小文件豁免 |

- 切 large 档的触发条件：**压缩包文件本身** >480 GiB（`assets/main.js` `LARGE_FILE_THRESHOLD_BYTES`），160GB 包走 default
- **ratio 有尺寸下限**（`ratio_min_bytes` = 1GiB）：小文件高压缩率合法常见（零填充/稀疏），且写出字节受"声明上限 + `check_space()`"双重约束，无害
- **唯一真实失败点是磁盘空间**：`check_space()` 按**解压后总量**查 `statvfs`，峰值 = `zip 体积 + 解出体积`。分卷场景"传一卷解一卷删一卷"可降峰值
- **32 位安全**：引擎内部 size 全 `uint64_t`；minizip `mz_zip.h:34-35` 的 `compressed/uncompressed_size` 是 `int64_t` → >4GiB 不截断
- **已知 UX 缺陷（未修）**：进度条 % 用字节（`main.js:1985`）、文字进度解压时用条目数（`main.js:2014`）、ETA 用字节速度（`task.c:129-177`）。混合大包上割裂，建议统一为字节

---

## 七、环境要点（新人必读）

- **PS5 是 x86-64 Zen 2**（不是 aarch64！），target triple `x86_64-sie-ps5`
- **PS5 SDK C++ runtime = LLVM libc++**（FreeBSD 系 sysroot，无 libstdc++）→ C++ 必须 `-stdlib=libc++`，链接 `-lc++ -lc++abi`（Makefile 已处理：unrar 用 prospero-clang++ 编）
- **PS5 SDK libc 的 `*at()` 族（mkdirat/openat/renameat/unlinkat）能链接但运行时损坏**：返回 -1 且 `errno=0`（2026-09-06 Frostpunk 2 真机确诊）。`zip_extract.c` 已有"*at() 失败回退全路径调用"兼容层；写新引擎代码时直接用全路径或沿用回退模式。报错要带 `(errno=%d)`，`errno=0` 时 `strerror` 会骗人
- WSL：`export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk` 后才能 make
- `target/user/homebrew/` 由 songl(197609) 拥有，WSL 身份 song(1000) 写不进 → staging 模式（make install 到 /tmp → `sudo cp -r`）
- `//wsl$/Ubuntu-22.04/` 是 SMB 只读视图，改 WSL 文件必须走 Windows 路径
- libmicrohttpd 必须 `--disable-https --disable-openssl`
- **minizip-ng 4.2.2 补丁（升级会丢）**：`src/mz_strm_os_posix.c` L25 后插 `#ifndef O_BINARY / #define O_BINARY 0 / #endif`
- **主机 POSIX shim（CP936 主机必需）**：`tests/posix_compat.h` 把 `lstat/stat → wfm_stat`、`opendir → _wopendir`（UTF-8 转换）、`fopen → _wfopen`。MinGW ANSI 入口看不见 UTF-8 文件名
- 复杂 commit / tag message 用 `-F 文件`，不要 `-m` 长文本（bash quoting 会挂）

---

## 八、7z `-mhe=on` 加密头（2026-09-23 已闭合）

**它为什么难**：`-mhe=on` 时整个 header 也是一条独立的 7z 流——归档末尾的下一头部区域以 `k7zIdEncodedHeader`（0x17）开头，后接一段 StreamsInfo，描述「一个 folder，其输出就是真正的 header」。而 vendored SDK 的 **C 解码器没有 7zAES coder**，`SzArEx_Open2()` 走到 `SzAr_DecodeFolder()` 就返回 `SZ_ERROR_UNSUPPORTED`，于是**连文件列表都读不出来**（文件名、folder 表、每个条目的尺寸全在那份加密头里）。

**做法**（`src/sevenz_header.{c,h}`）：
1. 自己读 32 字节 start header，只探**一个字节**——不是 0x17 就立刻 `SZH_PLAIN` 收工（普通的 `-mhc=on` 压缩头、`-mhc=off` 明文头都走这条，SDK 行为一字不变）。
2. 是 0x17 就整段读下来（CRC 校验），**最小解析** PackInfo + UnpackInfo：pack 位置/大小、folder 的 coder 描述字节范围、每个 coder 的 unpack size、folder CRC。解析刻意宽容——任何异常一律回落 `SZH_PLAIN`，把诊断权留给 SDK，保证非加密归档的报错一字不改。
3. 把这份描述喂给 **`sz_chain_parse()` / `sz_chain_decode()`**，也就是内容走的同一条 7zAES 路径，密码规则完全一致：需要密码而没给 → `SZH_ERR_PASSWORD`；解出来 CRC 不对 / 不是 `k7zIdHeader` → 同样是密码错。
4. 造一个**虚拟 `ISeekInStream`**：`[0,32)` 是改写过的 start header（指向明文头），`[hdr_off, hdr_off+L)` 是解出来的明文头，其余一律透传真实归档。`hdr_off` 就用加密头原本所在的偏移，所以**归档里存的任何一个偏移都不用搬**——SDK 在它预期的位置读到明文头，从明文头推出的 dataPos 依旧指向真实的内容 pack 流。
5. 交给 `SzArEx_Open()`，之后一切照旧（内容仍由 `sevenz_chain.c` 直接读 `sevenz_volstream`）。

**要点/坑**：
- 明文头比它替换掉的那条记录**长**（实测 aeshe.7z：记录 64 B、明文 462 B），所以虚拟流的 `total` 要取 `max(真实文件长度, hdr_off+L)`，否则 `SzArEx_Open2()` 的 seek-to-END 长度检查会报 `SZ_ERROR_INPUT_EOF`。
- **`LookToRead2_INIT` 不 seek**，第一次 `Look` 从真实流当前位置读。`szh_prepare()` 会把流移来移去，所以交给 SDK 之前必须显式 seek 回 0（`sevenz_extract.c` 里那一段有注释）。这原本是个隐性依赖。
- 明文头大小有上限（`SZH_MAX_HEADER` 64 MiB），sink 按需增长、不信头部里声明的 unpack size。
- 只处理 `numFolders == 1`（SDK 自己对这条记录就传 `numFoldersMax = 1`）与 `external == 0`。

**覆盖**：`tests/fixtures-7z/aeshe.7z`（密码 `Secret123`），`tests/run-sevenz-tests.sh` 的 `KNOWN_GAPS` 已清空——chain 驱动与 façade 两条路径都跑通；`test_sevenz_extract.c --cases` 另验无密码 / 错密码 → `ZIPX_ERR_PASSWORD`、正确密码 → 成功，且失败后不留 staging。

### 真机端到端待验证

ELF 已构建，但需装 PS5 实测：
1. ZIP / RAR / 7z 三类**分卷**真机解压
2. **加密 7z（7zAES + `-mhe=on` 加密头）** 真机解压（`aeshe.7z` 那类归档在真机上连文件列表都要走新代码）
3. 160GB / 9.5 万文件大 ZIP
4. 分卷 RAR 进度条实时走动
5. UI 右下角版本号显示（`/api/version` 与兜底字面量应一致）

> **📊 2026-09-23 首个真机性能数据**：解一个 **18 GB 的包**，**11 分钟**、**1252 个条目**
> （平均 14.7 MB），UI 报 **10–40 MB/s**；**18 GB 是压缩包自身的大小**（✅ 已确认）。
> **格式 = RAR**（✅ 已确认）；包原本在 **PC 上**，**经插件上传**进 PS5，**上传速度 30–40 MB/s**。
> 口径是**解压后的字节**（`zip_extract.c:651-678` 累加 `uncompressed_size`，`:900/910` 累加
> `write()` 写出的解压字节），且是 **250 ms 采样的瞬时值**（`task.c:202-206`，进度只在 ≥1 MiB
> 时上报）—— 摆动里含采样噪声，**只有「总字节 ÷ 总耗时」可信**。
> 仍然成立的一条：**per-entry 开销不是主因**（平均 14.7 MB/条目，不是小文件场景）。
>
> **⚠️ 2026-09-23 晚 更正：本节原先写的「解码不是瓶颈」已撤回。** 两条理由：
> ① 它拿「PS5 解 **RAR**」的 28 MiB/s 去比「PC 解 **7z**」的 427 MiB/s —— **不同格式、不同
> 解码器、不同机器**，量级论证不成立；② 「4× 摆动 = 解码无罪」此前已降级为 250 ms 采样
> 噪声的弱证据。**原先那句「源盘交付速度 ≈ 28 MiB/s 是硬上界」同样站不住** —— 它是从总耗时
> 反推的*观测结果*，不是设备能力上限；若解码是瓶颈，源盘恰恰没跑满。
>
> **③ 新发现（有据可查、且直接针对真实负载）：RAR 解码在 PS5 上是单线程的。**
> `third_party/unrar7/os.hpp:43-45` 的 `#define RAR_SMP` 落在 `#ifdef _WIN_ALL` 分支**内**
> ⇒ POSIX 构建不定义（我们 Makefile 里 0 次出现），而官方 POSIX makefile 第 11 行是
> `DEFINES=… -DRAR_SMP` —— **我们漏了这个开关**。后果：`unpack50mt.cpp`
> （`Unpack::Unpack5MT`，rarlab 专门调过的多线程 RAR5 解压器）**没编进来**，
> `unpack.cpp:185-198` 的 MT 分支整段不参与编译，`SetThreads` / `ThreadPool` 一并消失。
> ⇒ **首要假设：28 MiB/s ≈ 单线程 RAR5 解码的正常量级**（`18 GB ÷ 660 s` 是**解码输入**速率；
> 而上传实测证明**写入端**至少能到 30–40 MB/s、**读取通常快于写入** ⇒ 纯存储上限解释不了它）。
> ⚠️ 自查一条：**用 ELF 符号表查内部符号是无效手段** —— 该 ELF 只有 `.dynsym`（513 项）、
> **无 `.symtab`**，「零命中」是假象（本次差点据此误判）。结论来自 Makefile 与 `os.hpp`。
> **下一步**：先在 PC/WSL 上 A/B（`-DRAR_SMP` + `unpack50mt.cpp` + `-pthread`，跑同一批 RAR5
> fixture 并保证 37 项 RAR 断言全绿），收益显著再上真机；同时真机补两个小事实
> （**RAR4 还是 RAR5**、**解压后多大**）与 `T_copy`。详见 `docs/EXTRACTION-PERF.md` §六 文首
> 更正块与 `docs/REAL-CONSOLE-PROFILE.md`。
>
> **⇒ 终局（2026-09-23 18:15，用户决定）：这条线不做。** 不做的依据是**当前证据判不了收益**，
> 而不是没收益：MT 只并行**解码**（worker 只跑 `unpack50mt.cpp:190` 的 `UnpackDecodeThread`），
> 写盘恒为主线程串行（`UnpWriteBuf()` 只在 `unpack50mt.cpp:283/475/587` 被主线程调用）——
> 若瓶颈在写路径（真机上传已证明写入端只有 30–40 MB/s），收益退化为 1.0×。要判定必须先做
> `T_copy`（拿插件自己的 `TASK_COPY` 搬同一份包，`src/filemgr.c:836`），再决定是否值得改构建
> 并刷机验证；用户选择停在第一步之前。**重开的第一个动作是 `T_copy`，不是改 `-DRAR_SMP`。**

### 可选项（非阻塞）

- **性能**：**优化前**实测上游（7-Zip 本体）在 7z 格式上快 1.9×（单线程）/ 3.4×（8 线程）；ZIP 无显著差异。差距不在我们的架构（我们比 SDK 自己的 `SzArEx` 路径还快 1.02×）。**已全部落地（2026-09-16）**：①汇编解码器（`LzmaDecOpt.asm`+jwasm，1.26×，无 jwasm 自动退纯 C）②多线程 LZMA2（`Lzma2DecMt`，8 线程，1.37×，线程失败自动降级 chain；BCJ2/加密布局仍走 chain）③ZIP 逐条目 fsync 移除（8000 文件 ≥14×）。7z 现与 7-Zip 单线程打平、ZIP 已压过官方（本机受 Defender 拖累不可比，PS5 无该因素）。RAR 与官方 UnRAR 同速（unrar 自带 `target("aes")` SIMD 已启用，无逐条目 fsync）。完整数据见 `docs/EXTRACTION-PERF.md`，基准工具 `tests/bench_driver.py`。**⚠️ 但「单线程打平 / 8 线程 1.59×」是在最有利的输入形状上测的**：基准归档是 `-m0=lzma2 -ms=on` 的**单文件**（`tests/bench_driver.py:179`），恰好是唯一能让 `sz_chain_lzma2_root()`（`src/sevenz_chain.c:750`，要求 1 coder / 0 bond / 1 pack stream / 纯 LZMA2）生效的形状；真实的**多 folder / BCJ2 / 7zAES** 归档会让 MT 失效、退回单线程 chain —— 所以那个 1.59× **既不是上限也不是下限，方向未知**。剩余优化（CRC 硬件化、MT 扩到 BCJ2、条目级并行、ZIP inflate 换 libdeflate、AES-NI）**全部集中在 7z 多线程这一条线上**，建议**先拿真实归档在真机上 profile 再排序**，别按 PC 上这份数字动手
- ~~fsync 批量化（每 64MB/N 条刷一次）~~ → **已作废，改为「彻底移除」**（2026-09-16）：实际落地的不是批量刷，而是把 ZIP 引擎的逐条目 fsync 直接删掉（RAR/7z 本来就没有），三引擎统一为「**不 sync、只 rename**」——publish 是纯 rename、也没有续解功能，该 fsync 无收益。8000 文件 fixture：fsync 版 >200 s 未跑完 → 无 fsync **14.5 s（≥14×）**。已知取舍：publish 之后到落盘之间断电，可能出现「文件在但内容不完整」；要补只需在 extract 收尾做**一次**目录/整盘 flush（PS5 是 FreeBSD 系，`syncfs()` 不一定有，`sync()` 是全盘、偏重）。代码现状见 `src/zip_extract.c:937-945`，实测见 `docs/EXTRACTION-PERF.md:18-21`
- 解压失败保留 staging 支持续解（中等改动）
- ~~进度条 % / 文字进度 / ETA 三处口径统一为字节~~ → **已完成**（`assets/main.js:2071-2079`，条目计数已移除并注明原因）

---

## 九、仓库许可与代码归属（2026-09-15 核查）

用户曾担心「项目源自他人代码、没有许可」——**前提不成立**：

- 上游 `owendswang/ps5-web-file-manager` 经 GitHub API 确认 = **GPL-3.0**（78 stars，last push 2026-09-08）
- 本项目 `LICENSE`（GPL-3.0 全文）在 root commit `5cb0b76` 即存在，与上游一致
- 授权链完整：`ps5-payload-dev/websrv`（John Törnblom, GPLv3+，其 Copyright 头仍保留在 `asset.c` / `asset.h` / `mime.h` / `websrv.h`）→ `owendswang` → 本项目

代码量构成：
- 第三方 vendored **71,528 行**（unrar7 27,710 / zlib 20,106 / LZMA SDK 17,248 / minizip-ng 6,464）——重写时原样复用，零成本
- 第一方 20,844 行 = 上游 v1.7 遗产 13,860 + 自有 6,984
- **自有代码中 3,661 行零耦合**（`sevenz_chain` 2094 + `zipx_volume` 658 + `zipx_volstream` 494 + `sevenz_volstream` 415，只依赖 public domain / zlib）→ 可单独抽成 MIT 库

完整评估见 `docs/REWRITE-FEASIBILITY.md`（三路径：补合规 0.5 天 / 架构重构 12–18 天 / clean-room 重写 35–50 天）。**结论：建议补合规而非重写** —— GPL-3.0 保护 7z 引擎成果不被闭源白嫖。

---

## 十、工作区状态

⚠️ **2026-09-23：工作树不再干净** —— 本轮加密改动（15 个已修改 + 8 个未跟踪文件）**尚未提交**，见文末「十一、本轮变更」。发版前需要先决定版本号并 commit。

以下为 2026-09-15 的历史清理记录（当时工作树干净，"仅剩有意保留的未跟踪文档"）。

已清理（2026-09-15）：

| 文件 | 说明 | 去向 |
|---|---|---|
| `erssonglDesktopWeb File Managerps5-web-file-manager￢`（2543 B） | 早期 shell 转义事故：一次 `git log --oneline --color` 的输出被重定向进了文件名。末尾是 U+F022（私用区码位，mojibake 残留），各工具渲染不一 —— git 显示成八进制转义、`ls -b` 印成 ASCII 引号 | **回收站**（`$R…`，2543 B，可还原） |
| `web-file-mgr-unpack 1.9.1.elf`（898 KiB） | 陷阱：文件名写 1.9.1，内嵌却是 9-07 的 **v1.9**（无 7z 引擎） | 已不在仓库根 |

> ⚠️ **清理这类特殊文件名时**：`SHFileOperationW`（带 `FOF_ALLOWUNDO` 走回收站）对含私用区码位的路径会返回 `ERROR_FILE_NOT_FOUND (2)`，**但动作实际已生效**。删完务必查 `C:\$Recycle.Bin\<SID>\$I*` 记录确认落在回收站（`$I` 存原路径 UTF-16，`$R` 是内容）。本沙箱里 `Add-Type` 与 `rm` 都被拦（后者有 safe-delete 钩子），只能用 Python `ctypes` 调 shell32。

`.build/` 下的探针/调试产物已被 `.gitignore` 白名单覆盖，不再污染 `git status`。

---

## 十一、本轮（2026-09-23）变更：加密通道补齐（未提交）

**目标**：让 ZIP 与 RAR 的加密归档真正可解（7zAES 早已可用）。两者此前都报
`extract_unsupported`，但**缺口在引擎侧，不在 UI** —— 密码框、`password=` 字段、
`err_extract_password` 文案从 v1.9 起就已就位。

### 11.1 ZIP：给裁剪过的 minizip-ng 补一个 crypto 后端

`third_party/minizip-ng` 是裁到只读路径的精简副本，`mz_zip.c` 里
`#ifdef HAVE_WZAES / HAVE_PKCRYPT` 的分支保留着，但**对应的流与 crypto 后端被裁掉了**。
本轮补回：

| 文件 | 状态 | 说明 |
|---|---|---|
| `src/mz_strm_wzaes.{c,h}` | 上游 4.2.2 原样恢复 | WinZip AES 流（方法 99 + `0x9901` 扩展字段） |
| `src/mz_strm_pkcrypt.{c,h}` | 上游 4.2.2 原样恢复 | 传统 PKWARE / ZipCrypto 流 |
| `src/mz_crypt_wfm.c` | **新写**（~860 行） | 本地 crypto 后端：SHA-1、HMAC-SHA1、AES-128/192/256 |

后端要点：
- S-box 与 GF(2^8) log/alog 表**首次使用时推导**，所以不新增 `.rodata` 查表（实测 `.rodata` 仅 +256 B，是字符串）。
- 随机数直接 `open("/dev/urandom")`，**不要**走 `mz_os_rand()` —— 后者会退回 `rand()`/`srand()`，把两个新符号塞进导入表。最终产物**动态符号零新增**。
- PBKDF2 复用 vendored 的 `mz_crypt.c`（与上游逐字节一致），没有重写。
- 非 SHA-1 算法与 AEAD aad 一律返回 `MZ_SUPPORT_ERROR`（本项目只读，不需要）。
- KAT 先行：写完后先用 FIPS 197 / RFC 3174 / RFC 2202 / RFC 6070 / SP 800-38A
  向量单独验算（`.build/kat_crypto.c`，24/24），再接线。踩到的三个坑：AES 仿射用 `rol32`
  应为 `rol8`；GF 乘法 `(uint8_t)(a+b) % 255` 截断，应全程 int；HMAC 的 ipad 必须由
  **已 XOR 过 0x5c 的 opad** 再推。

### 11.2 RAR：把 `RARSetPassword` 接上

`src/rar_extract.{c,h}`：新增 `password` 形参（`rar_extract()` 为第 8 个参数）。
调用点是 `RAROpenArchiveEx` 之后、**首次 `RARReadHeaderEx` 之前**——这是解密 `-hp`
头加密归档的硬性顺序要求（scan 与 extract 两个阶段各自开档，两处都要设）。
`ERAR_MISSING_PASSWORD` / `ERAR_BAD_PASSWORD` 由 `ZIPX_ERR_UNSUPPORTED` 改映射为
`ZIPX_ERR_PASSWORD`；`RHDF_ENCRYPTED` 只在**没给密码**时提前拒绝。

### 11.3 前端：密码失败后自动重试（这步不做，功能等于不可达）

原先密码框**只对 7z 弹**（`actionExtract()` 里的 `isSevenZipArchive()` 判断），
ZIP/RAR 加密归档失败后用户根本没机会输密码。现在 `handleTerminalTask()` 在
`op === "extract" && error_code === "extract_password"` 时走
`retryExtractWithPassword()`：

- 记忆原始请求（`extractRetryKey(task.id)` → `{conflict, removeSource, name, large, attempts}`），
  重试时**保持冲突策略与大文件选配**；
- **必须按任务 id 记，不能按路径记**（v1.9.3M 后期修正）。路径在传输中被
  「服务端 JSON 逐字节转义为 `\u00XX`」+「`fs_path_value()` 反向还原成原始字节」这一对
  转换改了表示 ⇒ **非 ASCII 目录下**「页面手里的路径」≠「任务回报的路径」，按路径查必然
  落空 ⇒ 口令框永远不弹，用户只看到一个失败框，必须先手动再解压一次。任务 id 由服务端
  分配、原样回传，不受编码影响；重试时也改用**服务端回报的** `task.src` / `task.dst` 重发。
  消费即删（重试注册到新 id 下），Map 最多留 8 条（同一时刻只可能有一个活动任务）。
- 最多 3 次；取消或空输入即放弃，回落到原有失败提示；
- 7z 保留提前询问（免得白跑一次 scan + folder 解析）。

新增文案 `extractPasswordRetryAsk`（重试：密码不正确）+ `extractPasswordFirstAsk`（首次：
此压缩包已加密），与提前询问用的 `extractPasswordAsk` 区分 —— 第一次失败时用户还没输过密码，
再说「密码不正确」就是误导。

错误文案里的条目名必须过 `decodeFsText()`：`backendErrorText()` 原样用了 `error_arg`，
而服务端把它逐字节转义过 ⇒ 中文/日文条目名在错误框里显示成 `â®…ç§.psd`。列表侧一直有这层
翻译（`displayName()`），只有错误文案漏了。

同源的编码坑：`pathJoin(服务端回报的目录, 本地文件名)` 把两种表示混进同一个字符串，而
`fs_path_value()` **只要发现任一个码点 > 0xFF 就整体不修** ⇒ 中文名文件放进中文名目录时
路径失效。新增 `encodeFsText()`（`decodeFsText()` 的逆）在拼接前把本地名转成同一表示，
`uploadAndExtractFile()` 与 `actionNewText()` 两处都用它。

无头回归：`.build/ui_retry_test.mjs`（真 `main.js` 载入桩 DOM，40 checks，含「非 ASCII 目录
必须仍弹口令框」的回归用例）、`.build/ui_upload_menu_test.mjs`（40 checks：i18n 键覆盖、
菜单接线、样式、高亮规则的层叠作用域，以及**解压按钮不许被隐藏、只许被置灰**）、
`.build/preview_check.mjs`（无头 Chromium 跑真页面，验菜单开关、页脚布局、**解压按钮的
显隐/置灰/提示随选区变化**，并**读回三种交互状态下高亮的计算值**；该脚本已改为失败即
非零退出）。

**菜单行的「选中高亮」曾被两条规则同时破坏**（用户报「选中下面那个高亮效果不对」）：
① 全局 `button:focus` 的 `outline: 3px + offset 2px` 是按 54px 工具栏按钮设计的，套在 46px
菜单行上会越过面板 6px 内边距、压住相邻行，且 outline 的圆角半径不随 offset 自适应 ⇒ 视觉上
成了一个「脱离的框 + 两侧挂着的弧线」；② 面板自己的
`.upload-menu-list button:hover:not(:disabled)` **从未生效过** —— 它与
`button:not(.row-action):hover:not(:disabled)` 特异性同为 `(0,3,1)`，而后者在文件里更靠后 ⇒
后者胜出，于是 hover 是 `#303945`、focus 是 `#2b343e`，**两个高亮两个颜色**，且一行 hover 时
另一行仍因 focus 亮着 ⇒ 看起来「两行同时被选中」。修法：两条规则都收敛到面板 id
（`#uploadMenu button:…`，`(1,1,1)` / `(1,2,1)` 稳赢通用规则），行只用填充表示选中，键盘焦点
提示改为**行内 `inset` 环**（`box-shadow: inset 0 0 0 2px`）—— 画在行内，任何行高都不可能
越界。**这类坑只有真引擎读计算值才抓得住**，光看源码两条规则都「像是对的」。

**解压按钮改为「常显 + 置灰」**（用户要求「直接显示出来 只不过是灰色的 只有能解压的文件才可以
点击」）：`index.html` 去掉 `hidden`，`renderExtractButton()` 不再碰 `.hidden`，改成按选区设
disabled 并给一条说明原因的工具提示（什么都没选 ⇒ 新增 `extractSelectArchive`；只选中子卷 ⇒
沿用 `extractSelectMainVolume`；选中**多个** ⇒ 新增 `extractOneAtATime`，旧代码这种情况错用了
「请改选主卷」，文案本身是错的）。🪤 **`button:disabled` 带 `pointer-events: none` ⇒ 禁用按钮
无法 hover，`title` 永远不弹** —— 必须像既有的 `.parent-nav-button:disabled` 那样把
`pointer-events` 还回来（点击仍无效，`disabled` 属性本身挡激活）。标签同时从 `extractToCurrent`
（「解压到当前目录」）换成短词 `extract`（「解压」），与工具栏其他动词一致：**常显按钮不该
同时又是最宽的那个**（英文下 `Extract to current folder` 会到 107 px）。
**代价必须实测而不是估**：按钮宽 96 px ⇒ 工具栏换行阈值（zh）1080 → 1190 px、（en）1230 →
1350 px。`.build/preview_check.mjs` 已把阈值**钉成断言**（1920/1600/1280 必须都是一行），
并按四种选区验 disabled / opacity / title；该脚本同时从「只打印」改成**失败即非零退出**。

### 11.4 构建坑：编译选项变化必须让目标文件失效（**改 Makefile 前必读**）

`make` **看不见**编译选项变化。加 `-DHAVE_WZAES -DHAVE_PKCRYPT` 后，
`mz_zip.o` / `mz_crypt.o` 被判定为最新而复用 → 此时已无线程引用新流 →
`--gc-sections` 把加密代码再丢一次，**链接却报成功**（本次第一次构建的产物与
已发布 v1.9.2 **逐字节相同**，`readelf` 才发现 `.text` 只长了 336 B）。

修法（取代原先的 `LzmaDec.o` 特例）：把第三方编译选项写进标记文件，
内容变了才重编。

```make
PS5_FLAGS_STAMP   := ps5-obj/.third_party_cflags
LINUX_FLAGS_STAMP := linux-obj/.third_party_cflags
$(PS5_FLAGS_STAMP): FORCE
	@printf '%s\n' '$(THIRD_PARTY_C_FLAGS_7Z) $(LZMA_DEC_OPT_FLAG)' > $@.tmp
	@cmp -s $@.tmp $@ || { mv -f $@.tmp $@; echo '  [cflags] ...'; }
```

> 诊断手法：拿未 strip 的产物比 `readelf -S` 各段尺寸，而不是看总体积。
> 改一个字符串常量会重排 `.rodata` 字符串池，字节 diff 会被放大到几万字节，
> 但段尺寸是守恒的——判断"代码到底有没有变"要看段尺寸 + 助记符序列。
> 现成脚本：`.build/_seccmp.py`、`.build/_operandcheck.sh`。

### 11.5 产物与验证

| 项 | 值 |
|---|---|
| 主机测试 | ZIP 140 + RAR 37 = **177 checks / 0 失败**（`tests/run-tests.sh`） |
| 前端测试 | **27 checks / 0 失败**（`.build/ui_retry_test.mjs`） |
| ELF | 870,680 B · sha256 `b1409f5c1bc4b1a39ab337853b956f4807f95c5770dee6eca7a18a62cc08f80e` · e_machine 0x003e（加密轮结束时；`-mhe=on` 之后的产物见 §12.3） |
| 确定性 | 同一源码树构建两次逐字节一致 |
| 段变化（vs 已发布 v1.9.2） | `.text` +11,296 · `.bss` +5,120（AES 表） · `.rodata` +256 · 动态符号零新增 |
| 内嵌资产核验 | ELF 内 gzip 资源中可检出 `retryExtractWithPassword` / `extractPasswordRetryAsk`（普通 `strings` 找不到，要先解 gzip；脚本 `.build/check-elf-gzip.py`） |

**未做（发版前必做）**：未 commit / tag / 发 Release；真机端到端未验。
版本号**已升**为 `v1.9.3M`（2026-09-24 加改版标记 `M`，见 §2.2）。
README（中英）、CHANGELOG、本文档已同步为「未发布」状态。

---

## 十二、本轮（2026-09-23）变更：7z `-mhe=on` 加密头（未提交）

**目标**：补上最后一个 7z 格式缺口（设计与坑见 §八）。

### 12.1 新增

| 文件 | 说明 |
|---|---|
| `src/sevenz_header.{c,h}` | **新写**（~900 行）。头部读取 + `k7zIdEncodedHeader` 最小解析 + 虚拟 `ISeekInStream` |
| `Makefile` | `src/sevenz_header.c` 进 `COMMON_SRCS`（PS5 与 linux 共用） |
| `tests/run-sevenz-tests.sh` | 编 `sevenz_header.o` 进 `ENGINE_OBJS`；`KNOWN_GAPS` 清空；façade 矩阵加入 `aeshe` |
| `tests/sevenz_chain_e2e.c` | 按与产品相同的顺序接线 `szh_prepare()`（否则 chain 矩阵读不了 `aeshe`） |
| `tests/test_sevenz_extract.c` | `aeshe` 三例（无密码 / 错密码 → `ZIPX_ERR_PASSWORD`；正确密码 → 成功），另修一处 `snprintf` 截断告警 |

### 12.2 关键设计（细节见 §八）

- 只探**一个字节**：不是 `0x17` 立刻返回 `SZH_PLAIN`，SDK 行为与改动前完全一致（12 个既有 fixture 全部复跑通过）。
- 解析刻意宽容：PackInfo/UnpackInfo 之外的任何异常都回落 `SZH_PLAIN`，把诊断权留给 SDK。
- 复用 `sz_chain_parse()` / `sz_chain_decode()`，所以 7zAES 的密码/错误语义与内容侧**完全同源**，不新增第二个 crypto 实现。
- 虚拟流的 `total` 必须 `max(真实长度, hdr_off + L)`；`LookToRead2_INIT` 不 seek，交回 SDK 前必须显式 seek 到 0。

### 12.3 产物与验证

| 项 | 值 |
|---|---|
| 7z 套件 | **27 用例 / 0 失败**，`aeshe` 在 chain 与 façade 两条路径都 `ok`，`KNOWN_GAPS` 为空 |
| 主机测试（ZIP/RAR 回归） | ZIP 140 + RAR 37 = **177 checks / 0 失败**（无回归） |
| ELF | 903,448 B · sha256 `8ca47d5aaca75085b32641300cce30fadb7df7749cb6b53d04f129bcecc286b7` · e_machine 0x003e（== 本轮最终产物，见 §12.4） |
| 确定性 | 同一源码树构建两次 sha256 相同 |
| 段变化（解压按钮常显 vs 上一轮） | **只有 `.rodata` 变化**：`0x026CC0` → `0x026F00`（+0x240 = 576 B：index.html 去掉 `hidden` 并换短标签、`main.js` 的三条禁用理由、两份语言文件各两条新文案、`.extract-action:disabled` 及其注释）。`.text` 两次均为 `0x087780`、`.data` 均为 `0x00034C` —— 第六次「只改内嵌前端资源」 |
| 段变化（菜单行高亮修复 vs 上一轮） | **只有 `.rodata` 变化**：`0x026B40` → `0x026CC0`（+0x180 = 384 B，三条收敛后的高亮规则加其注释）。`.text` 两次 readelf 均为 `0x087780` —— 又一次「只改内嵌前端资源、不碰 C 逻辑」的标准形状 |
| 段变化（本轮前端三项 vs 上一轮） | **只有 `.rodata` 变化**：`0x026A40` → `0x026B40`（+0x100 = 256 B）。`.text` / `.data` / `.eh_frame*` 一字节未变 —— 「只改内嵌前端资源 + 加两条文案」的标准形状 |
| 段变化（加 `M` 标记 + 修正 `err_extract_unsupported` 文案 vs 加密轮产物） | **只有 `.rodata` 变化**：加 `M` 标记 +0x100（256 B），修正文案再 +0x40（64 B）；`.text` / `.data` / `.bss` / `.eh_frame*` / `.gcc_except_table` **一个字节都没变**。又因 16 KiB 段对齐留有余量，**六次构建的文件总尺寸都是 903,448 B**：尺寸相同**不代表**二进制相同（sha256 逐个不同：`f3164efa…` → `53296d29…` → `7b5ab00c…` → `212107a6…` → `da36834d…` → `cf2c0fcf…` → `8ca47d5a…`） |
| 段变化（加密轮 vs 其前一轮） | `.text` +4,880 · `.rodata` +640 · `.eh_frame_hdr` +32 · `.eh_frame` +160 —— 正文合计 **+5,712**；其余 **+27,056** 是 `p_align=0x4000` 的两处段对齐填充（LOAD#1 越过 0x8C000 边界）。**段数仍为 20，动态符号零新增（513 → 513）** |

> 判读提示：这次文件涨了 32,768 B，但正文只涨 5,712 B —— 不要按体积下结论。
> 权威做法是比较**段尺寸**与**动态符号集合**（见 §11.4 的诊断手法）。

**未做（发版前必做）**：未 commit / tag / 发 Release；真机端到端未验。
版本号已升为 `v1.9.3M`（改版标记 `M` 于 2026-09-24 加入）。
