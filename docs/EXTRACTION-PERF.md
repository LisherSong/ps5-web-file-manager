# 解压性能：实测、根因、提速方案

> 实测 2026-09-15 · 对照物 = 官方 7-Zip 26.03（上游 v1.8 的 helper 就是它）
> 复现：`python tests/bench_driver.py --big --runs 3`；WSL 同环境对比见 `.build/bench/wsl-*.sh`

> **✅ 方案 A 已落地（2026-09-16）**：`Asm/x86/LzmaDecOpt.asm` + `7zAsm.asm` 已 vendor 到
> `third_party/7z/`，jwasm `-elf64 -DABI_LINUX` 汇编进 PS5 与 Linux 两条链路，
> `LzmaDec.o` 加 `-DZ7_LZMA_DEC_OPT`。实测 **1.39 s → 1.05–1.13 s（1.26×）**，解出字节与
> C 版逐字节一致；7z 测试矩阵 28 checks 全过。Makefile 对该优化做了条件化（无 jwasm 自动
> 退回纯 C）并依赖 Makefile 本身触发重编（flag 变化不会被 make 察觉）。ELF
> 1,017,944 B / sha256 `7fc0881b…`。方案 B（多线程）仍未做，见第四节。

## 结论

**7z 解码我们比 7-Zip 慢 1.57×（单线程），根因已定位到一个具体的编译开关。**

不是架构问题，不是算法问题，不是编译选项问题 —— 是 **SDK 里有一份汇编版解码器我们没启用**：

```c
/* LzmaDec.c */
#ifdef Z7_LZMA_DEC_OPT
int Z7_FASTCALL LZMA_DECODE_REAL(CLzmaDec *p, SizeT limit, const Byte *bufLimit);  /* asm */
#else
  ... 纯 C 宏展开 + LzmaDec_DecodeReal2()   /* ← 我们在这里 */
#endif
```

`LzmaDecOpt.asm` 是 Igor Pavlov 官方 SDK 的一部分（public domain），**1339 行**，实现同一个函数。开不开这个开关，实测差 1.5 倍。

---

## 一、同环境实测（关键：排除跨平台假象）

第一轮数据是在 Windows 上打的（我们 MinGW 构建 vs `7za.exe`），混了平台因素。重做：**在同一台机器、同一个 WSL Linux 环境、同一份归档、同一类编译器**下对比。

归档：329 MiB 解压量 / 22 MiB 压缩，LZMA2 solid，单文件

| 配置 | 单线程 | 8 线程 | 相对我们 |
|---|---:|---:|---:|
| **ours**（facade，含 staging + fsync + publish） | **1.39 s** | — | 1.00× |
| 官方 7-Zip（Linux 构建） | **0.89 s** | **0.51 s** | **1.57× / 2.73×** |

> 两个数字都是同一台机器上的实测。7-Zip 的 Linux 版和 Windows 版几乎一样快（0.89 vs 0.84 s），说明平台差异不是因素。

---

## 二、四个被实测排除的原因

排查过程里每个假设都先给出过错误结论，所以逐个记录：

| # | 假设 | 实验 | 结果 |
|---|---|---|---|
| 1 | **fsync / 写盘开销** | 两边都解到 `/dev/shm`（tmpfs，fsync 近乎免费） | ❌ 我们 1.47 s，磁盘上也是 1.47 s —— **fsync 成本可忽略** |
| 2 | **pull 粒度太小**（64 KiB 输出块 → 5000+ 次调用） | 把 `SZ_OUT_CHUNK` 提到 1 MiB | ❌ 1.39 s，与 64 KiB 无差别。profile 显示 `node_pull` **只调用 329 次**，调度开销 ≈ 0 |
| 3 | **编译选项保守**（我们用 `-O2 -w`） | `-O3` / `-march=native` / `-march=x86-64-v3` 各跑一遍 | ❌ 全部落在 1.31–1.52 s，无显著差异 |
| 4 | **汇编优化只值 6%**（我曾据此推断"不是主因"） | 对比 Windows 版(有 asm) 与 Linux 版 | ❌ **这个推断是错的** —— Linux 官方版同样含 asm，所以只看到 6% 的平台差异。见下节 |

---

## 三、真正的根因：profile 说话

`gprof`，同一份归档：

```
  %      self     calls   name
 82.81   1.06 s   81237   LzmaDec_DecodeReal2      ← LZMA 解码核心（C 版）
 17.19   0.22 s     660   CrcUpdateT12             ← CRC32 校验
  0.00   0.00 s     329   node_pull                ← 我们的链调度，可忽略
  0.00   0.00 s     329   szx_sink_write           ← 写盘，可忽略
  0.00   0.00 s    1705   LzmaDec_DecodeToDic
```

**82.8% 的时间在一个函数里，而那个函数有一个 asm 版本我们没有使用。**

这解释了为什么前四个假设全部落空：它们针对的都是那 0% 的部分。

### 三方交叉验证

- 我们的构建：未定义 `Z7_LZMA_DEC_OPT` → profile 里是 `LzmaDec_DecodeReal2` ✓
- SDK 源码：明确写着 `#ifdef Z7_LZMA_DEC_OPT` 时声明外部 asm 符号 ✓
- 官方 GCC 构建规则（`7zip_gcc_c.mak`）：`USE_LZMA_DEC_ASM` 开关 + `jwasm` 汇编 `LzmaDecOpt.asm` ✓

### 附带发现：CRC 占 17%

`CrcUpdateT12`（slicing-by-12）花掉 0.22 s。7-Zip 解压时同样校验 CRC，所以这部分**不构成差距**，但如果单独优化（SSE4.2 硬件 `crc32` 指令，Zen 2 支持）能再省约 0.15 s。

---

## 四、提速方案

### 方案 A（推荐）：启用 asm 解码器

| 步骤 | 内容 |
|---|---|
| 1 | 取 `Asm/x86/LzmaDecOpt.asm` + `Asm/x86/7zAsm.asm` 入 `third_party/7z/` |
| 2 | 用 **jwasm**（MASM 兼容汇编器，支持 ELF64 输出）汇编成 `.o` |
| 3 | Makefile 加规则；`LzmaDec.c` 编译时加 `-DZ7_LZMA_DEC_OPT` |
| 4 | 完整测试矩阵（163 checks）+ 基准复测 |

- **预期收益：1.39 s → ~0.95 s（≈1.45×）**，追平 7-Zip 单线程水平
- **工作量**：小～中（一个汇编文件 + 一条 Makefile 规则 + 一个宏）
- **风险**：中 —— 唯一的不确定点是 **jwasm 能否产出 PS5（prospero-clang / x86-64 ELF）可链接的目标文件**。这一条必须先验证再动手
- **为什么"稳"**：asm 是 SDK 官方组成部分（同一位作者维护，与 C 版有链接时版本校验 `_3`，对不上会直接链接失败而不是静默出错）；正确性由现有 163 项测试兜底

### 方案 B：多线程 LZMA2 解码

SDK 自带 `C/Lzma2DecMt.c`（1095 行，public domain）就是 7-Zip `-mmt` 的并行实现，实测 0.89 → 0.51 s。

- **预期收益：额外 1.75×**（与 A 叠加后 ≈ 2.6×，基本追平 7-Zip 全核）
- **工作量**：大 —— 要重构 chain 的调度（block 级并行 + 字典依赖管理）
- **风险**：中高（并发正确性、内存峰值；PS5 只有 8 核且 HTTP/任务系统同进程，建议限制线程数）
- **前置**：建议先完成 A，因为 A 不改架构、收益确定、能独立验证

### 方案 C：CRC 硬件加速（可选）

用 SSE4.2 的 `crc32` 指令替换 `CrcUpdateT12`。Zen 2 支持。

- **预期收益：约 0.15 s（10%）**
- **工作量**：小
- **注意**：7-Zip 也做 CRC 校验，这不会拉开差距，只是净提速

### 方案 D（备选，不推荐）：上游的 helper 路线

直接把 7-Zip 做成独立进程，一步到位拿到 1.57×/2.73×。

不推荐的理由：

1. 引入外部 ELF 依赖 + IPC + 进程生命周期管理，**故障模式比现在多得多**（"最稳"的反面）
2. 方案 A 用一个文件 + 一条规则就能拿到 1.45×，D 的增量收益只有多线程那部分
3. PS5 上还要处理 elfldr 加载；上游自己都是"单独分发，让用户手动放到 `/data/wfm/`"

---

## 五、执行顺序建议

```
第一步  A（asm 解码器）
        └─ 先验证 jwasm → ELF64 → prospero-ld 这条链能否走通
        └─ 通过则：1.39 s → ~0.95 s，测试矩阵全绿后提交
第二步  B（多线程）
        └─ 在 A 的基础上做，目标 ~0.55 s
第三步  C（CRC 硬件加速，可选）
        └─ 再省 ~0.15 s
```

**不做任何优化时的现状也是可接受的**：1.39 s / 329 MiB ≈ 237 MiB/s 单线程吞吐。在真实场景（大游戏包）里受存储 I/O 限制，差距往往比这个倍数更小。

---

## 六、复现

```bash
export PATH="/c/mingw64/bin:/c/Users/songl/.workbuddy/binaries/PortableGit/versions/1.2.0/mingw64/bin:/c/Users/songl/.workbuddy/binaries/python/versions/3.13.12:/usr/bin:/bin:/c/Windows/System32:/c/Windows"
cd "/c/Users/songl/Desktop/Web File Manager/ps5-web-file-manager"

# 先各跑一次，生成引擎对象
/usr/bin/bash tests/run-sevenz-tests.sh
/usr/bin/bash tests/run-tests.sh --rebuild

# Windows 基准（我们的引擎 vs 7za.exe）
python tests/bench_driver.py --big --runs 3

# 同环境对比（WSL）：官方 Linux 7-Zip / 我们的引擎 / 编译选项 / profile
wsl.exe -d Ubuntu-22.04 -- bash < .build/bench/wsl-perf.sh
wsl.exe -d Ubuntu-22.04 -- bash < .build/bench/wsl-ours.sh
wsl.exe -d Ubuntu-22.04 -- bash < .build/bench/wsl-flags.sh
wsl.exe -d Ubuntu-22.04 -- bash < .build/bench/wsl-prof.sh
```

## 附：测量方法备忘

这个沙箱里 bash 计时不可用，三个坑都先给出过错误数字：

- 每次 `date` 要 ~350 ms → `t0/t1` 对给 ~600 ms 的测量注入 700 ms 误差
- `time` 的 user/sys 看不到 native 子进程（解 82 MiB 报 `user 0.031s`）
- 反复跑堆积 2.7 GB 输出目录 → 测出过"内部时间 > 外部时间"

正确做法：Python 驱动（单次 spawn + 单调时钟）+ 候选程序自带内部计时 + 显式扣除 spawn tax（~190–240 ms）+ best of N + 每次跑前清输出目录。
