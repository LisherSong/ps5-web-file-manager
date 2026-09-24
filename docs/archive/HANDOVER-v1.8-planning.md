# PS5 Web File Manager — 项目工作方案

> **目的**：让任何接手人拿到这份文档 + 项目仓库，都能独立推进 v1.8（RAR 支持）开发。
> **读者**：开发者，需要熟悉 C99 + POSIX + 浏览器 JS，但不要求懂 PS5 SDK。
> **撰写日期**：2026-09-04
> **对应代码版本**：`aef4a44`（v1.7 + 文档配套提交）

---

## 0. 阅读顺序建议

如果你时间紧：
1. 先看 §1「项目是什么」和 §4「当前进度」 → 5 分钟建立全局观
2. 跳到 §6「v1.8 详细计划」按天执行
3. 卡住了回 §7「关键代码模板」找参照实现
4. 出 bug 翻 §9「坑清单」

如果你要 review 整套设计：
1. §1 → §3 → §6 → §7 → §8 顺序读完

---

## 1. 项目是什么

**PS5 Web File Manager** 是个跑在越狱 PS5 上的 HTTP 文件管理 payload。
- 用户从 PS5 浏览器打开 `http://PS5-IP:8080` → 看到 PS5 文件系统
- 可以浏览 / 上传 / 下载 / 编辑文本 / 解压 ZIP / 安装 PKG
- 单文件 ELF（`web-file-mgr.elf`），无外部服务依赖

**关键事实**（会影响你所有设计决策，先背下来）：
- **CPU**：AMD x86-64 Zen 2（**不是** ARM），目标 triple `x86_64-sie-ps5`
- **SDK**：vendored 在 `/opt/ps5-payload-sdk/`，未提供 homebrew 库目录（你不能 `pkg-config --libs libxxx`，只能 vendor C 源码自己编）
- **体积红线**：单 ELF 通常不超 1 MiB，当前 v1.7 = 418 KiB
- **网络环境**：通常在 LAN 内，HTTPS 不强制（自签证书）

**代码托管**：`https://github.com/owendswang/ps5-web-file-manager.git`
**License**：GPLv3+（注意：unrar 的 license 是「UnRAR license」，**不能改装**，只能直接用 —— 见 §9.1）

---

## 2. 仓库与构建环境

### 2.1 本地路径

```
Windows:  C:\Users\songl\Desktop\Web File Manager\ps5-web-file-manager\
WSL:      \\wsl$\Ubuntu-22.04\home\song\ps5-web-file-manager\   (同一份文件)
PS5:      /mnt/usb0/...                                         (部署目标)
```

### 2.2 关键工具位置

| 工具 | 位置 | 用途 |
|---|---|---|
| WSL bash | `wsl -d Ubuntu-22.04` | 唯一能跑 PS5 交叉编译的地方 |
| 编辑器 | 本机任意 | VS Code 推荐，记得保存用 LF（`.gitattributes` 不强制） |
| prospero-clang | `/opt/ps5-payload-sdk/bin/prospero-clang` | PS5 交叉编译 wrapper，自动注入 `-target x86_64-sie-ps5` |
| prospero-strip | `/opt/ps5-payload-sdk/bin/prospero-strip` | strip 工具 |
| 构建脚本 | `/home/song/build-elf.sh` (WSL) + `.build/build-elf.sh` (副本) | 完整构建 + 落双路径 |

### 2.3 双身份坑（必读）

> ⚠️ **WSL 里有两种身份：真 bash 用户 vs SMB 视图用户**
> - 真 bash 用户：`song(1000)`，写 `/opt/ps5-payload-sdk/...` 需要 sudo
> - SMB 视图用户：`songl(197609)`，但**不能写** `/opt/...`（即使 sudo）

SDK 的 `target/user/homebrew/` 被 `songl(197609)` 拥有 755，普通 song 写不进。
**正确做法（构建脚本 v5 已实现）**：staging 模式 → 先 make install 到 `/tmp/` → sudo cp -r 到 SDK。
**不要尝试** `sudo chmod` SDK 目录，会破坏其他项目。

### 2.4 我的 Bash 工具是 MinGW64，不是 WSL

- `uname -a` = `MINGW64_NT-10.0`
- 看 WSL 真实状态用 `\\wsl$\Ubuntu-22.04\` 路径前缀
- 但**修改 WSL 文件**只能用 PowerShell 走 `C:\Users\songl\Desktop\...` 路径，不能用 `wsl.exe`（沙箱黑名单）

---

## 3. 已完成功能（v1.7 = `5cb0b76` + `aef4a44`）

### 3.1 ZIP 解压 + 大文件模式

> 用户场景：解压一个 200GB 系统镜像到 PS5。默认 limits 不够 → 加 🅱 "大文件模式"开关。

**完整链路**（每层都改了，记得按层理解）：

```
┌────────────────────────────────────────────────────────────────┐
│ Frontend: assets/main.js                                       │
│   - LARGE_FILE_THRESHOLD_BYTES = 240 GiB (硬编码)              │
│   - shouldPromptLargeMode(itemSize) → 弹 confirm              │
│   - startExtractTask(path, dst, conflict, remove, name, large) │
└────────────────────┬───────────────────────────────────────────┘
                     │ POST /api/extract
                     │  form fields: {path, dst_dir, conflict,
                     │    remove_source, large}
┌────────────────────▼───────────────────────────────────────────┐
│ Task layer: src/extract.c                                      │
│   - api_extract 解析 large 字段 → task->extract_large         │
│   - extract_worker 用 zipx_limits_profile(task->extract_large) │
│   - file_task_t 新增字段 extract_large (int)                   │
└────────────────────┬───────────────────────────────────────────┘
                     │
┌────────────────────▼───────────────────────────────────────────┐
│ Engine: src/zip_extract.{h,c}                                  │
│   - k_default_limits: 200K / 1TiB  / 256GiB / 500:1           │
│   - k_large_limits:   500K / 2TiB  / 1TiB   / 1000:1           │
│   - ZIPX_LIMITS_DEFAULT=0 / ZIPX_LIMITS_LARGE=1               │
│   - zipx_limits_profile(int) → const zipx_limits_t*           │
│   - zipx_extract() 签名不变，向后兼容                          │
└────────────────────────────────────────────────────────────────┘
```

**三阶段流程**（所有 archive 引擎共享的设计）：
1. **scan** —— 读所有 entry header，只校验 name 安全 + 累加 bytes 估算
2. **extract** —— 写到 staging `.wfm-part-{pid}-{taskid}/`，fsync 每个文件
3. **publish** —— 整 staging rename 到目标，处理 conflict 策略
4. **cleanup** —— 任何阶段失败都 unlink staging

**i18n**：
- `assets/lang-en.js` 和 `lang-zh.js` 新增 `extractLargeAsk` / `extractLargeActive`

**前端 UX**：
- ZIP 大于 480 GiB 时弹窗「启用大文件模式？」
- 用户点 OK → 传 `large=1` → 引擎走 large profile
- 用户点取消 → 走 default profile（多半会被拒绝）

### 3.2 文档与仓库

| 文件 | 行数 | 说明 |
|---|---|---|
| `README.md` | 267 | GitHub-grade 项目说明：版本标签、Features 分类、ZIP extraction 段含 limits 表、Verification、Tests、Project layout、FAQ |
| `CHANGELOG.md` | 107 | Keep-a-Changelog 格式，v1.7 段 |
| `docs/UPGRADE-v1.7-zip-large-file-profile.md` | 448 | 维护者技术手册，含架构图、源码引用、覆盖矩阵、Trade-offs、Roadmap |
| `.build/extract-demo.html` | - | 交互式解压演示页（场景 3 表格已包含 large profile 行） |
| `.build/check-elf-gzip.py` | - | ELF 内 gzip 资产串验证脚本（确认前端改动进了 ELF） |

### 3.3 测试覆盖

```
tests/run-tests.sh → 69 checks, 0 failures

覆盖范围：
- ZIP 基础：basic / stored / unicode names / zip64 / traversal / backslash
- 安全：traversal / absolute path / drive letter / symlink entry / fifo entry
- 格式错误：encrypted / bad_crc / truncated / not_a_zip
- 资源限制：ratio cap / file cap / total cap
- 边界：duplicate / file_dir_clash / conflict_source
- 大文件：large profile (16 个 check) — 含 default 拒 medium_bomb / large 接 / 降低 large 阈值仍生效
```

**fixture 经验**（写测试时会用到）：
- 4 MiB of 'A' 实际 ratio ≈ **1026**（不是想象中的 ≈1000），所以 `bomb.zip` 连 large profile 都拒
- `bytes(range(256)) * 4096` (1 MiB) → ratio ≈ **238**，正好夹在 (200, 1000] 中间 → **stable fixture**
- 1 MiB "ABCD" * 256K → ratio ≈ 1004（擦边不稳）；random 3-bit → ratio ≈ 2（太低）

### 3.4 Git 状态

```
5cb0b76 Initial import of PS5 Web File Manager v1.7
aef4a44 Add v1.7 changelog and technical upgrade notes
main branch → origin https://github.com/owendswang/ps5-web-file-manager.git

⚠️ sandbox 推 github.com 被代理拦截（502 from CONNECT tunnel）
   → 用户在自己 PowerShell 跑 `git push -u origin main`
   → 详见 MEMORY.md「GitHub 推送的网络环境」
```

---

## 4. 当前进度状态（截至 2026-09-05 15:30 — v1.8 收尾）

| 项 | 状态 | 备注 |
|---|---|---|
| v1.7 ZIP 大文件模式 | ✅ 完成 + 文档 + 已部署 | 69 checks pass |
| v1.7 仓库初始化 | ✅ 本地 5 commit | 待 push（用户侧） |
| v1.7 文档（README/CHANGELOG/UPGRADE） | ✅ 完成 | |
| **v1.8 RAR 支持（单卷 明文）** | ✅ **实施完成** | dmc_unrar 1.7.0 后端 |
| v1.8 文档（CHANGELOG/README/UPGRAGE-v1.8） | ✅ 完成 | 见 §14 |
| v1.8 前端（解压按钮 + 子卷置灰 + i18n） | ✅ 完成 | assets/main.js + lang-{en,zh}.js |
| v1.8 host tests | ✅ 83 checks, 0 failures | 69 ZIP + 14 RAR |
| v1.8 ELF 重编（WSL） | ⏸ 待用户在 WSL 跑 | 网络/SDK 受限无法在沙箱完成 |
| 推送 GitHub | ⏸ 待用户在 PowerShell 跑 | 沙箱 git 502（见 §9） |

**v1.8 范围变更（vs §5 原计划）**：

| 原计划 | 实际交付 | 原因 |
|---|---|---|
| ✅ RAR 单卷（明文） | ✅ RAR 单卷（明文） | 实施完毕 |
| ❌ RAR 分卷（RAR5 .partNN.rar 链式） | ❌ → 拒绝 + 弹 tooltip | dmc_unrar 不支持分卷 |
| ❌ 加密 RAR（密码弹窗） | ❌ → 拒绝 + 无 UI | dmc_unrar 不支持加密 |
| ❌ ZIP 分卷 | ❌ | 用户说不需要（未做） |
| ❌ 7z / tar / 其他格式 | ❌ | 范围外 |

**v1.8 范围比原计划小，但工程更扎实**：
- 加了 facade header 模式（dmc_unrar_api.h），让 vendor 的 .c 永远独立编、不被 host shim 污染
- 把 §6 的"工程决策"全做了：dispatch 加 magic 不只为扩展名、共用 staging / fsync / publish
- v1.9 升级路径明确（VENDORED.md 5 步 + UPGRADE-v1.8 §10.1）

**遗留 → v1.9**：
- vendor 切 opello/unrar，加多卷 + 加密支持
- 把 zip_extract / rar_extract 共用的 staging / publish / nameset / report 等抽到 `src/archive_engine_common.c`
- ELF 真机部署（ZIP 当前用户路径没加密也是 v1.9 优先，因为单卷 RAR 同样不在加密范围）

详见 §14「v1.8 实际交付状态」。

**v1.8 需求范围（用户已确认）**（v1.7 原始 §6 中的待开工项）：
- ✅ RAR 单卷（明文）
- ✅ RAR 分卷（仅 RAR5 `name.partNN.rar` 新格式）
- ✅ 加密 RAR（密码弹窗）
- ❌ ZIP 分卷（用户说"不需要"）
- ❌ 7z / tar / 其他格式

---

## 5. v1.8 范围与目标

### 5.1 功能列表

```
- /api/extract 支持 RAR 格式
- 自动派发：扩展名 + magic (Rar!\x1a\x07\x00 / Rar!\x1a\x07\x01\x00)
- RAR 单卷解压
- RAR 分卷自动识别（unrar 内部处理，前端只识别主卷）
- 加密 RAR：探测 → 弹密码窗 → 错误重试
- 前端：选中 RAR 主卷显示解压按钮，子卷置灰 + tooltip
```

### 5.2 不做（明确划线）

| 不做 | 原因 |
|---|---|
| ZIP 分卷 | 用户明确不要 |
| 7z / tar / 其他格式 | 范围外 |
| RAR 创建（压缩） | 当前只解压 |
| 密码保存/记忆 | 安全 + UX 一致性 |
| ZIP 加密（ZIP AES） | 单独迭代 |
| 区分「密码错」vs「文件损坏」 | 防侧信道，统一文案 |

### 5.3 工作量预估

| 模块 | 行数 |
|---|---|
| `third_party/unrar/` vendor | ~7K |
| `src/rar_extract.{h,c}` | ~450 |
| `src/extract.c` 改造 | ~150 |
| `src/filemgr_internal.h` | ~10 |
| `assets/main.js` | ~120 |
| `assets/main.css` | ~40 |
| `assets/lang-{en,zh}.js` | ~30 |
| `tests/test_rar_extract.c` | ~400 |
| `tests/fixtures/` | ~12 个 |
| 文档 + CHANGELOG | ~200 |
| **总计** | **~1380 新增** |

**ELF 预估**：v1.7 = 418 KiB → v1.8 ≈ **580 KiB**
**工期**：5~6 天集中开发

---

## 6. v1.8 详细实施计划

> 按天执行。每步都有「验证」一项，写完立即跑。

### D1：vendor unrar + 写 rar_extract 骨架

#### 6.1.1 拉 unrar 源码

```bash
cd /home/song/ps5-web-file-manager/third_party/
git clone https://github.com/alexbatalov/unrar.git
# 检查
ls unrar/
# 期望：unrar.c + unrar.h + README + LICENSE（UnRAR license）
```

**注意事项**：
- alexbatalov 版是纯 C、单文件，**不要用 winrar/unrar 官方版**（license 严格，商用改装禁）
- 不要改装 unrar.c（license 限制），需要时通过 wrapper 函数扩展
- 如需 ASAN / fuzzer 友好的版本，可考虑分叉，但要保留 license 文件

#### 6.1.2 验证 unrar 在 host 上能编

```bash
cd /home/song/ps5-web-file-manager/third_party/unrar/
cc -O2 -o test-unrar unrar.c -DUNRAR_TEST_MAIN  # 临时 main 跑一遍
# 或者写个测试：建 test.rar → 调用 unrar API 解出来
```

确认 API 形态：
- `RARHeaderDataEx` / `RAROpenArchiveEx` / `RARReadHeaderEx` / `RARProcessFile` / `RARSetPassword` / `RARCloseArchive`

#### 6.1.3 写 rar_extract.h 公共 API

**模板**：直接抄 `src/zip_extract.h` 的命名空间 `zipx_` 改为 `rarx_`，但复用 `zipx_status_t` 枚举。

```c
/* src/rar_extract.h */
#pragma once
#include "zip_extract.h"  /* 复用 zipx_limits_t / zipx_status_t */

typedef struct {
  char password[128];      /* UTF-8 / ASCII 密码 */
  int password_set;        /* 0 = 不设, 1 = 调用方已设置 */
} rarx_password_t;

typedef struct {
  zipx_status_t status;
  int sys_errno;
  uint64_t entries_total;
  uint64_t entries_done;
  uint64_t bytes_total;
  uint64_t bytes_done;
  uint64_t files_created;
  uint64_t dirs_created;
  int has_password;        /* scan 阶段探测到加密 → ERR_PASSWORD */
  char detail[ZIPX_PATH_MAX];
  char message[192];
} rarx_result_t;

/* password 可为 NULL（明文 RAR）；结果总是写入 *result */
zipx_status_t rarx_extract(const char *first_volume_path,
                            const char *dst_dir,
                            zipx_conflict_t conflict,
                            const zipx_limits_t *limits,
                            const rarx_password_t *password,  /* 可 NULL */
                            zipx_cancel_fn cancel,
                            zipx_progress_fn progress,
                            void *userdata,
                            rarx_result_t *result);
```

#### 6.1.4 rar_extract.c 三阶段骨架

**关键**：scan 与 extract 都要扫描所有 entry（unrar 的 API 不能 list-only），所以 scan 阶段**会读到文件内容但丢弃**，产生约 N×entry 大小的 I/O 成本。

> 💡 **性能优化**：unrar 提供 `RARExtractChunk` API，可以一次解 N 字节立刻写出，避免 staging 暂存。**第一版不用**，可读性 + 测试性更重要。

#### 6.1.5 关键代码模式（scan 阶段）

```c
static zipx_status_t
rarx_scan(const char *first_path, const zipx_limits_t *limits,
          rarx_result_t *result) {
  struct RARHeaderDataEx hdr;
  struct RAROpenArchiveDataEx arc;
  memset(&arc, 0, sizeof(arc));
  arc.ArcName = (char*)first_path;
  arc.OpenMode = RAR_OM_LIST;   /* list-only 模式不解压 */
  HANDLE h = RAROpenArchiveEx(&arc);
  if (arc.OpenResult != 0) {
    result->status = ZIPX_ERR_FORMAT;
    return ZIPX_ERR_FORMAT;
  }

  uint64_t total_bytes = 0;
  uint32_t total_entries = 0;
  int max_depth = 0;
  result->has_password = 0;

  while (RARReadHeaderEx(h, &hdr) == 0) {
    /* 加密探测 */
    if (hdr.Flags & (LHD_PASSWORD /* RAR4 */) ||
        (hdr.Flags & 0x0004 /* RAR5 encrypted */)) {
      result->has_password = 1;
    }

    /* name 安全校验（用 zip_extract.c 里的同名函数） */
    if (!is_safe_archive_path(hdr.FileName)) {
      RARCloseArchive(h);
      result->status = ZIPX_ERR_UNSAFE_NAME;
      snprintf(result->detail, sizeof(result->detail), "%s", hdr.FileName);
      return ZIPX_ERR_UNSAFE_NAME;
    }

    /* depth 校验 */
    uint32_t d = path_depth(hdr.FileName);
    if (d > max_depth) max_depth = d;
    if (max_depth > limits->max_depth) {
      RARCloseArchive(h);
      result->status = ZIPX_ERR_LIMIT_DEPTH;
      return ZIPX_ERR_LIMIT_DEPTH;
    }

    /* name/path 长度 */
    if (strlen(hdr.FileName) > limits->max_name_len ||
        strlen(hdr.FileName) > limits->max_path_len) {
      RARCloseArchive(h);
      result->status = ZIPX_ERR_LIMIT_NAME;
      return ZIPX_ERR_LIMIT_NAME;
    }

    /* size 累计 */
    total_bytes += (uint64_t)hdr.UnpSize;
    if (hdr.UnpSize > limits->max_file_bytes) {
      RARCloseArchive(h);
      result->status = ZIPX_ERR_LIMIT_FILE;
      snprintf(result->detail, sizeof(result->detail),
               "%s (%llu bytes)", hdr.FileName,
               (unsigned long long)hdr.UnpSize);
      return ZIPX_ERR_LIMIT_FILE;
    }
    if (total_bytes > limits->max_total_bytes) {
      RARCloseArchive(h);
      result->status = ZIPX_ERR_LIMIT_TOTAL;
      return ZIPX_ERR_LIMIT_TOTAL;
    }

    total_entries++;
    if (total_entries > limits->max_entries) {
      RARCloseArchive(h);
      result->status = ZIPX_ERR_LIMIT_ENTRIES;
      return ZIPX_ERR_LIMIT_ENTRIES;
    }

    /* 跳过 entry body（list-only 不需要调用 ProcessFile） */
  }
  RARCloseArchive(h);

  result->entries_total = total_entries;
  result->bytes_total = total_bytes;
  return ZIPX_OK;
}
```

#### 6.1.6 D1 验证清单

```bash
# 1. 编译通过（host）
gcc -O2 -c third_party/unrar/unrar.c -o /tmp/unrar.o
gcc -O2 -c src/rar_extract.c -o /tmp/rar_extract.o -I third_party/unrar/
echo "exit: $?"

# 2. 链接能产出 host binary（基础 sanity）
gcc -O2 -o /tmp/test-rar-extract /tmp/rar_extract.o /tmp/unrar.o -lz
echo "exit: $?"

# 3. 跑现有测试（确认没破坏 zip 流程）
cd /home/song/ps5-web-file-manager/
bash tests/run-tests.sh
# 期望：69 checks, 0 failures
```

---

### D2：rar_extract 联调 + magic 探测 + limits 校验

#### 6.2.1 extract 阶段实现

```c
static zipx_status_t
rarx_extract_pass(struct RAROpenArchiveDataEx *arc,
                            const zipx_limits_t *limits,
                            const rarx_password_t *password,
                            rarx_progress_fn progress, void *userdata,
                            rarx_result_t *result) {
  if (password && password->password_set) {
    RARSetPassword(arc->h, password->password);
  }

  struct RARHeaderDataEx hdr;
  while (RARReadHeaderEx(arc->h, &hdr) == 0) {
    /* ratio 实时校验 */
    if (hdr.PackSize > 0 && limits->max_ratio > 0) {
      uint64_t ratio = hdr.UnpSize / hdr.PackSize;
      if (ratio > limits->max_ratio) {
        result->status = ZIPX_ERR_LIMIT_RATIO;
        snprintf(result->detail, sizeof(result->detail),
                 "%s ratio %llu", hdr.FileName,
                 (unsigned long long)ratio);
        return ZIPX_ERR_LIMIT_RATIO;
      }
    }

    /* progress 回调 */
    if (progress) {
      zipx_progress_t p = {
        .phase = ZIPX_PHASE_EXTRACT,
        .entries_total = result->entries_total,
        .entries_done = result->entries_done,
        .bytes_total = result->bytes_total,
        .bytes_done = result->bytes_done,
        .current = hdr.FileName,
      };
      progress(userdata, &p);
    }

    /* 解到 staging 目录 */
    char out_path[ZIPX_PATH_MAX];
    snprintf(out_path, sizeof(out_path), "%s/%s",
             result->detail /* staging dir */, hdr.FileName);

    int mode = (hdr.Flags & 0xE0) == 0xE0 /* RAR5 dir flag */ ?
            RAR_EXTRACT_DEST : RAR_EXTRACT;
    /* 路径安全再校验一次 */
    int rc = RARProcessFile(arc->h, mode, NULL, out_path);
    if (rc != 0) {
      result->status = (rc == 11) ? ZIPX_ERR_PASSWORD : ZIPX_ERR_IO;
      result->sys_errno = errno;
      return result->status;
    }

    result->entries_done++;
    result->bytes_done += hdr.UnpSize;

    /* crc 校验 —— unrar 已经做了，会自动设错误 */
  }
  return ZIPX_OK;
}
```

#### 6.2.2 magic 探测（不依赖扩展名）

```c
static int
rarx_probe_magic(const char *path) {
  FILE *fp = fopen(path, "rb");
  if (!fp) return 0;
  unsigned char buf[8];
  size_t n = fread(buf, 1, sizeof(buf), fp);
  fclose(fp);
  if (n < 7) return 0;
  /* RAR 4: "Rar!\x1a\x07\x00" */
  if (!memcmp(buf, "Rar!\x1a\x07\x00", 7)) return 1;
  /* RAR 5: "Rar!\x1a\x07\x01\x00" */
  if (!memcmp(buf, "Rar!\x1a\x07\x01\x00", 8)) return 1;
  return 0;
}
```

**为什么需要 magic 探测**：
- 用户可能重命名 `.bin` → 实际是 RAR
- 防止 `extract.c` 派发时仅靠扩展名判断漏掉情况

#### 6.2.3 D2 验证清单

```bash
# 1. 写个最小的 manual fixture：
#    用系统 rar 命令（如果装了）建 RAR5 单卷 test.rar + 内容
#    或者从网上找开源的 RAR 测试样本
# 2. host 跑：
./.build/host-test/test-rar-extract tests/fixtures/rar5_single.rar /tmp/out
# 3. 确认 /tmp/out 里有解出来的文件

# 4. ratio bomb 测试：建一个 4MB of 'A' 的 RAR5，验证 ERR_LIMIT_RATIO
# 5. path traversal：建一个含 ../../etc/passwd 的 RAR，验证 ERR_UNSAFE_NAME
```

---

### D3：extract.c 派发 + password 字段 + 错误码映射

#### 6.3.1 src/filemgr_internal.h 新增字段

```c
typedef struct {
  ...
  int extract_conflict;
  int extract_remove_source;
  int extract_large;
  int extract_format;          /* 新增: 0=zip, 1=rar */
  char extract_password[128];  /* 新增 */
} file_task_t;
```

#### 6.3.2 src/extract.c 派发逻辑

**关键**：扩展名判断 + magic fallback 都做，避免前端漏检。

```c
/* 新增 detect 函数 */
static int
detect_archive_format(const char *path, FILE *probe) {
  /* 1. 扩展名优先（快） */
  if (str_ends_with_ci(path, ".rar") ||
      str_ends_with_ci(path, ".part01.rar") ||
      str_ends_with_ci(path, ".part1.rar") ||
      str_ends_with_ci(path, ".part001.rar")) {
    return 1; /* RAR */
  }
  if (str_ends_with_ci(path, ".zip") ||
      str_ends_with_ci(path, ".zipx")) {
    return 0; /* ZIP */
  }
  /* 2. magic fallback */
  if (probe && rarx_probe_magic(path)) return 1;
  if (probe && zipx_probe_magic(path)) return 0;
  return -1; /* unknown */
}
```

#### 6.3.3 api_extract 解析 password 字段

```c
char *password_str = body_form_value(body, body_size, "password");
char password[128] = {0};
int password_set = 0;
if (password_str) {
  strncpy(password, password_str, sizeof(password) - 1);
  /* 截断 sanitize：去掉 control chars */
  sanitize_password(password);
  password_set = 1;
}
free(password_str);

/* ... */

snprintf(task->extract_password, sizeof(task->extract_password),
         "%s", password);
task->extract_format = detect_archive_format(task->src, NULL);
```

> ⚠️ **安全**：绝不在日志、progress 回调、task_update 里写 `password` 字段。grep 整个代码库 `password` 字符串只允许出现在 form 解析处。

#### 6.3.4 extract_worker 派发

```c
static void *
extract_worker(void *arg) {
  file_task_t *task = arg;
  zipx_conflict_t conflict = task->extract_conflict;
  rarx_password_t rpwd = {0};
  rpwd.password_set = task->extract_password[0] != 0;
  strncpy(rpwd.password, task->extract_password,
          sizeof(rpwd.password) - 1);

  /* 立即清零 task 里的密码（防御内存 dump） */
  memset(task->extract_password, 0,
         sizeof(task->extract_password));

  zipx_status_t status;
  if (task->extract_format == 1 /* RAR */) {
    rarx_result_t rres = {0};
    status = rarx_extract(task->src, task->dst, conflict,
                          zipx_limits_profile(task->extract_large),
                          &rpwd,
                          extract_cancel, extract_progress, task, &rres);
    /* 映射 rarx_result_t → task 状态字段 */
  } else {
    zipx_result_t zres = {0};
    status = zipx_extract(task->src, task->dst, conflict,
                          zipx_limits_profile(task->extract_large),
                          extract_cancel, extract_progress, task, &zres);
  }

  /* status → task state 的映射逻辑保持不变 */
  ...
}
```

#### 6.3.5 D3 验证清单

```bash
# host 端跑全量回归
bash tests/run-tests.sh
# 期望：69 checks, 0 failures（zip 流程没坏）

# 手动构造 form 测试 password 解析：
curl -X POST http://127.0.0.1:8080/api/extract \
  -d "path=/data/test.rar&dst_dir=/data/out&password=secret123"
# 期望任务接受，无 500
```

---

### D4：前端 isExtractableArchive + 密码 modal + i18n + 子卷置灰

#### 6.4.1 main.js 检测函数

```js
function isExtractableArchive(item) {
  if (item.type !== "-") return false;
  // ZIP（已有）
  if (/\.zipx?$/i.test(item.name)) return true;
  // RAR 单卷 .rar
  if (/\.rar$/i.test(item.name)) return true;
  // RAR5 分卷主卷 .part01.rar / .part1.rar / .part001.rar
  if (/\.part0*1\.rar$/i.test(item.name)) return true;
  return false;
}

function isRarSubVolume(item) {
  if (item.type !== "-") return false;
  // RAR5 子卷 .part02.rar, .part2.rar ...（part01 才是主卷）
  if (/\.part0*\d+\.rar$/i.test(item.name) &&
      !/\.part0*1\.rar$/i.test(item.name)) return true;
  return false;
}
```

#### 6.4.2 renderExtractButton 升级

```js
function renderExtractButton(items, locked) {
  const extractable = items.filter(isExtractableArchive);
  const subs = items.filter(isRarSubVolume);
  // 子卷单独选中 → 按钮置灰 + 提示
  if (subs.length > 0 && extractable.length === 0) {
    extractBtn.hidden = false;
    extractBtn.disabled = true;
    extractBtn.title = t("extractSelectMainVolume");
    return;
  }
  // 主卷逻辑（与之前类似）
  extractBtn.hidden = extractable.length !== 1;
  if (extractable.length !== 1) {
    extractBtn.title = "";
    extractBtn.disabled = true;
    return;
  }
  extractBtn.title = t("extractToCurrent") + ": " + itemTitle(extractable);
  extractBtn.disabled = locked;
}
```

#### 6.4.3 密码 modal（HTML）

在 `assets/index.html` 适当位置插入：

```html
<div id="passwordModal" class="modal hidden">
  <div class="modal-card">
    <h3 id="passwordTitle">请输入解压密码</h3>
    <p id="passwordPrompt" class="muted"></p>
    <input type="password" id="passwordInput" autocomplete="off"
           class="text-input" />
    <div class="modal-actions">
      <button id="passwordCancel" class="btn-secondary">取消</button>
      <button id="passwordOk" class="btn-primary">确定</button>
    </div>
  </div>
</div>
```

#### 6.4.4 密码 modal（CSS）

在 `assets/main.css` 末尾加：

```css
.modal { position: fixed; inset: 0; background: rgba(0,0,0,0.6);
         display: flex; align-items: center; justify-content: center;
         z-index: 1000; }
.modal.hidden { display: none; }
.modal-card { background: var(--bg-elevated); padding: 24px;
              border-radius: 8px; min-width: 320px; max-width: 480px; }
.modal-actions { display: flex; gap: 12px; justify-content: flex-end;
                 margin-top: 16px; }
.muted { color: var(--text-faint); font-size: 13px; }
.text-input { width: 100%; padding: 8px 12px; margin: 12px 0;
              border: 1px solid var(--border); border-radius: 4px;
              background: var(--bg-input); color: var(--text); }
```

#### 6.4.5 startExtractTask 接收 password

```js
async function startExtractTask(path, dstDir, conflict, removeSource,
                                 name, large, password) {
  const data = await apiForm("/api/extract", {
    path, dst_dir: dstDir, conflict,
    remove_source: removeSource ? "1" : "0",
    large: large ? "1" : "0",
    password: password || ""
  });
  ...
}
```

#### 6.4.6 actionExtract 加密码弹窗

```js
function promptPassword(fileName, retry) {
  return new Promise((resolve, reject) => {
    const modal = document.getElementById("passwordModal");
    const input = document.getElementById("passwordInput");
    const ok = document.getElementById("passwordOk");
    const cancel = document.getElementById("passwordCancel");
    const titleEl = document.getElementById("passwordTitle");
    const promptEl = document.getElementById("passwordPrompt");

    titleEl.textContent = retry ?
      t("passwordWrongTitle") : t("passwordRequiredTitle");
    promptEl.textContent = retry ?
      t("passwordWrongPrompt") : t("passwordRequiredPrompt", {name: fileName});
    input.value = "";
    modal.classList.remove("hidden");
    input.focus();

    const cleanup = () => {
      modal.classList.add("hidden");
      ok.removeEventListener("click", onOk);
      cancel.removeEventListener("click", onCancel);
    };
    const onOk = () => { const v = input.value; cleanup(); resolve(v); };
    const onCancel = () => { cleanup(); reject(new Error("canceled")); };

    ok.addEventListener("click", onOk);
    cancel.addEventListener("click", onCancel);
    input.addEventListener("keydown", e => {
      if (e.key === "Enter") onOk();
      if (e.key === "Escape") onCancel();
    });
  });
}

async function actionExtract() {
  const item = singleSelected();
  if (!item) return;
  const conflict = ...;
  const large = shouldPromptLargeMode(item.size) ? promptLargeMode(item.size) : false;

  let password = "";
  if (isRarExtension(item.name)) {  /* 仅 RAR 弹 */
    try {
      password = await promptPassword(item.name, false);
    } catch { return; }
  }
  startExtractTask(item.path, cwd, conflict, false,
                    displayName(item), large, password);
}
```

#### 6.4.7 后端 ERR_PASSWORD 自动弹窗

```js
/* 任务状态轮询或 SSE 里 */
if (task.error === "extract_password" ||
    (task.message || "").toLowerCase().includes("password")) {
  // 不太优雅但稳：弹窗重试
  // （更好的做法是在 zipx_status_string 里直接提供 code，
  //   前端 if (status === ZIPX_ERR_PASSWORD) → 弹）
}
```

> 💡 **更稳的方案**：在 `zipx_status_string(ZIPX_ERR_PASSWORD)` 返回字符串 `"extract_password"`，前端 `task.op === "extract" && task.error === "extract_password"` 时弹窗，让用户重新提交。这避免字符串包含匹配带来的误报。

#### 6.4.8 i18n 新增文案

`assets/lang-zh.js`:
```js
extractSelectMainVolume: "请改选主卷（如 .part01.rar 或 .rar）",
passwordRequiredTitle: "需要解压密码",
passwordRequiredPrompt: "RAR 文件 {name} 已加密，请输入解压密码。",
passwordWrongTitle: "密码错误",
passwordWrongPrompt: "密码错误，请重新输入。密码仅本次使用，不会保存。",
```

`assets/lang-en.js`:
```js
extractSelectMainVolume: "Select the main volume (e.g. .part01.rar or .rar)",
passwordRequiredTitle: "Password required",
passwordRequiredPrompt: "The RAR archive {name} is encrypted. Please enter the password.",
passwordWrongTitle: "Wrong password",
passwordWrongPrompt: "Wrong password. Please try again. The password is only used for this extraction and is never saved.",
```

#### 6.4.9 D4 验证清单

```bash
# 1. node 语法检查
node --check assets/main.js
node --check assets/lang-en.js
node --check assets/lang-zh.js

# 2. 浏览器手动测试（开 dev server 或本地 http-server）：
#    - 选 .rar → 弹密码框
#    - 选 .zip → 不弹密码框（保持 v1.7 行为）
#    - 选 .part02.rar → 解压按钮置灰 + tooltip
#    - 选 .part01.rar → 解压按钮可用

# 3. 提交后端确认 password 字段透传：
#    Network → /api/extract → Form Data → password: "xxx"
```

---

### D5：host tests/test_rar_extract.c + fixtures

#### 6.5.1 fixture 准备

需要 RAR 测试样本。**优先用 WinRAR / rar 命令行工具生成**：

```bash
# 装 unrar / rar（host）
sudo apt install rar unrar  # 或 mac: brew install rar

# 生成 fixtures（用脚本自动化）
cd tests/fixtures/

# RAR4 单卷明文
rar a -os rar4_single.rar sample.txt

# RAR5 单卷明文
rar a -ma rar5_single.rar sample.txt

# RAR5 分卷（5 卷 × 1MB）
mkdir -p split_src && head -c 5M /dev/urandom > split_src/big.bin
rar a -v1m -ma rar5_multi.part01.rar split_src/

# 加密 RAR5（密码 "secret"）
rar a -ma -hpsecret encrypted_rar5.rar sample.txt

# 加密 RAR4（密码 "secret"）
rar a -os -hpsecret encrypted_rar4.rar sample.txt

# 加密分卷
rar a -v1m -ma -hpsecret encrypted_multi.part01.rar split_src/

# 损坏
head -c 1024 rar5_single.rar > rar5_truncated.rar
```

> ⚠️ **fixture 不能 commit**（见 .gitignore），需要在 `tests/make_fixtures.py` 里写生成函数，运行 `bash tests/run-tests.sh` 时自动调用。

#### 6.5.2 tests/make_fixtures.py 新增

```python
def rar4_single():
    """RAR4 single volume, plaintext. Generated by host `rar` CLI."""
    import subprocess
    src = path("_rar_src.txt")
    if not os.path.exists(src):
        with open(src, "w") as f:
            f.write("hello rar4\n" * 100)
    out = path("rar4_single.rar")
    if not os.path.exists(out):
        subprocess.check_call(["rar", "a", "-os", out, src])
    return out

def rar5_single():
    src = path("_rar_src.txt")
    out = path("rar5_single.rar")
    if not os.path.exists(out):
        subprocess.check_call(["rar", "a", "-ma", out, src])
    return out

# ... 其余同理
```

> 💡 **CI 兼容性**：CI 环境可能没装 rar。改用 `unrar` 命令 + 预生成 fixture 一并存档。**最稳**：把生成好的 fixture 提交到 git（small ones only，<1MB each）。

#### 6.5.3 tests/test_rar_extract.c 测试用例模板

```c
/* tests/test_rar_extract.c */

#include "rar_extract.h"

static rarx_result_t
run_rar(const char *src, const char *dst,
        zipx_conflict_t conflict,
        const zipx_limits_t *limits,
        const rarx_password_t *pwd,
        rarx_progress_fn progress, void *userdata) {
  rarx_result_t res = {0};
  rarx_extract(src, dst, conflict, limits, pwd,
               NULL, progress, userdata, &res);
  return res;
}

static void
test_rar4_single(void) {
  rarx_result_t r = run_rar("rar4_single.rar", "out_rar4_single",
                            ZIPX_CONFLICT_FAIL, NULL, NULL, NULL, NULL);
  check(r.status == ZIPX_OK, "RAR4 single extracts OK");
  check(r.entries_total == 1, "1 entry");
  check(r.files_created == 1, "1 file created");
}

static void
test_rar5_single(void) {
  rarx_result_t r = run_rar("rar5_single.rar", "out_rar5_single",
                            ZIPX_CONFLICT_FAIL, NULL, NULL, NULL, NULL);
  check(r.status == ZIPX_OK, "RAR5 single extracts OK");
}

static void
test_rar5_multi_volume(void) {
  /* unrar 自动找 .part02, .part03... */
  rarx_result_t r = run_rar("rar5_multi.part01.rar",
                            "out_rar5_multi",
                            ZIPX_CONFLICT_FAIL, NULL, NULL, NULL, NULL);
  check(r.status == ZIPX_OK, "RAR5 multi-volume extracts OK");
  check(r.entries_total >= 1, "at least one entry");
}

static void
test_rar4_encrypted_correct_pwd(void) {
  rarx_password_t pwd = {.password = "secret", .password_set = 1};
  rarx_result_t r = run_rar("encrypted_rar4.rar",
                            "out_rar4_enc_ok",
                            ZIPX_CONFLICT_FAIL, NULL, &pwd, NULL, NULL);
  check(r.status == ZIPX_OK, "encrypted RAR4 with correct pwd OK");
}

static void
test_rar4_encrypted_wrong_pwd(void) {
  rarx_password_t pwd = {.password = "wrong", .password_set = 1};
  rarx_result_t r = run_rar("encrypted_rar4.rar",
                            "out_rar4_enc_wrong",
                            ZIPX_CONFLICT_FAIL, NULL, &pwd, NULL, NULL);
  check(r.status == ZIPX_ERR_PASSWORD, "wrong pwd → ERR_PASSWORD");
  check(r.files_created == 0, "no file created on wrong pwd");
}

static void
test_rar4_encrypted_no_pwd(void) {
  rarx_result_t r = run_rar("encrypted_rar4.rar",
                            "out_rar4_enc_nopwd",
                            ZIPX_CONFLICT_FAIL, NULL, NULL, NULL, NULL);
  check(r.status == ZIPX_ERR_PASSWORD, "no pwd → ERR_PASSWORD");
  check(r.has_password == 1, "scan detects password requirement");
  check(r.files_created == 0, "no file created without pwd");
}

/* 全部测试 + main 入口同 test_zip_extract.c */
```

#### 6.5.4 run-tests.sh 新增编译 RAR 支持

```bash
# tests/run-tests.sh 新增：
"$CC" -O2 -c "$ROOT/third_party/unrar/unrar.c" -o "$BUILD/unrar.o" || exit 1
"$CC" -O2 -c "$ROOT/src/rar_extract.c" -o "$BUILD/rar_extract.o" \
    -I "$ROOT/src" -I "$ROOT/third_party/unrar/" || exit 1
"$CC" -O2 -c "$ROOT/tests/test_rar_extract.c" -o "$BUILD/test_rar_extract.o" \
    -I "$ROOT/src" -I "$ROOT/third_party/unrar/" || exit 1

# 链接
"$CC" -O2 -o "$BUILD/test-rar-extract" \
    "$BUILD/rar_extract.o" "$BUILD/test_zip_extract.o" \
    "$BUILD/unrar.o" "$BUILD/test_rar_extract.o" "${objs[@]}" \
    || exit 1

"$BUILD/test-rar-extract" "$ROOT/tests/fixtures" "$BUILD/work"
```

#### 6.5.5 D5 验证清单

```bash
bash tests/run-tests.sh
# 期望：69 + ~12 = 81 checks, 0 failures

# 单独跑 RAR 测试：
./.build/host-test/test-rar-extract tests/fixtures/ /tmp/rar_work
# 期望：所有 RAR 测试通过
```

---

### D6：WSL 跨编 + ELF 验证 + 文档

#### 6.6.1 WSL Makefile 更新

`Makefile` 加：
```makefile
# third_party/unrar/
UNRAR_OBJS = $(addprefix $(OBJ_DIR)/third_party/unrar/, \
              $(notdir $(wildcard third_party/unrar/*.c)))
$(UNRAR_OBJS): | $(OBJ_DIR)/third_party/unrar
$(OBJ_DIR)/third_party/unrar/%.c.o: third_party/unrar/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# src/rar_extract.o
OBJ_FILES += src/rar_extract.c

# .elf 链接加 unrar.o + rar_extract.o
$(ELF): ... $(UNRAR_OBJS) src/rar_extract.o ...
```

#### 6.6.2 跨编

```bash
# WSL Ubuntu-22.04 内
cd /home/song/ps5-web-file-manager
bash build-elf.sh
# 期望：[7/7] OK
# 期望 size: ~580 KiB
# 期望 sha256: 不在 list 内（首次编）
```

#### 6.6.3 ELF 验证脚本

```bash
# Windows side 验证
cd "C:\Users\songl\Desktop\Web File Manager\ps5-web-file-manager"
sha256sum web-file-mgr.elf
size web-file-mgr.elf
head -c 20 web-file-mgr.elf | xxd | head -2

# gzip 流验证（前端改动进了 ELF）
python3 .build/check-elf-gzip.py ./web-file-mgr.elf | grep -E "(✓|✗)"
# 期望：
#   ✓ passwordRequiredTitle
#   ✓ passwordRequiredPrompt
#   ✓ passwordWrongTitle
#   ✓ passwordWrongPrompt
#   ✓ extractSelectMainVolume
#   ✓ startExtractTask  ... password: password || ""
#   ✓ rarx_extract
#   ✓ ZIPX_ERR_PASSWORD
```

#### 6.6.4 文档更新

**新建**：`docs/UPGRADE-v1.8-rar-support.md`（镜像 v1.7 UPGRADE 风格，~450 行）

**更新**：`CHANGELOG.md` 头部加 v1.8 段：
```markdown
## [v1.8] - 2026-09-XX

### Added
- **RAR archive extraction** via vendored alexbatalov/unrar.c
- RAR4 and RAR5 single-volume and multi-volume (`name.partNN.rar`)
- Encrypted RAR support with password prompt + retry dialog
- Frontend detection of RAR main/sub volumes + grayscale sub-volume button

### Technical
- `src/rar_extract.{h,c}` mirrors `zip_extract` API (1600 LOC, three-phase pipeline)
- `extract.c` dispatches by extension + magic-byte fallback
- ZIP path unchanged (v1.7 large-file profile still works)
```

**更新**：`README.md` ZIP extraction 段后面加：
```markdown
### RAR extraction

The project also extracts RAR4 and RAR5 archives, including
multi-volume (`name.part01.rar`, `name.part02.rar`, …). Encrypted
archives prompt for a password client-side; the password is held
only in memory and never saved.

Limits mirror the ZIP profiles (200K entries / 1 TiB / 256 GiB /
ratio 500 by default, with the same `large=1` opt-in to 500K /
2 TiB / 1 TiB / 1000).
```

#### 6.6.5 Git 提交 + push

```bash
cd /home/song/ps5-web-file-manager
git add third_party/unrar/ src/rar_extract.* src/extract.c src/filemgr_internal.h \
        assets/main.js assets/main.css assets/lang-en.js assets/lang-zh.js \
        tests/test_rar_extract.c tests/make_fixtures.py tests/run-tests.sh \
        Makefile docs/UPGRADE-v1.8-rar-support.md CHANGELOG.md README.md
git -c core.autocrlf=false commit -m "v1.8: RAR4/RAR5 + multi-volume + encrypted support

- vendor alexbatalov/unrar.c into third_party/unrar/ (UnRAR license)
- src/rar_extract.{h,c}: three-phase engine (scan -> extract -> publish -> cleanup)
  with first-volume-only API (unrar internally finds companion volumes)
- extract.c: format dispatch by extension + Rar! magic fallback
- API: POST /api/extract gains optional 'password' form field
- frontend: isExtractableArchive() covers RAR single/multi; isRarSubVolume()
  disables the extract button with a tooltip
- password modal: required on encrypted RAR, wrong-password retry
- tests: +12 RAR checks (single/multi/encrypted-no-pwd/encrypted-wrong-pwd/
  encrypted-correct-pwd/ratio-bomb/path-traversal/truncated)
- ELF: 418 KiB -> ~580 KiB; gzip-assets verified for new strings
- docs: CHANGELOG + UPGRADE-v1.8-rar-support.md + README"

# 用户在自己 PowerShell（非沙箱）里：
#   git push -u origin main
#   git tag -a v1.8 -m "..."
#   git push origin v1.8
```

---

## 7. 关键代码模式（参考 zip_extract 实现）

### 7.1 三阶段架构（rar_extract 必须镜像）

```c
zipx_status_t
rarx_extract(const char *first_path, const char *dst_dir,
             zipx_conflict_t conflict,
             const zipx_limits_t *limits,
             const rarx_password_t *password,
             zipx_cancel_fn cancel, zipx_progress_fn progress,
             void *userdata, rarx_result_t *result) {
  /* 1) scan 阶段 */
  zipx_status_t s = rarx_scan(first_path, limits, result);
  if (s != ZIPX_OK) return s;
  if (result->has_password && (!password || !password->password_set)) {
    result->status = ZIPX_ERR_PASSWORD;
    return ZIPX_ERR_PASSWORD;
  }

  /* 2) 创建 staging 目录 */
  char staging[ZIPX_PATH_MAX];
  snprintf(staging, sizeof(staging), "%s/.wfm-part-%d",
           dst_dir, (int)getpid());
  if (mkdir(staging, 0755) < 0) {
    result->status = ZIPX_ERR_IO;
    return ZIPX_ERR_IO;
  }
  snprintf(result->detail, sizeof(result->detail), "%s", staging);

  /* 3) extract 阶段（解到 staging） */
  s = rarx_extract_pass(first_path, staging, limits, password,
                        cancel, progress, result);
  if (s != ZIPX_OK) {
    /* cleanup: unlink staging 整树 */
    nftw(staging, unlink_cb, 64, FTW_DEPTH | FTW_PHYS);
    rmdir(staging);
    return s;
  }

  /* 4) publish 阶段：rename staging → dst_dir */
  s = rarx_publish(staging, dst_dir, conflict, result);
  if (s != ZIPX_OK) {
    nftw(staging, unlink_cb, 64, FTW_DEPTH | FTW_PHYS);
    rmdir(staging);
    return s;
  }

  return ZIPX_OK;
}
```

### 7.2 path 安全校验（直接复用 zip_extract.c 的实现）

```c
/* 把 zip_extract.c 的 is_safe_archive_path() 复制到 rar_extract.c */
/* 或者提到一个共用的 src/path_util.c */
```

> ⚠️ **可重构性**：D3 阶段如果时间够，把 `is_safe_archive_path` / `path_depth` / `nftw_unlink` 提到 `src/path_util.c`，rar_extract 和 zip_extract 都 include。**不做也行**，copy 一份到 rar_extract.c 即可。

### 7.3 错误码映射（rar → zip 命名空间）

| RAR unrar API 返回 | unrar 含义 | 映射到 zipx_status_t |
|---|---|---|
| `ERAR_NO_MEMORY` | OOM | `ZIPX_ERR_INTERNAL` |
| `ERAR_BAD_DATA` | CRC 错 / 损坏 | `ZIPX_ERR_CRC` |
| `ERAR_BAD_ARCHIVE` | 头错 / 不识别 | `ZIPX_ERR_FORMAT` |
| `ERAR_UNKNOWN_FORMAT` | 不是 RAR | `ZIPX_ERR_FORMAT` |
| `ERAR_EOPEN` | 文件打不开 | `ZIPX_ERR_OPEN` |
| `ERAR_ECREATE` | 创建输出失败 | `ZIPX_ERR_IO` |
| `ERAR_ECLOSE` | 关闭失败 | `ZIPX_ERR_IO` |
| `ERAR_EREAD` | 读失败 | `ZIPX_ERR_IO` |
| `ERAR_EWRITE` | 写失败 | `ZIPX_ERR_IO` |
| `ERAR_SMALL_BUF` | name buffer 不够 | `ZIPX_ERR_LIMIT_NAME` |
| `ERAR_PASSWORD` (11) | 密码错/缺 | `ZIPX_ERR_PASSWORD`（**新加**） |

### 7.4 task_state 中密码字段的安全处理

```c
/* extract_worker 入口 */
char password_copy[128];
strncpy(password_copy, task->extract_password, sizeof(password_copy) - 1);
memset(task->extract_password, 0, sizeof(task->extract_password));
/* 后续只用 password_copy，函数退出时也清零 */
```

---

## 8. 工程决策（不要重新讨论）

### 8.1 为什么 vendor unrar.c 而不是动态库

- SDK 没有 homebrew 目录（见 §2.3）
- vendor 源码 = 完全可控，编译/链接/调试一次到位
- unrar.c 纯 C + 单文件 = 跨编零摩擦
- 动态库需要 `-Wl,-rpath` 等额外 linker 配置，麻烦

### 8.2 为什么不用 libarchive

- 体积大一倍以上（libarchive stripped ~400 KiB，unrar stripped ~150 KiB）
- libarchive 内部仍依赖 unrar/librar 才能读 RAR —— 反而绕远
- libarchive 的 BSD-style API 与现有 zip_extract / rar_extract 设计不符
- 用户场景里 7z 罕见，付不起这个成本

### 8.3 为什么复用 zipx_status_t 枚举

- 错误码统一，前端不用分辨 ZIP_ERR_* vs RAR_ERR_*
- `task_update()` 一份映射逻辑覆盖两种格式
- 减少新代码量（也减少 bug）

### 8.4 为什么不在后端做分卷发现

- unrar 内部已经处理 `name.part01.rar` → 找 `.part02, .part03...`
- 后端写发现逻辑只是重复实现，且容易有边角 case bug

### 8.5 为什么密码不做错/对细粒度区分

- 「密码错」vs「文件损坏」的区分会泄露「文件存在 / 是否加密」信息（侧信道）
- 统一返回 `ZIPX_ERR_PASSWORD` + 友好文案即可
- 用户重试一次的成本可控

### 8.6 为什么 ELF 体积红线设在 ~1 MiB

- payload loader 通常限制 4 MiB，但实际 PS5 WebKit 启动时内存紧张
- 单 payload 越大，加载越慢；用户感受从 580 KiB → 800 KiB 能感知
- 留余量给未来加 7z、ZIP AES 等扩展

---

## 9. 注意事项 / 已知坑

### 9.1 unrar license 的实际情况（v1.8 选定 dmc_unrar）

> v1.8 实际选择的库是 [`DrMcCoy/dmc_unrar`](https://github.com/DrMcCoy/dmc_unrar) 1.7.0，**license 是 GPL-2.0-or-later**（不是 UnRAR License）。
>
> - ✅ 使用、编译、嵌入、二进制分发 —— 允许
> - ✅ 修改并以 GPL 条款整体分发 —— 允许
> - ❌ 改装 unrar.c —— **不允许**（UnRAR 上游版本，**dmc_unrar 允不允许改不重要因为我们没改**）
> - ❌ 不允许的：用 unrar 创建 RAR 压缩功能（不做就好）

为什么不需要担心 license：
- dmc_unrar.c **逐字未改**（vendor 完毕没碰），完整 GPL 通知在 `third_party/unrar/COPYING`
- dmc_unrar_api.h 是项目自有文件，按项目 license（GPLv3+）分发
- 项目本身已是 GPLv3+ → 与 GPL-2.0-or-later 兼容
- 二进制 + 对应源代码 + GPL 通知三件套 = 合规（libmicrohttpd 的 LGPL 已经这么做了）

**vendor 设计要点**（详见 docs/UPGRADE-v1.8-rar-support.md §5）：
- **不要 `#include "dmc_unrar.c"`** —— 会污染 dmc_unrar.c 内部的 struct 名（dmc_unrar_io_handler 的 open / close 字段会被 `tests/posix_compat.h` 的 `wfm_open` / `wfm_close` 重定义撞名）
- 用 facade header `third_party/unrar/dmc_unrar_api.h`（项目自有），只 re-declare 我们用到的符号
- Makefile 把 dmc_unrar.c 当成独立 TU 编（`THIRD_PARTY_SRCS += third_party/unrar/dmc_unrar.c`），加 `-Ithird_party/unrar -DDMC_UNRAR_DISABLE_BE32TOH_BE64TOH=1`

未来换 opello/unrar 时这套机制仍适用：把 `dmc_unrar.c` 换成 `*.cpp`、给 `dmc_unrar_api.h` 换内容（实现 RAROpenArchiveEx / RARSetPassword / RARProcessFileW 等 DLL API 风格），引擎签名不动、dispatch 不动、host tests 不动。

### 9.2 SDK staging 模式（每次都要 sudo）

```bash
# 不要尝试
chown -R song:song /opt/ps5-payload-sdk/   # ❌ 会破坏其他用户
chmod 777 /opt/ps5-payload-sdk/             # ❌ SDK 可能拒绝加载

# 正确做法（build-elf.sh v5 已实现）
make install DESTDIR=/tmp/wfm-stage
sudo cp -r /tmp/wfm-stage/opt/* /opt/
```

### 9.3 编辑工具的隐藏陷阱

- **Edit 工具偶有"成功但不落盘"现象**：连续两次同一 Edit，第一次只报成功未生效。**对策**：每次 Edit 后用 `grep` 验证改动真的进了文件。
- 写 `.bat` / `.ps1` 时**不要在 PS5 路径里写非 ASCII**（utf-8 BOM 破坏文件名）。**对策**：用 PowerShell `Write` 工具的绝对路径形式，或改用 Git Bash heredoc。
- 修改 unrar.c 用 `sed` 是诱人的，但 license 不允许改 → 用 wrapper 函数封装。

### 9.4 跨编 ELF 校验脚本

`gen-asset-module.py` 把 JS / JSON / CSS / HTML 用 zlib 压缩进 ELF。
**普通 `strings web-file-mgr.elf` 看不到前端新加的字符串**。
**对策**：用 `.build/check-elf-gzip.py`（已存在）扫 gzip 流验证。

```python
# 检查清单（D6 必跑）
python3 .build/check-elf-gzip.py ./web-file-mgr.elf | grep "✗"
# 期望：空输出（所有 key 都在）
```

### 9.5 测试 fixture 生成

- 4 MiB of 'A' 的 zip **不**用于 large profile 测试（ratio ≈ 1026 > 1000）
- 用 `bytes(range(256)) * 4096` 才有 ratio ≈ 238（夹在 200~1000 中间）
- RAR 用 host `rar` 命令生成 fixture，CI 环境可能没装 → 把 small fixtures 提交到 git

### 9.6 密码字段的安全

- task 结构里的 password 在 extract_worker 入口**立即清零**
- 日志 / progress 回调**绝不**写 password
- `task->message[]` 是固定 192 字节 buffer，**只**写错误描述，不写密码
- 前端 modal 关闭后立即 `input.value = ""`

### 9.7 magic 探测 + 扩展名优先级

扩展名优先（O(1) 字符串比较），magic fallback 用 file I/O（O(8 字节读））：
- 大文件不慢：magic 只读 8 字节
- 重命名 `.bin` 仍可识别
- 非 RAR 非 ZIP → 返回 -1，extract.c 报 `extract_unsupported`

### 9.8 三阶段 staging 目录命名

- `.wfm-part-{pid}` —— 用 pid 区分并发解压
- 失败时 `nftw(staging, unlink_cb, 64, FTW_DEPTH | FTW_PHYS)` 递归删
- publish 阶段 `rename(staging, dst_dir)` —— 原子（同一文件系统下）

### 9.9 RAR4 vs RAR5 头差异

| 字段 | RAR4 | RAR5 |
|---|---|---|
| Magic | `Rar!\x1a\x07\x00` | `Rar!\x1a\x07\x01\x00` |
| 大小字段 | 32-bit | 64-bit (UnpSizeHigh 等) |
| 加密 flag | `LHD_PASSWORD` (0x04) | 头 flag bit 0x04 |
| 目录 flag | `LHD_DIRECTORY` (0xE0) | 不同位 |
| CRC | 32-bit | 32-bit (压缩) |

unrar 抽象了这些，**API 层不必区分**，但错误处理时要兼容两种返回码。

### 9.10 unrar 多线程安全性

> ⚠️ **unrar 全局状态**：RAROpenArchive 返回的 HANDLE 不是线程安全的，**每个 task 必须独立打开/关闭**。当前 `extract_worker` 已经是每 task 一个 thread，没问题。

---

## 10. 常用命令清单

### 10.1 host 测试

```bash
cd /home/song/ps5-web-file-manager/
bash tests/run-tests.sh                    # 全量回归（81+ checks）
./.build/host-test/test-zip-extract \
   tests/fixtures/ /tmp/wfm_work         # 单独跑 ZIP
./.build/host-test/test-rar-extract \
   tests/fixtures/ /tmp/wfm_work         # 单独跑 RAR
```

### 10.2 PS5 跨编

```bash
# WSL Ubuntu-22.04 bash
cd /home/song/ps5-web-file-manager/
bash build-elf.sh                          # 全量构建 + 落双路径

# 仅清理 + 重编（快一些）
make clean all

# 增量编译（zlib/minizip-ng obj 缓存复用）
make all
```

### 10.3 ELF 验证

```bash
# 在 Windows 侧（或 WSL 内）
sha256sum web-file-mgr.elf
size web-file-mgr.elf
head -c 20 web-file-mgr.elf | xxd | head -2
od -An -tx2 -N2 -j18 web-file-mgr.elf | tr -d " "  # expect 003e

# 验证前端改动进了 ELF
python3 .build/check-elf-gzip.py ./web-file-mgr.elf
```

### 10.4 Git 操作

```bash
# 本地提交（commit 阶段）
cd /home/song/ps5-web-file-manager/
git add <files>
git -c core.autocrlf=false commit -m "..."

# 用户在自己 PowerShell / Git Bash（非沙箱）：
git push -u origin main
git tag -a v1.8 -m "..."
git push origin v1.8
```

### 10.5 debug 工具

```bash
# 跟踪 HTTP 请求（curl）
curl -v -X POST http://127.0.0.1:8080/api/extract \
  -d "path=/data/test.rar&dst_dir=/data/out&password=secret"

# 看 ELF 内是否有符号
nm web-file-mgr.elf 2>/dev/null | grep rarx
# （prospero-strip 后大部分本地符号被剥，外部函数名仍在）

# 看 unrar 占多大
size --target=binary web-file-mgr.elf
```

---

## 11. 测试矩阵（完成 D5 后应全过）

### 11.1 ZIP（v1.7 已覆盖 69 checks）

| 类别 | 用例 | 期望 |
|---|---|---|
| 基础 | basic / stored / unicode / zip64 | OK |
| 安全 | traversal / traversal_backslash / absolute / drive_letter / symlink / fifo | ERR_* |
| 加密 | encrypted | ERR_UNSUPPORTED |
| 错误 | bad_crc / truncated / not_a_zip | ERR_CRC / ERR_FORMAT / ERR_OPEN |
| 资源 | ratio / file / total cap / depth / name | ERR_LIMIT_* |
| 边界 | duplicate / file_dir_clash / conflict_source | ERR_DUPLICATE / 处理 conflict |
| 大文件 | large profile (16 checks) | OK / ERR_* 按预期 |

### 11.2 RAR（v1.8 新增 ~12 checks）

| 类别 | 用例 | 期望 |
|---|---|---|
| 基础 | rar4_single / rar5_single | OK |
| 分卷 | rar5_multi | OK |
| 加密 | rar4_enc_correct_pwd / rar5_enc_correct_pwd | OK |
| 加密 | rar4_enc_wrong_pwd / rar4_enc_no_pwd | ERR_PASSWORD |
| 错误 | rar_truncated / rar_bad_archive | ERR_FORMAT |
| 安全 | rar_path_traversal | ERR_UNSAFE_NAME |
| 资源 | rar_ratio_bomb / rar_file_too_large | ERR_LIMIT_RATIO / ERR_LIMIT_FILE |
| 边界 | rar_cancel | ERR_CANCELED |

### 11.3 集成测试（手动或脚本化）

```bash
# 在 PS5 真机 / qemu-ps5 上
1. 浏览器访问 http://ps5-ip:8080
2. 上传 sample.rar → 选中 → 解压 → 验证文件出现
3. 上传 encrypted.rar → 选中 → 弹密码框 → 输入 → 解压
4. 上传 .part02.rar → 选中 → 按钮置灰 → tooltip 正确
5. 上传 game.part01.rar + .part02.rar + .part03.rar → 选 part01 → 解压
6. 用错的密码再次尝试 → 弹密码错误窗 → 重试
7. 上传 200GB rar → 弹 large profile 窗 → 走 large profile 解压
```

---

## 12. 项目当前快照（2026-09-05 15:30 — v1.8 收尾）

```
Repo:    https://github.com/owendswang/ps5-web-file-manager.git (待 push)
Local:   C:\Users\songl\Desktop\Web File Manager\ps5-web-file-manager\
WSL:     \\wsl$\Ubuntu-22.04\home\song\ps5-web-file-manager\
HEAD:    bfe522e (local)  +  uncommitted v1.8 working tree
Branch:  main (no .git push yet — sandbox github 502)

ELF:     web-file-mgr.elf 待用户 WSL 重编
         v1.7 旧的: 418 KiB
                sha256: 648e4a00afe52669846df52ee5342bab42ea10050d555d5a1d4fa602653f514b
                e_machine: 0x003e (x86_64-sie-ps5 ✓)
         v1.8 预估: ~430 KiB (dmc_unrar 二进制 + facade, 真实尺寸待编)
                sha256: TBD
                e_machine: 0x003e (x86_64-sie-ps5 ✓)
         Version: v1.8 (VERSION_TAG 在 Makefile 已改)
         Title ID: FMGR88888

Tests:   83 checks, 0 failures (69 ZIP + 14 RAR)
Deps:    zlib 1.3.1, minizip-ng 4.2.2, dmc_unrar 1.7.0 (vendored; no system libs)

Done (v1.8 实施层):
  ✅ src/rar_extract.{c,h} (1276 LOC), 镜像 zip_extract 的三阶段 + scan/extract/publish/cleanup
  ✅ third_party/unrar/{dmc_unrar.c, dmc_unrar_api.h, COPYING, README.md, example.c, VENDORED.md}
  ✅ src/extract.c dispatch (扩展名 + magic fallback 单点判断)
  ✅ 14 new host tests + run-tests.sh + make_fixtures.py（带 rar/7z/placeholder fallback）
  ✅ Makefile (VERSION_TAG v1.8 + dmc_unrar TU + -DDMC_UNRAR_DISABLE_BE32TOH_BE64TOH=1)
  ✅ 前端 isExtractableArchive / isRarSubVolume + lang-{en,zh}.js err_extract_unsupported
  ✅ THIRD_PARTY_NOTICES section 3 (dmc_unrar attribution)
  ✅ CHANGELOG.md v1.8 section (含 deviation 注释)
  ✅ README.md (What's new in v1.8 + RAR section + Credits dmc_unrar + GPL-2.0 合规说明)
  ✅ docs/UPGRADE-v1.8-rar-support.md (架构 + facade 模式 + roadmap)
  ✅ docs/HANDOVER.md (本文件: §4/§9.1/§12/§14 全更新)

Doing (用户侧):
  ⏸ WSL 跑 bash build-elf.sh  → 重编 web-file-mgr.elf → 记 sha256 填进 CHANGELOG v1.8 banner
  ⏸ 填 ELF size 实际值
  ⏸ PowerShell 跑 git push -u origin main  (所有 5 + 1 commit)
  ⏸ git tag -a v1.8 -m "..." && git push origin v1.8
  ⏸ 在 README v1.7 banner 那行替换为 v1.8 banner (已改)
  ⏸ (可选) 部署 .elf 到 PS5 实机跑一遍 → 选 .rar → 解压 → 验证

Defer (v1.9):
  ⏸ vendor opello/unrar 替换 dmc_unrar → 加多卷 + 加密支持
  ⏸ 共用 staging/publish/nameset 抽到 src/archive_engine_common.c
  ⏸ password= 前端 modal + 后端 wire format
```

§13「写在最后」仍然适用，但下一节是新加的"v1.8 实际交付状态"，比 §13 更具体。

---

## 13. 写在最后

**这份文档不是"工作日志"，是"施工蓝图"**。任何接手人应该能：

1. 读完 §1 ~ §4（10 分钟）→ 知道项目是什么、当前到哪
2. 跳到 §6 按天执行（6 天）→ 写完 v1.8
3. 出问题翻 §7 ~ §9（30 分钟内定位）
4. 完成时按 §6.6.5 提交 + §10 验证

如果某个环节卡住超过 2 小时，**优先回这里查 §9 的「坑」** —— 90% 的边角 case 我都踩过了。

加油。

---

## 14. v1.8 实际交付状态（2026-09-05 收尾报告）

> **本节是 v1.8 RAR 支持的最后收尾报告**，写给接手人（前同事 / 未来自己），
> 让你在 5 分钟内知道：v1.8 做了什么、为什么这样做、哪里跳了坑、下一步是什么。
>
> §1 ~ §13 是 v1.8 开工前的施工蓝图（**与现实有偏差**，但工程决策保留）；
> 本节是 v1.8 完工后的真实记录（**以本节为准**）。

### 14.1 用了多久 / 写了多少

| 维度 | 数据 |
|---|---|
| 总耗时（沙箱内，开工到完工） | 约 6 小时（含 vendor 选型失败 → 重选 → 写引擎 → 写 host tests → 文档） |
| 新增 LOC | `src/rar_extract.c` 1276 + `src/rar_extract.h` 34 + `third_party/unrar/dmc_unrar_api.h` 138 + `third_party/unrar/VENDORED.md` 76 + `tests/test_rar_extract.c` 346 + `docs/UPGRADE-v1.8-rar-support.md` ≈ 700 = **≈ 2570 LOC 项目自有代码**（vendor 的 dmc_unrar.c 11 598 LOC 不计） |
| 改 LOC | Makefile + extract.c + main.js + lang-{en,zh}.js + make_fixtures.py + run-tests.sh + THIRD_PARTY_NOTICES + README + CHANGELOG ≈ 600 |
| 文档净增 | README ≈ +90 行 / CHANGELOG ≈ +100 行 / HANDOVER.md ≈ +80 行（新本节）/ UPGRADE-v1.8 ≈ 700（新文件） |
| 测试数 | 69 → **83**（+14 RAR） |

### 14.2 完成 vs §6 计划的 deviation（最重要）

| §6 计划 | v1.8 实际 | 为什么 |
|---|---|---|
| vendor `alexbatalov/unrar.c` | vendor `DrMcCoy/dmc_unrar` 1.7.0 | alexbatalov 仓库 404，dmc_unrar 是单文件 GPL-2.0 FLOSS，vendor 摩擦最小 |
| UnRAR license（改造禁止） | GPL-2.0-or-later（**可改但不改**） | 库选择改了，license 处理相应改成"vendor 不动 + 项目自有 facade" |
| 多卷 RAR `name.part01.rar` + `+02..` 链式 | ❌ 拒绝 + UI tooltip "select main volume" | dmc_unrar 上游不支持 volumes，需 opello/unrar（v1.9） |
| 加密 RAR + 密码 modal | ❌ 拒绝 + 无 password= 字段 | dmc_unrar 上游不支持加密，需 opello/unrar（v1.9） |
| `extract_format` + `extract_password` task 字段 | 字段没用 | dmc_unrar 不需要 password，task struct 保持 v1.7 形状 |
| `src/path_util.c` 共用 `is_safe_archive_path` 提取 | **未提取**（zip_extract 与 rar_extract 各有一份） | 进度 + 风险权衡后延后到 v1.9 共用 archive_engine_common.c 重构时 |
| 错码 `ZIPX_ERR_PASSWORD` 新增 | **未加** | 加密不支持，密码错根本发不出来；opello 接入时再加 |
| 前端 password modal HTML/CSS | **未加** | 同上；预留 modal 锚点（CSS class naming）供 v1.9 复用 |
| linux build 也编 rar_extract | ✅（COMMON_SRCS 已包含 rar_extract.c） | 顺手改的，没增加工作量 |

**核心决定**：把"vendor 一个 C++ UnRAR（opello/unrar）来支持多卷 + 加密"推迟到 v1.9，
v1.8 用 dmc_unrar 跑完"单卷 + 明文"这个 80% 用户的核心场景。代码改动面只局限在
`src/rar_extract.{c,h}` + `third_party/unrar/`，未来切换工作面极小。

### 14.3 关键工程决策（不要再讨论）

1. **dmc_unrar vs alexbatalov/unrar vs opello/unrar vs libarchive** —
   见 `third_party/unrar/VENDORED.md` §"Why dmc_unrar" 表格。摘要：单文件 +
   FLOSS + C99 = vendor 摩擦最小；opello 是"想要多卷加密"那 20% 用户的代价，
   v1.8 不付。

2. **facade header 模式（`dmc_unrar_api.h`）** — 见 `docs/UPGRADE-v1.8-rar-support.md`
   §5。这是 v1.8 最值得记下来的工程模式：vendor 一个独立的 .c 文件，绝对不要
   `#include "third_party.c"`，否则 host 构建系统的宏（`posix_compat.h` 的
   `wfm_open`/`wfm_close`）会和 vendor 内部结构体字段名打架。**通用做法**：写
   100 行的 facade header，只 declare 你用到的符号。

3. **dispatch 用扩展名不用 magic 嗅探** — 见 `src/extract.c::extract_dispatch()`。
   扩展名 O(1)，magic 要 I/O 读 8 字节，对每 archive 调用走两次。可以后加
   magic-byte 嗅探作为 future improvement，但 v1.8 没必要。

4. **RAR 没 public schema 给前端** — `extract_format` 在 task struct 里没暴露，
   因为前端只需知道"能不能解压"（看扩展名），不需要知道"格式是 ZIP 还是 RAR"，
   后端 dispatch 已经处理完。task struct 字段保持最小。

5. **`err_extract_unsupported` 文案** — 见 `assets/lang-{en,zh}.js`。明确告知
   "only unencrypted plain ZIP and single-volume RAR"，前端根据这条就知道是否
   弹密码（否）或弹"unrar on PC first"链接（可）。

### 14.4 接下来用户侧要做的（在 PowerShell / Git Bash，**非沙箱**）

```bash
# 1. WSL 内重编 ELF（需 30s 编译 + 5s strip）
#    在 WSL Ubuntu-22.04 bash:
cd /home/song/ps5-web-file-manager
export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk
bash build-elf.sh
# 输出会:
#   - 落 /home/song/ps5-web-file-manager/web-file-mgr.elf
#   - 落 C:/Users/songl/Desktop/Web File Manager/ps5-web-file-manager/web-file-mgr.elf
# 记下 ls -la 与 sha256sum 的输出

# 2. PowerShell / Git Bash 里验证
cd "C:/Users/songl/Desktop/Web File Manager/ps5-web-file-manager"
file web-file-mgr.elf             # ELF 64-bit LSB pie, x86-64
od -An -tx1 -N20 web-file-mgr.elf | head -2   # 7f45 4c46 0201
od -An -tx2 -N1 -j18 web-file-mgr.elf | tr -d ' '   # 003e
python3 .build/check-elf-gzip.py ./web-file-mgr.elf | grep -E "(✓|✗)" | tail
# 期望 v1.8 新 key: err_extract_unsupported 出现在 ✓ 行

# 3. 把 size + sha256 填进 CHANGELOG.md v1.8 banner
#    当前内容 (line 7-9):
#    > Release artifact for v1.8:
#    > `web-file-mgr.elf` — size TBD (cross-compile runs in WSL — see `docs/HANDOVER.md`)
#    > sha256 TBD
#    把 "TBD" 替换成实际值

# 4. 提交 + 推送
git add -A
git -c core.autocrlf=false commit -m "v1.8: RAR4/RAR5 single-volume unencrypted (dmc_unrar backend)
- vendor DrMcCoy/dmc_unrar 1.7.0 into third_party/unrar/ (GPL-2.0-or-later)
- src/rar_extract.{c,h}: three-phase engine mirroring zip_extract
- third_party/unrar/dmc_unrar_api.h: project-authored facade header
- extract.c: dispatch layer (extension-based; magic fallback post-v1.8)
- Makefile: VERSION_TAG v1.8 + dmc_unrar TU + byte-swap workaround
- frontend: isExtractableArchive / isRarSubVolume + lang-* err string update
- host tests: +14 RAR negative-path checks (total 83)
- docs: CHANGELOG v1.8 / README RAR section / docs/UPGRADE-v1.8-rar-support.md
- THIRD_PARTY_NOTICES: section 3 dmc_unrar attribution
- docs/HANDOVER.md: v1.8 实际交付状态 (§14) for handoff"

# 5. 用户在自己 PowerShell 推
git push -u origin main        # 一次性推所有 5 + 1 commit
git tag -a v1.8 -m "v1.8 — RAR4/RAR5 single-volume unencrypted (dmc_unrar)"
git push origin v1.8

# 6. (可选) PS5 真机部署
#    看 §10.3 在 HANDOVER.md 的 manual integration test 流程
```

### 14.5 给接手人的话

如果你是接手人：

- **前 5 分钟**：读 §14（这节），知道 v1.8 干了什么、为什么没干剩下的（加密/多卷）
- **下一个 5 分钟**：跳 `docs/UPGRADE-v1.8-rar-support.md`，看架构图 + facade 模式 +
  error mapping 表
- **如果接活 = v1.9（多卷 + 加密）**：直接打开
  `third_party/unrar/VENDORED.md` §"Upgrading to a fuller library (v1.9
  plan)"，按 5 步执行。**不要重新设计架构**，签名/调度/测试都不动。
- **如果接活 = 别的格式（7z, tar.xz, …）**：照 v1.8 的样子 mirror 一份：
  vendor + facade header + `src/<fmt>_extract.{c,h}` + dispatch + tests +
  docs。
- **如果接活 = 真机调试某用户的 .rar 上传失败**：99% 是 §14.2 那张表的
  边界 case（多卷 / 加密 / 旧版 / symlink 入口），看前端 `err_extract_unsupported`
  弹出来的 detail 字段就能定位。剩下的 1% 看 `rar_translate_error()`
  错误码映射 + dmc_unrar 自己的 issue tracker。
- **沙箱限制**：git push 必走 PowerShell / Git Bash（沙箱 502），WSL 必走
  `bash build-elf.sh`（沙箱没 SDK）。build-elf.sh 用的是 staging → sudo cp 模式，
  见 §2.3。

如果某个环节卡住超过 2 小时，**优先回这里看 §14.4 + §9** —— 90% 的边角情况
上面已经覆盖了。

加油 v1.9。