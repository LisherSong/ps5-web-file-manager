# ELF 瘦身可行性分析

> 2026-09-20 · 环境：WSL Ubuntu-22.04 + `/opt/ps5-payload-sdk` + LLD 18.1.8
> 基线产物 `web-file-mgr-v1.9.1.elf` = **1,034,328 B**
> 复现脚本：`.build/_sizeprobe.sh`、`.build/_probe3.sh`、`.build/_slimtest.sh`、`.build/_slimtest2.sh`、`.build/_modsize.sh`

## 结论

> ✅ **4.1 + 4.2 已落地（2026-09-20）** — `src/demangle_stub.c` 已加入 `COMMON_SRCS`，
> `LDFLAGS` 已加 `-Wl,--icf=all`。
>
> 产物：**870,488 B** · sha256 `24392aff6ddcca4dc0ea969cce356bd693ac52efe8a117d61ee1c814aa43cd07` · e_machine `0x003e`
>
> 落地后复核：`__cxa_demangle` 本体 11 B、`itanium_demangle` 符号 0、
> `__cxa_throw` / `_Unwind_Resume` 等异常符号齐全、`sevenz_extract` / `rar_extract` /
> `zipx_extract` / `MHD_start_daemon_va` 全部存在。
> section 变化：`.text` 637,616 → 538,336、`.rela.dyn` 54,816 → 25,800、
> `.eh_frame` 58,904 → 43,972、`.rodata` 162,016 → 153,664。
>
> 4.3（RELR）与 4.4 未启用 —— 待真机验证 / 权衡后再决定。

**能瘦，而且有一个"白捡"的 15.8%。**

| 方案 | 结果 | 降幅 | 风险 |
|---|---:|---:|---|
| 瘦身前 | 1,034,328 B | — | — |
| **+ `__cxa_demangle` 桩** | **886,872 B** | **−147 KB** | 极低 |
| **+ ICF 代码折叠（当前产物）** | **870,488 B** | **−164 KB (−15.8%)** | 极低 |
| 再 + RELR 重定位压缩 | 854,176 B | −180 KB (−17.4%) | 需真机验证 |
| 第三方改 `-Oz`（单列） | 969,504 B | −65 KB | 可能降速 |

前两项**不损失任何功能，也不影响解压速度** —— 去掉的是一段永远不会被执行的代码。

---

## 一、现状构成

数据源：`size -A` / `nm --size-sort --print-size` / 未 strip 重链接。

### section 级

| section | 大小 | 备注 |
|---|---:|---|
| `.text` | 637,616 | 代码主体 |
| `.rodata` | 162,016 | 前端资源（gzip）+ 字符串常量 |
| `.eh_frame` + `.eh_frame_hdr` | 71,444 | **C++ 异常展开表** |
| `.gcc_except_table` | 8,500 | **C++ 异常处理器表** |
| `.rela.dyn` | 54,816 | 2,119 × `R_X86_64_RELATIVE` + 165 × `GLOB_DAT` |
| `.data.rel.ro` | 20,192 | 含指针的只读数据 |
| `.dynsym` + `.dynstr` + `.gnu.hash` | 23,863 | 动态符号表（PIE 必需） |
| `.text$LZMADECOPT` | 4,719 | LZMA 汇编解码器（提速 1.26×，保留） |
| `.bss` | 56,416 | **不占文件体积** |

### 模块级（按目标文件归属，text+data）

| 模块 | 文件数 | text | data |
|---|---:|---:|---:|
| unrar7（RAR 引擎） | 48 | 314,528 | 658 |
| libc++ / libc++abi / PS5 运行时 | — | ~161,900 | 20,738 |
| **C++ 异常机制** | — | **151,288** | — |
| zlib | 8 | 65,211 | 336 |
| 7z SDK + 自研链 | 30 | 85,406 | 56 |
| minizip-ng | 8 | 39,382 | 384 |
| 自有代码 `src/` | 14 | 60,653 | 472 |
| libmicrohttpd | — | 44,647 | — |
| 前端资源（gzip 后） | 13 | 40,278 | 104 |

---

## 二、最大的单一发现：151 KB 的 C++ 异常机制

`nm` 统计出 **607 个 `itanium_demangle::*` 符号，合计 105,431 B** —— 这是 libc++abi 的
C++ 名字还原器（`__cxa_demangle`），单独一块就占了整个 ELF 的 **10.2%，比 zlib 整个库还大**。

它是怎么被拉进来的：

1. `third_party/unrar7/dll.cpp` 用了 `catch (RAR_EXIT)` / `catch (std::bad_alloc&)`
   （`unpack.cpp` / `model.cpp` 里也有 `throw std::bad_alloc()`）
2. 只要 C++ 异常运行时存在，libc++abi 的 `__cxa_throw` 链路就会引用 `__cxa_demangle`
   （用于打印未捕获异常的类型名）
3. 链接器于是把整个 `cxa_demangle.cpp` 拉进来 —— 一个深度内联的模板解析器，
   展开成 607 个函数

连带被拖进来的还有 `libunwind`（21,953 B，栈回溯）和异常胶水（23,904 B），
以及散落在每个 C++ 目标文件里的 `.eh_frame`（58,904 B）/ `.gcc_except_table`（8,500 B）。

**但这个 demangler 只在「未捕获异常」的诊断路径上才会被调用。**
我们的 unrar7 走 DLL 模式，所有异常都在 `dll.cpp` 内部被 catch 掉，
程序逻辑永远走不到那条路径。

---

## 三、实测（同一 WSL、同一份源码、同一个链接器）

| # | 变更 | 结果 | 差值 |
|---|---|---:|---:|
| E0 | 重新链接（校验基线） | 1,034,328 | 0 |
| E1 | `-Wl,--icf=all` | 1,017,944 | −16,384 |
| E2 | 注入 `__cxa_demangle` 桩 | 886,872 | **−147,456** |
| E3 | 桩 + `--icf=all` | 870,488 | **−163,840** |
| E4 | `-Wl,-z,pack-relative-relocs` | 985,248 | −49,080 |
| E5 | `-Wl,-z,noseparate-code` | 1,034,328 | 0（无效） |
| E7 | 桩 + ICF + RELR | 854,176 | −180,152 |
| E8 | 第三方全部改 `-Oz` | 969,504 | −64,824 |

> E4 单独能省 49 KB，但与 ICF 组合后只剩 16 KB —— ICF 已经合并掉了一批重定位。

---

## 四、落地方案

### 4.1 立即可用：`__cxa_demangle` 桩（−147 KB）

新增 `src/demangle_stub.c`：

```c
/* 只提供 __cxa_demangle 的桩，让 libc++abi 里 105 KB 的名字还原器
 * 不被链接进来。该函数只在打印「未捕获异常的类型名」时被调用；
 * 返回 NULL 时调用方退回打印 mangled 名，不影响任何业务流程。 */
#include <stddef.h>

char *__cxa_demangle(const char *mangled, char *buf, size_t *len, int *status)
{
    (void)mangled; (void)buf; (void)len;
    if (status) *status = -1;
    return NULL;
}
```

Makefile 里把它加进 `COMMON_SRCS`（PS5 与 Linux 两条链路都受益）：

```make
COMMON_SRCS := src/main.c src/websrv.c ... src/sevenz_mt.c src/demangle_stub.c
```

**原理**：链接器解析 `__cxa_demangle` 引用时，命令行上的 `.o` 优先于归档成员，
所以 `libc++abi.a` 里的 `cxa_demangle.o` 压根不会被取出。

**安全性（已实测验证）**：

| 检查项 | 结果 |
|---|---|
| `__cxa_demangle` 本体大小 | **11 B**（我们的桩；原 demangler 入口是 1 701 B） |
| `itanium_demangle::*` 符号残留 | **0** |
| `__cxa_throw` | 存在 |
| `__cxa_begin_catch` / `__cxa_end_catch` | 存在 |
| `_Unwind_Resume` / `__gxx_personality_v0` | 存在 |

**唯一的行为变化**：万一真的出现未捕获异常，`std::terminate` 打印的是 mangled 名
而不是可读名。解压逻辑、错误码、进度上报、HTTP 服务一概不受影响。

### 4.2 立即可用：ICF 代码折叠（−16 KB）

```make
LDFLAGS := -Wl,--gc-sections -Wl,--icf=all
```

lld 的 identical code folding，合并字节完全相同的函数。工具链已确认为 LLVM LLD 18。
建议只加在 PS5 的 `LDFLAGS`，不动 Linux 链路（GNU ld 的 `--icf` 支持不完整）。

### 4.3 需真机验证：RELR 重定位压缩（−16 KB）

```make
LDFLAGS += -Wl,-z,pack-relative-relocs
```

把 2,119 条 `R_X86_64_RELATIVE`（24 B/条）压成 RELR 位图格式（8 B/条）。

**风险**：需要 PS5 的 ELF 加载器认得 `.relr.dyn`。如果加载器只处理 `.rela.dyn`，
重定位根本不会执行 —— 表现是启动即崩。**先在一台机器上验证再推广。**

### 4.4 不建议作为默认项

| 项 | 收益 | 为什么不默认开 |
|---|---:|---|
| 第三方改 `-Oz` | −65 KB | 作用于 LZMA / Deflate / RAR 的热循环，解压速度有下降风险。要用先跑 `tests/bench_driver.py` 对比 |
| 关掉 PPMd（`-DZ7_PPMD_SUPPORT`） | −10 KB | PPMd 压缩的 7z 就解不开了 —— 违背"功能完整" |
| 去掉 zlib `deflate`（只留 inflate） | −18 KB | `mz_strm_zlib_write` 引用了它，需要桩或改库，收益/风险不划算 |

---

## 五、还能挖的（未实测，仅估算）

| 项 | 预估 | 代价 |
|---|---:|---|
| unrar7 去掉 C++ 异常（`throw`/`catch` 改错误码 + `-fno-exceptions -fno-rtti`） | −100~110 KB | 改 vendored 代码，需回归 163 checks。可回收 `.eh_frame` 59 KB + `.gcc_except_table` 8.5 KB + libunwind 22 KB + 异常胶水 |
| LTO（`-flto=thin`） | −30~60 KB | 全量重编，第三方 `.o` 需统一编译选项；有一定概率反而提速 |
| 前端资源改 LZMA 压缩（复用已有解码器，替代 gzip） | ~−10 KB | 改 `gen-asset-module.py` + `asset.c` |

理论极限在 700 KB 上下（−32%），但边际成本递增：4.1 + 4.2 用 20 行代码换 164 KB，
而再往下 100 KB 要动 vendored 源码或验证加载器行为。

---

## 六、推荐执行顺序

1. ~~落 4.1 + 4.2 → 构建~~ ✅ **已完成（2026-09-20）**，产物 870,488 B
2. 跑 163 checks（`tests/run-tests.sh` + `tests/run-sevenz-tests.sh`）确认无回归
3. PS5 真机跑一轮 ZIP / RAR / 7z（含分卷）解压，确认行为不变
4. 真机验证 4.3（RELR）后再决定是否加入
5. 有需要再评估第五节的三项

> 本次改动只动链接期（新增一个 TU + 一个 lld 参数），未触碰任何解压逻辑，
> 因此 163 checks 的预期是"逐条不变"。

**结果（2026-09-20）**：163 checks（ZIP 108 + RAR 27 + 7z 28）**0 失败**，
`aeshe` 仍是已知的 `-mhe=on` 缺口。README / CHANGELOG / HANDOVER / 论坛帖里的
产物指纹已同步为 870,488 B · sha256 `177e90fe…8e84`。

---

## 附录 A：v1.9.2 产物一致性验证（2026-09-20）

v1.9.2 是一次"只改内嵌版本号"的重发（原 `v1.9.1` tag 落后产出发布二进制的提交
4 个提交）。为确认这次重发**真的**只动了版本号，在 WSL 里做了下面的验证。

### A.1 复现性实验（决定性证据）

当前工作区相对 `HEAD` 只有两处改动：`Makefile` 的 `VERSION_TAG` 与
`assets/main.js` 的 `APP_VERSION_FALLBACK`。把这两处用 `sed` 回退成 `v1.9.1`
后重新构建：

| 构建 | sha256 |
|---|---|
| 已发布的 v1.9.1 ELF | `24392aff6ddcca4dc0ea969cce356bd693ac52efe8a117d61ee1c814aa43cd07` |
| 回退后重建的产物 | `24392aff6ddcca4dc0ea969cce356bd693ac52efe8a117d61ee1c814aa43cd07` |

**逐字节相同。** 再恢复 `v1.9.2` 重构，sha256 也精确回到 `177e90fe…8e84`。

→ 构建是**确定性**的，因此 v1.9.2 与 v1.9.1 的全部差异就等于那两处版本字面量。
复现脚本：`.build/_repro.sh`。

### A.2 为什么原始字节 diff 有 5.5 万字节 —— 别被吓到

`cmp` 两个 ELF 会看到 **55,280 字节不同（6.35%）**，但这是链接器字符串池重排的
副作用，不是代码变了：

| section | 差异字节 | 占该 section |
|---|---:|---:|
| `.rodata` | 53,496 | 34.8% |
| `.text` | 1,543 | 0.3% |
| `.rela.dyn` | 241 | 0.9% |

而**每个 section 的尺寸完全相同**（`.text` 538,336 = 538,336），段数也都 17 个。

机制：`.rodata` 里 7 字节的 `"v1.9.1\0"` 被换成 `"v1.9.2\0"` 后落点变了，其后
所有字符串整体平移 7 字节 → 指向它们的 `lea rdi,[rip+disp]` 位移和 `.rela.dyn`
重定位加数全部跟着变。

两条量化证据：

| 检查 | 结果 |
|---|---|
| 指令**助记符**序列（`objdump -d --no-show-raw-insn` 只取 mnemonic） | 141,780 条 vs 141,780 条，**完全一致** —— 没有任何指令被增删改 |
| `.text` 差异字节的增量分布 | 1,543 个里 **1,506 个恰好是 −7**（正是那个 7 字节平移）；`.rela.dyn` 241/241 个 8 字节字段减 7 |
| 嵌入的 gzip 资产 | 6 个成员，5 个逐字节相同，唯一不同的是 `main.js`，且差异 = `APP_VERSION_FALLBACK` 那一行 |

脚本：`.build/_diffmap.py`、`.build/_operandcheck.sh`、`.build/_fieldcheck.py`、
`.build/_verify_v192b.py`、`.build/_seccmp.py`。

### A.3 源码层的约束

`VERSION_TAG` 在源码里**只出现在字符串上下文**：

```
src/version.c:29   json_escape(&b, VERSION_TAG);
src/main.c:127     printf("version: %s\n", VERSION_TAG);
src/main.c:147     notify_user("Web File Manager\nVersion: %s\nPort: %u", VERSION_TAG, port);
```

没有任何算术、比较或分支依赖它，因此改版本号在语言层面就不可能改变控制流。

---

## 附录 B：瘦身逐符号账目

在 **同一个 Makefile / 同一个 `VERSION_TAG`（v1.9.2）** 下重建三个变体，差异只落在
"有没有桩"和"有没有 ICF"这两处，因此是干净的 A/B/C 对照。

| 变体 | 内容 | stripped | unstripped |
|---|---|---:|---:|
| `base` | 无桩、无 ICF（瘦身前） | **1,034,328** | 1,222,752 |
| `nicf` | 有桩、无 ICF | **886,872** | 1,010,208 |
| `new` | 有桩 + `--icf=all`（发布态） | **870,488** | 993,824 |

拆分：桩贡献 **−147,456 B**，ICF 再贡献 **−16,384 B**，合计 **−163,840 B（−15.8%）**。

### B.1 section 位移（base → new）

| section | base | new | 差值 |
|---|---:|---:|---:|
| `.text` | 637,616 | 538,336 | −99,280 |
| `.rela.dyn` | 54,816 | 25,800 | −29,016 |
| `.eh_frame` | 58,904 | 43,972 | −14,932 |
| `.data.rel.ro` | 20,192 | 9,328 | −10,864 |
| `.rodata` | 162,016 | 153,664 | −8,352 |
| `.eh_frame_hdr` | 12,540 | 9,108 | −3,432 |
| `.gcc_except_table` | 8,500 | 7,364 | −1,136 |
| `.dynsym` / `.dynstr` / `.got` | — | — | −24 / −9 / −8 |

### B.2 符号集合差

| 项 | 数量 |
|---|---:|
| base 定义符号 | 2,657 |
| new 定义符号 | 2,029 |
| base → new **消失** | **628** |
| base → new **新增** | **0** |

628 个消失符号的构成：

- `itanium_demangle::*` —— **607**
- `GCC_except_table*` —— **21**（上面那批代码自己的异常表标签，不是独立函数）

### B.3 桩本体与 demangler 符号

| 变体 | `__cxa_demangle` 符号大小 | `itanium_demangle` 符号数 |
|---|---:|---:|
| base | 1,701 B（真身） | 607 |
| nicf | **11 B**（我们的桩） | **0** |
| new | **11 B** | **0** |

异常机制在所有三个变体里都完好：`__cxa_throw` / `__cxa_begin_catch` /
`__cxa_end_catch` / `_Unwind_Resume` / `__gxx_personality_v0` /
`__cxa_allocate_exception` / `__cxa_free_exception` 各 1 个，无变化。

自有 `src/` 关键符号（`ctx_fail` / `rarx_fail` / `szx_fail` / `fnv1a` /
`nameset_init` / `remove_tree` / `ensure_parent_dirs` / `zipx_volume_detect` /
`sevenz_extract` / `rar_extract` / `filemgr_api_request` 等）base 与 new 数量一致。

### B.4 ICF 折叠了什么

**符号数 2,029 → 2,029，一个没少** —— ICF 是"合并"不是"删除"。共 **76 个折叠组**，
全部含具名符号。典型几类：

- 我们自己的同码副本：`ctx_fail == rarx_fail == szx_fail`、
  `nameset_init == szx_nameset_init`、`fnv1a == rarx_fnv1a == szx_fnv1a`、
  `remove_tree == szx_remove_tree`
- C++ 的 `C1 == C2` / `D1 == D2` 构造析构对（编译器为同一函数生成两个 ABI 入口）：
  `_ZN10CmdExtractC1EP11CommandData == ...C2...`、`_ZN4FileD1Ev == _ZN4FileD2Ev` 等
- 只读常量表：`Sbox == _ZL1S`（AES 表在 `rijndael.cpp` 与 `Aes.c` 各一份）、
  `SHA256_K_ARRAY == _ZL1K`、`PPMD7_kExpEscape == _ZL9ExpEscape`
- RARDLL 模式下被置空的 UI 函数、`mz_stream_read_int64 == read_uint64`、
  libunwind 的 `__unw_* == unw_*`、`__unw_resume == unw_resume`

**风险提示**：`--icf=all` 是 LLD 的激进模式，**不做地址敏感性检查**
（`--icf=safe` 才会读 `.llvm_addrsig` 跳过被取地址的函数）。逐组核对下来这 76 组
都是同码副本、没有"比较函数/常量表地址"的用法 —— 但这是人工判断，不是编译器给的
保证。想绝对保守就把 `--icf=all` 换成 `--icf=safe`，代价是少省几 KB。

复现脚本：`.build/_whatremoved_v192.sh`（一次跑完三个变体 + 全部核对）。
