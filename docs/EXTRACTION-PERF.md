# 解压性能：实测、根因、提速方案

> 实测 2026-09-15 · 对照物 = 官方 7-Zip 26.03（上游 v1.8 的 helper 就是它）
> 复现：`python tests/bench_driver.py --big --runs 3`；WSL 同环境对比见 `.build/bench/wsl-*.sh`

> **✅ 方案 A 已落地（2026-09-16）**：`Asm/x86/LzmaDecOpt.asm` + `7zAsm.asm` 已 vendor 到
> `third_party/7z/`，jwasm `-elf64 -DABI_LINUX` 汇编进 PS5 与 Linux 两条链路，
> `LzmaDec.o` 加 `-DZ7_LZMA_DEC_OPT`。实测 **1.39 s → 1.05–1.13 s（1.26×）**，解出字节与
> C 版逐字节一致；7z 测试矩阵 28 checks 全过。Makefile 对该优化做了条件化（无 jwasm 自动
> 退回纯 C）并依赖 Makefile 本身触发重编（flag 变化不会被 make 察觉）。
>
> **✅ 方案 B 已落地（2026-09-16）**：`Lzma2DecMt.c` + `MtDec.c` + `Threads.c` 已 vendor，
> 单一纯 LZMA2 folder（7-Zip 默认布局）走 SDK 并行解码器（`src/sevenz_mt.c` 适配层），
> 8 线程 + 1 MiB inBufSize_MT；`SZ_ERROR_THREAD` 自动降级回单线程 chain（BCJ2/加密/奇异
> 布局本来就由 chain 负责）。实测 329 MiB：1.05 s → **0.77 s（1.37×）**，与 7za -mmt=off
> 打平（898 ms）；7za -mmt=8 = 485 ms。7z/ZIP/RAR 163 checks 全绿。
>
> **✅ ZIP 引擎逐条目 fsync 移除（2026-09-16）** —— 原计划写的是「批量化（每 64MB/N 条刷一次）」，
> **实际落地改为彻底移除**：publish 是纯 rename、又没有续解功能，逐条目 fsync 换不到任何东西
> （RAR/7z 引擎本来就没有，三引擎现在统一为「不 sync、只 rename」）。8000 文件 fixture：fsync 版
> \>200 s 未完成 → 无 fsync **14.5 s（≥14×）**。同机官方 7-Zip 反而要 >400 s（Defender
> 实时扫描逐文件查杀；PS5 无此因素）。
>
> 与 §二 排除 1 不矛盾：那里测的是**单一大文件**归档，一次 fsync 本来就近乎免费；这里是
> **8000 个文件**，成本随条目数线性叠加（且 PS5 无 Defender，比例只会更极端）。
> 已知取舍：publish 之后到落盘之间断电，会出现「文件在但内容不完整」；要补只需在 extract
> 收尾做**一次**目录/整盘 flush（PS5 是 FreeBSD 系，`syncfs()` 不一定有，`sync()` 是全盘、偏重）。
> 代码现状见 `src/zip_extract.c:937-945`。

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
| **ours**（facade，含 staging + publish；当时仍含逐条目 fsync，2026-09-16 已移除） | **1.39 s** | — | 1.00× |
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

`CrcUpdateT12`（slicing-by-12，**纯软件实现**）花掉 0.22 s。

> ⚠️ **2026-09-23 更正**：本节原先写着「7-Zip 解压时同样校验 CRC，所以这部分**不构成差距**」——**这条是错的**。
> 依据是本仓 vendor 的官方构建规则 `third_party/7z/7zip_gcc_c.mak:298-310`：
> ```
> ifdef USE_X86_ASM
> $O/7zCrcOpt.o: ../../../Asm/x86/7zCrcOpt.asm     ← 官方走这条：汇编版
> else
> $O/7zCrcOpt.o: ../../7zCrcOpt.c                  ← 我们走这条：纯 C
> ```
> 而 `CpuArch.h:691` 的 `CPU_IsSupported_CRC32()` 说明那份汇编就是 SSE4.2 硬件 `crc32`。
> 我们的 `third_party/7z/Asm/x86/` 里**只有** `7zAsm.asm` 与 `LzmaDecOpt.asm`，**没有 `7zCrcOpt.asm`**。
> 所以这 17% **是真实差距的一部分**，不只是「可选的净提速」；它同时还是 MT 路径的**串行瓶颈**
> （`src/sevenz_mt.c` 的 `mt_seq_write` 在调用线程上算 CRC，Amdahl 意义上压住了多线程上限）。
>
> **2026-09-23 实测补充（`.build/_crcbench.c`，本机 MinGW x64，329 MiB 同一块数据，跑两次）**：
> `CrcUpdateT12` **3.18–3.51 GB/s**（329 MiB → 0.098–0.109 s），zlib `crc32` **2.59–2.80 GB/s**，
> SSE4.2 `crc32` 指令（直线写法）**6.03–6.11 GB/s**。
> → CRC 实际只占单线程 1.39 s 的 **≈7%**，上面那个「0.22 s / 17%」**大概率是 gprof 插桩放大的**
> （`-pg` 对纯循环函数特别吃亏）。**本节往下请按 ≤7% 理解，不要再引用 17%。**
> 另外实测挖出一个**会算错的陷阱**，见「方案 C」。

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

### 方案 C：CRC 加速（**实测后收益大幅缩水，且有一个会算错的陷阱**）

`.build/_crcbench.c` 实测（本机 MinGW x64，329 MiB 同一块数据，两次运行）：

| 实现 | 吞吐 | 329 MiB 耗时 | 谁在用 |
|---|---:|---:|---|
| `CrcUpdateT12`（slicing-by-12） | 3.18–3.51 GB/s | 0.098–0.109 s | 我们：7z chain / MT 输出 / 逐条目 CRC |
| zlib `crc32` | 2.59–2.80 GB/s | 0.123–0.133 s | 我们：ZIP 引擎 |
| SSE4.2 `crc32` 指令（直线写法） | 6.03–6.11 GB/s | 0.056–0.057 s | 候选 |

> ⚠️ **陷阱：x86 的 `crc32` 指令算的是 CRC-32C（Castagnoli），不是三个格式要的 IEEE CRC-32。**
> 同一份基准里的判定（标准向量 `"123456789"`）：
> `_mm_crc32_*` 得 **`0xE3069283`（CRC-32C）**，而 `CrcCalc` / zlib 得 **`0xCBF43926`（IEEE）**。
> 所以**不能把 `CrcUpdate` 直接换成 `_mm_crc32_u64`** —— 必须补一个多项式转换
> （GF(2) 上的 32×32 矩阵），或改用 pshufb / PCLMULQDQ 手写 IEEE 并行 CRC。
> **这正是官方 `Asm/x86/7zCrcOpt.asm` 存在的意义**：它不是两行 intrinsic 包装。
> UnRAR 那边同理 —— `third_party/unrar7/crc.cpp` 的硬件路径是 `USE_NEON_CRC32`（**ARM 专属**），
> x86 上是 slicing-by-16 纯软件。

**收益重估（按实测）**：CRC 只占单线程 ≈7%（0.098 s / 1.39 s）。硬件指令直线写法 1.8× 于
slicing-by-12，但还要扣掉多项式转换的开销 → 乐观估计单线程省 **0.05–0.07 s ≈ 4–5%**；
MT 路径里它是串行分量（`sevenz_mt.c:64`），按 0.098 s 算 0.77 s → 约 0.72 s（≈6%），
**不足以解释 8 线程下与 `7za -mmt=8`（0.485 s）的 1.6× 差距**。

**结论**：这仍然是**我们与 7-Zip 差距里确定存在**的一块（官方走 `USE_X86_ASM` 分支编汇编版，
我们走 `else` 编纯 C），但**收益是个位数百分比，不是 10–15%，实现也不平凡**。
风险倒是低：CRC 算错会**响亮失败**（每个条目报 `ZIPX_ERR_CRC`），现有 177 + 27 项测试会立刻抓住，
不会静默写坏数据。**优先级从「最高」降为「可做，但别指望它拉平差距」。**

### 方案 D（备选，不推荐）：上游的 helper 路线

直接把 7-Zip 做成独立进程，一步到位拿到 1.57×/2.73×。

不推荐的理由：

1. 引入外部 ELF 依赖 + IPC + 进程生命周期管理，**故障模式比现在多得多**（"最稳"的反面）
2. 方案 A 用一个文件 + 一条规则就能拿到 1.45×，D 的增量收益只有多线程那部分
3. PS5 上还要处理 elfldr 加载；上游自己都是"单独分发，让用户手动放到 `/data/wfm/`"

---

## 五、执行顺序建议

```
第一步  A（asm 解码器）                        ✅ 已落地 2026-09-16，实测 1.26×
        └─ 先验证 jwasm → ELF64 → prospero-ld 这条链能否走通   ✅ 走通了
第二步  B（多线程）                            ✅ 已落地 2026-09-16，实测 1.37×
        └─ 在 A 的基础上做，目标 ~0.55 s      ⚠️ 实际 0.77 s：仍慢于 7za -mmt=8 的 0.485 s
第三步  C（CRC 加速）                          ⬜ 未做 —— 但**实测后收益降到 4–5%**，且有 CRC-32C 陷阱
        └─ 官方走 USE_X86_ASM 编汇编版，我们编纯 C，确是真实差距；但不值得为它冒险
```

> **2026-09-23 补充**：B 之后我们与 `7za` 的对比是 0.77 s vs 单线程 0.898 s / 8 线程 0.485 s。
> 也就是说单线程已打平，**8 线程下仍差约 1.6×**。
> 曾把这段差距归给「没做的 CRC 硬件化」，但实测否掉了这个假设：CRC 全程只占 ≈7%（**且那是
> gprof 放大后的口径，实测 0.098 s 更小**），把它全消掉也只值 4–5%，撑不起 1.6×。
> 更可能的来源是 BCJ2 / 7zAES 布局仍走单线程 chain、以及 `Lzma2DecMt` 自身的线程扇出效率
> —— 两者都要真机 profile 才能定性。
> 其余候选（ZIP inflate 换 libdeflate、条目级并行、AES-NI）同理，均属「收益不可预期」或「工程量大」。

> **⚠️ 2026-09-23 限定：上面这个 1.59× 是在「最有利的输入形状」上测出来的，不能外推到真实归档。**
>
> 先看基准归档到底是什么（`tests/bench_driver.py:156-179` + payload 构造 `:144-156`）：
>
> ```
> payload  = 文本块 ×240 → 一个大文件；再拼 ntoskrnl.exe ×30 → payload_mix.bin
> 7za add  = -t7z -m0=lzma2 -mx=5 -ms=on
> ```
>
> 也就是 **1 个条目 / 1 个 solid folder / 1 个纯 LZMA2 coder / 未加密**。而 `sz_chain_lzma2_root()`
> （`src/sevenz_chain.c:750-762`）要求**恰好** `num_coders == 1 && num_bonds == 0 &&
> num_pack_streams == 1 && method == LZMA2` 才走 MT —— 换句话说，这个 fixture 是**唯一能让我们的
> MT 生效的形状**，也是 7-Zip 拿不到任何结构优势的形状。它把 per-entry 开销（我们的强项）和
> 非 LZMA2 布局（我们的弱项）**同时排除在外**了。
>
> 真实 PS5 归档（游戏包 / repack）几乎全是相反的形状：
>
> | 真实特征 | 对我们的后果 | 对 7-Zip 的后果 |
> |---|---|---|
> | 几千～几万个条目 | per-entry 开销主导（这块我们反而大幅领先，见上方 8000 文件数据） | 同样逐条目，无优势 |
> | `-ms=off` / 超大归档切块 → **多 folder** | `num_pack_streams != 1` → **MT 失效**，退回单线程 chain | 跨 folder/块并行，`-mmt` 照常吃满 |
> | exe/dll 用 **BCJ2** | `num_coders != 1` → **MT 失效** | 照常多线程 |
> | **7zAES 加密** | `sz_chain_needs_password` → **MT 失效** | 照常多线程 |
>
> 结论：**1.59× 既不是上限也不是下限**，真实方向未知 —— 可能因 I/O 与 per-entry 成本被摊薄到无关，
> 也可能因为整条解码退回单线程而比 1.59× 更糟。**在拿到真实归档上的 profile 之前，任何一侧的
> 断言都是猜的。**
>
> 同样，下面这句原话也**未经验证**，暂按假设保留：
> 「在真实场景（大游戏包）里受存储 I/O 限制，差距往往比这个倍数更小」——它成立的前提是解码项
> 只占 wall-clock 的一小部分；按 0.77 s / 329 MiB ≈ 427 MiB/s 的解码吞吐与 PS5 存储带宽同量级来算，
> 这个前提**未必成立**。

**不做任何优化时的现状也是可接受的**：1.39 s / 329 MiB ≈ 237 MiB/s 单线程吞吐；ZIP/RAR 两个引擎
已分别压过/追平各自的官方实现，7z 单线程与 `7za -mmt=off` 打平。

---

## 六、真机首次实测（2026-09-23）

> ### ⚠️⚠️ 第二次更正（2026-09-23 深夜）：拿到**真实归档的参数**，并实测了 MT
> 用户给了 `D:\PPSA16608-e.part{1,2,3}.rar`，其中 **part3 当时还在盘上**，我用 WinRAR 7.23 的
> `UnRAR lt` 直接读了它的头（该文件随后被用户清理掉，故下列数字是**一次性的实测记录**）：
>
> | 项 | 实测值 |
> |---|---|
> | 格式 | **RAR 5**（不是 RAR4；头 8 字节 `52 61 72 21 1A 07 01 00`） |
> | 分卷 | **卷 3 / 锁定（locked）** |
> | **固实** | **不是固实** —— 直接解析 part3 主头：archive flags = `0x0013` = `VOLUME|VOLNUMBER|LOCK`，**`MHFL_SOLID=0x0004` 未置位**（`third_party/unrar7/headers5.hpp:25-29`）。旁证两处自洽：`MHFL_VOLNUMBER` ⇒ volnumber=2 ⇒ unrar 显示「卷 3」✓；`MHFL_LOCK` ⇒ unrar 显示「锁定」✓ |
> | 关键条目 | `PPSA16608.exfat` **19 493 027 840 B → 打包 3 831 295 727 B**（**ratio 5.09 : 1**，高度可压缩） |
> | 压缩参数 | **`RAR 5.0(v50) -m1 -md=4g`** —— **-m1「最快」档 + 4 GiB 字典** |
> | 体量 | part3 = **3.568 GiB ≈ 该条目的打包大小** ⇒ 这个 19.5 GB 条目**整段都在 part3 内**（另有两个 KB 级小文件）；parts 1+2（各 4 GB）装的是其余 ~1250 个条目 |
>
> **① UI 速度口径已核实 = 解压后字节。** `src/rar_extract.c:766` 在 `UCM_PROCESSDATA` 回调里
> `c->bytes_done += p2`（`p2` 是 unrar 交出的**解压后**长度），`bytes_total` 累加 `hdr.UnpSize`。
> ⇒ 用户看到的 **10–40 MB/s 是"吐出数据的速度"**。
>
> **② 「scan 阶段白解码一遍（2×）」这个最大嫌疑——已排除。**
> `third_party/unrar7/dll.cpp:341-342` 只在 **`!Arc.Solid`** 时把 `RAR_SKIP` 走廉价的
> `Arc.SeekToNext()` 分支；`extract.cpp:529`（`SkipSolid=Arc.Solid` ⇒ 真解码后丢弃）只在
> **固实**时成立。本档**非固实** ⇒ 我们的 scan 只读头 + 跳过字节，**不解码**。
> （这条曾是最有希望的一项：真固实的话 `scan + extract` 会把 11.6 GB 解码两遍。）
>
> **③ MT 收益已实测**（host、WinRAR 自带 **UnRAR 7.23**、`-mt<N>` 开关虽未见于 `-?` 帮助但可解析）：
>
> | 夹具 | 载荷 | `-mt1` | `-mt8` | 加速 |
> |---|---|---:|---:|---:|
> | **代表性**（`-m1 -md4g`、5.27:1 可压缩、固实、2 GB、16 文件） | 2 GB | **386 MB/s** | **946 MB/s** | **2.45×** |
> | 代表性同上但不固实（5.01:1） | 2 GB | 505 MB/s | 1222 MB/s | 2.42× |
> | ~~非代表性~~（90% 随机数据 ⇒ 压缩块小） | 240 MB | 129 MB/s | 331 MB/s | ~~2.55×~~ **作废** |
>
> ⇒ **形状错误会让结论偏乐观**：第一版夹具用 90% 随机数据，压缩块远小于
> `unpack50mt.cpp:150-152` 的 `LargeBlockSize=0x20000`（128 KiB）阈值，MT 全程生效；
> 真实归档是 5:1 可压缩数据，块更大、会触发 `LargeBlock` 退化路径，实测也确实从 2.55× 降到 2.45×。
> 结论：**在真实形状上 MT 值 ~2.4×**，可信。（单位均为**解压后** MB/s。）
>
> **④ 接线比原计划简单：1 个编译开关 + 1 个链接开关，不用改 vendored 源码、不用增删源文件。**
> （下表中间那一行「把 `threadmisc.cpp` 加进源列表」**经实测作废** —— 行内已更正。）
> | 改动 | 位置 | 为什么必需 |
> |---|---|---|
> | `-DRAR_SMP` | `Makefile:UNRAR7_CXX_FLAGS`（PS5 与 host 两处） | `os.hpp:42-45` 的 `#define RAR_SMP` 在 `#ifdef _WIN_ALL` 内；且 `unpack.cpp:7-9` 的 `#include "unpack50mt.cpp"` 就在 `#ifdef RAR_SMP` 里 ⇒ 不定义宏则整个 MT 解码器**根本不参与编译** |
> | ~~把 `threadmisc.cpp` 加进 `UNRAR7_SRCS`~~ **← 这一条是错的，不要做** | — | `GetNumberOfThreads()` 确实定义在 `threadmisc.cpp:178`，但 `threadpool.cpp:5` **已经** `#include "threadmisc.cpp"` ⇒ 它**已经**被编进 `threadpool.o`（实测回执：`nm unrar7_threadpool.o` 能查到 `T GetNumberOfThreads` / `T GetNumberOfCPU`）。**再加进源列表 = 重符号链接失败。** 同理 `blake2sp.cpp` 也不必补 —— `blake2s.cpp:27` 已经 `#include "blake2sp.cpp"`。官方 POSIX makefile 只列 `threadpool.o`、不列 `threadmisc.o`/`blake2sp.o`，正是这个原因 |
> | `-pthread`（编译+链接） | `Makefile` 的 PS5 link 行 | `threadpool.cpp` 的 `_UNIX` 路径用 pthread cond/mutex（**不用 `sem_t`**）。SDK 侧 `target/lib/libpthread.a` 在位、`target/include/pthread.h:198-237` 声明齐全 |
>
> **实测回执（宿主 MinGW，2026-09-23）**：用**与 PS5 完全相同的 50 个源文件**、只加 `-DRAR_SMP`，
> 链接**一次通过**（`bench_mt.exe` 1,011,448 B）：`nm` 里 `Unpack5MT` 出现 1 次、`ThreadPool` 12 次；
> 换成 `-DZIPSFX`（关掉 `RAR_SMP`）后 `Unpack5MT` 归 0。
> ⇒ **接线 = 1 个编译开关 + 1 个链接开关，零源码改动、零源文件增删。**
> **为什么不必改 `dll.cpp`**：`dll.cpp:6-12` 的 `DataSet` 成员顺序是 `CommandData Cmd; Archive Arc; CmdExtract Extract;`，
> 构造时 `Cmd` 先完成 → `RAROptions::Init()`（`options.cpp:22-24`）在 `RAR_SMP` 下执行
> `Threads=GetNumberOfThreads()` → 随后 `CmdExtract Extract(&Cmd)` 构造函数里
> `Unp->SetThreads(Cmd->Threads)`（`extract.cpp:25-27`；上限 `Min(Threads,8)`，`unpack.cpp:65-71`）。
> ⇒ **宏一开，RARDLL 路径自动拿到 MT**（这正是 CLI 与 DLL 共用的那条链路）。
> 内存代价：MT 下 `UnpackThreadData` × `MaxUserThreads*2`（每个 `Decoded` 预分配 0x4100 项）
> + `ReadBufMT` 4 MiB ≈ **15 MB 量级**，PS5 上可忽略。
>
> **⑤ 但是：新证据把矛头指向「写路径 / 存储」，而不是解码 —— 先别改代码。**
> - 同形状**单线程**解码在 PC 上是 **386–505 MB/s（解压后口径）**；PS5 的 Zen 2 单核即使按 1/3 算也有 **~130 MB/s**。
> - 真机只算 `.exfat` 一项就是 **19.49 GB ÷ 660 s = ≥29.5 MB/s 解压后**（且与 UI 的 10–40 吻合）。
>   若 parts 1+2 的 8 GB 打包数据解开后还有十几 GB，那么全流水线就是 **30–70 MB/s 解压后**，
>   比 CPU 能力低 **3–10×**。
> - **两次独立操作撞同一个数**：上传（写 11.6 GB）实测 30–40 MB/s；解压（写 ≥19.5 GB）≈30 MB/s。
>   ⇒ 优先怀疑 **PS5 这条写路径的上限就在 30–40 MB/s**（内置盘 / 外置盘 / 目标目录待确认）。
> - ⇒ **决策顺序**：先做 `T_copy`（零改动、纯搬运）。纯搬运也 ~10 分钟 ⇒ 收工，MT 不必做
>   （做了也会被 I/O 吃掉）；纯搬运明显快 ⇒ 再上 MT，那 2.4× 才是真金白银。
>
> 复现脚本：`.build/rabtest/ab_rar_mt.py`（第一版，形状错误，留作反例）、
> `.build/rabtest/ab_rar_mt2.py`（代表性版）。夹具留在 `D:\_wfm_rabtest{,2}\`（≈1 GB）。

> ### ⚠️ 2026-09-23 晚 更正：**「瓶颈不在解码」这个结论已撤回**
>
> 本节最初写它时只知道「18 GB / 11 分钟 / 1252 条目」，**不知道归档格式**。随后的补充
> （**格式是 RAR**；包在 PC 上、经插件上传进 PS5；上传速度 30–40 MB/s）把两个前提都改了：
>
> **① 参照物错了 —— 这是方法错误，不是估计偏差。**
> 原文拿「PS5 上解 **RAR** 的 28 MiB/s」去比「PC 上解 **7z** 的 427 MiB/s」。**不同格式、
> 不同解码器、不同机器**：RAR 我们直接用 rarlab 的 UnRAR 库，7z 走自建 chain，两者毫无
> 可比性。⇒ 原文「推论二（量级差 3–10×）」**不成立**，不能作为解码无罪的证据。
> 「推论一（摆动）」此前已自行降级为弱证据（250 ms 采样噪声）。**两条都没了。**
>
> **② 查代码查出一个具体缺口：RAR 解码在我们这里是单线程的。**
> `third_party/unrar7/os.hpp:43-45` 的 `#define RAR_SMP` 落在 `#ifdef _WIN_ALL` 分支**内**，
> 所以 POSIX（PS5）构建**不定义** `RAR_SMP` —— 我们 Makefile 里 0 次出现；而官方 POSIX
> makefile 第 11 行是 `DEFINES=... -DRAR_SMP`，**我们漏了这个开关**。后果：
> - `unpack.cpp:185-198` 的 MT 分支整段不参与编译 ⇒ 永远走单线程 `Unpack5()`
> - `unpack50mt.cpp`（`Unpack::Unpack5MT`）**不在我们的源列表里**；这是 rarlab 专门调过的
>   多线程 RAR5 解压器（文件头注释：「0x400000 和 2 对 i9-12900K 最优」）
> - `Unpack::SetThreads()` / `ThreadPool` 随之消失
> - ⚠️ **我们在 ELF 上做的符号核查是无效的**：该 ELF 只有 `.dynsym`（513 项）、**没有
>   `.symtab`**，任何内部符号都查不到（零命中是假象）。上面的结论来自 Makefile 与 `os.hpp`。
>
> **③ 新的首要假设：28 MiB/s ≈ 单线程 RAR5 解码的典型量级。**
> RAR5 `-m5` 单线程在现代桌面 CPU 上约 40–80 MB/s 输出，PS5 的 Zen 2 单核更低。
> 另一个角度：`18 GB ÷ 660 s = 27.9 MB/s` 是**解码输入**速率，而上传实测证明**写入端**
> 至少能到 30–40 MB/s、**读取通常快于写入** ⇒ 「纯存储上限」解释不了这个数。
>
> **④ 附带作废一条**：「读粒度只有复制路径 1/32–1/64」是 **7z 的数字**（`SZ_IN_CHUNK`
> 256 KiB），对 RAR 不适用 —— RAR 走 `od.ArcName` 按路径打开（`src/rar_extract.c:1059`），
> 归档 I/O 由 UnRAR 自己的 `File` 类完成，我们的回调只收到**解压后的数据**，
> **插不进 read-ahead**。⇒ 待办 #48 对 RAR 无效。
>
> **⑤ 归档真实参数（口径已闭合）** —— 两个来源合起来读得通：WinRAR 信息页（待解压版本 5.0、
> 加密「缺少」、无恢复记录、压缩文件锁定「存在」、**字典 4 GB**、压缩率 19%、
> **总大小 19,493,028,232 B = 18.15 GiB**、打包大小 3,831,296,089 B、**总文件 3**）＋ 文件列表
> （三卷 `.part1/2/3.rar` = 4 GB + 4 GB + 3.6 GB ≈ 11.6 GB）。
> **两者不矛盾**：信息页是把 **part3 当独立归档**打开的 —— part3 = 3.568 GiB，里面就是那 3 个条目
> （一个 19.5 GB 的 `PPSA16608.exfat` ＋ 两个 KB 级小文件），上面的「第二次更正」块已用
> `UnRAR lt` 直接读头证实；而三卷合计 ≈11.6 GB 才是整个包（另外还有 ~1250 个条目）。
> ⚠️ 我先前按"两图互相矛盾、需用户确认"写的那一版**作废**：不是两个归档，是"单卷视图 vs 整包视图"。
> 速度口径不受影响：**18.15 GiB ÷ 660 s = 29.5 MB/s（解压后字节）**；就那个大条目而言
> 读侧只需 3.83 GB ÷ 660 s ≈ **5.8 MB/s** ⇒ **读侧不是瓶颈，29.5 MB/s 是解码+写盘的真实速率**。
>
> ## ⚑ 终局：RAR5 多线程**不做**（2026-09-23 18:15，用户决定）
>
> **#51 结案：保持单线程现状，生产代码不动、不刷机。**
>
> **为什么不做的依据是"判不了"，不是"没收益"** —— 收益本身已实测（块 ⑧：我们的引擎
> 1.40–1.74×；rarlab CLI 在用户那种形状上 2.45×）。卡住的是**它能不能兑现**：
> - `T_copy` 没做 ⇒ 无法区分「解码慢」与「写路径上限 ≈30 MB/s」。
> - 而 MT **只并行解码**：worker 只跑 `UnpackDecodeThread`（`unpack50mt.cpp:190`），
>   **写盘恒为主线程串行**（`UnpWriteBuf()` 仅由主线程调用 —— `unpack50mt.cpp:283/475/587`）。
>   ⇒ 若墙在写路径，MT 的收益直接退化成 **1.0×**。
> - 成本收益：判定要人上手测一次复制；上线要刷机 + 重跑 11 分钟。而收益可能为 0
>   ⇒ **不做**。维持 11 分钟，把不确定性留在文档里，比赌一次更划算。
>
> **下面是已完成的技术取证，全部保留** —— 将来若要重开，它就是现成答案。
> ⚠️ **重开的第一个动作是 `T_copy`，不是改构建**（判读见 `docs/REAL-CONSOLE-PROFILE.md` 第 1 步）。

> **⑥ 开 RAR5 多线程只需一个编译开关**（把 #51 的工作量从"未知"降到"一行"）：
> - `third_party/unrar7/unpack.cpp:7-9` **已经** `#include "unpack50mt.cpp"`；
>   `threadpool.cpp:5` **已经** `#include "threadmisc.cpp"`。⇒ 官方 POSIX makefile 不列这两个
>   `.o` 是**正常的**，**不需要新增源文件**（此前"官方 makefile 自相矛盾"的疑点已消除）。
> - `raros.hpp:23-25`：非 Windows 一律 `#define _UNIX` ⇒ PS5 构建自动走 Unix 分支；
>   `threadpool.cpp` 的 `_UNIX` 路径用 pthread cond/mutex，**不用 `sem_t`**。
> - 线程数在 DLL 模式下**自动接上**：`dll.cpp` 每个归档持有一个 `CommandData`，
>   构造函数 `cmddata.cpp:6-9 → Init() → RAROptions::Init()` →
>   `options.cpp:22-24 Threads=GetNumberOfThreads()`；`extract.cpp:25-27` 再
>   `Unp->SetThreads(Cmd->Threads)` → `unpack.cpp:65-71 MaxUserThreads=Min(Threads,8)`。
>   ⇒ **不需要 CLI 开关、不需要 vendor 补丁**。
> - ⇒ 改动 = 给 unrar 对象加 `-DRAR_SMP` + 链接 pthread。**PS5 侧唯一未验证点是
>   `sysconf(_SC_NPROCESSORS_ONLN)` 是否返回真核数**（`threadmisc.cpp:111-124`）：
>   SDK 的 `unistd.h:291` 定义了 `_SC_NPROCESSORS_ONLN 58`、`pthread_*` 在
>   `target/include/pthread.h:198-237` 齐全；但若 `sysconf` 返回 1，**MT 会静默失效**。
>   ⚠️ 注意 `threadmisc.cpp:116-124` 在 `_UNIX` 且未定义 `_SC_NPROCESSORS_ONLN` 时**没有
>   return 语句**（UB）—— 真机上要能看到核数才算数。
>
> **⑦ MT 对固实归档同样生效，唯一例外是分片窗口**：`unpack.cpp:185-198` 在
> `MaxUserThreads>1` 时调 `Unpack5MT(Solid)` —— 形参本身就带 `Solid`。会把它挡在外面的只有
> `Fragmented`（`unpack.cpp:193`）：`unpack.cpp:130-145` 只在 4 GiB 窗口的**连续分配失败**
> 且 `WinSize>=16 MiB` 且 64 位时才置位，而且分片窗口路径**本身更慢**。
> 64 位下单次 malloc 失败通常是"量不够"而非"地址空间碎"，此时 `FragWindow.Init(同一大小)`
> 也会失败 ⇒ **分片窗口属于罕见回退，概率低**。但它是可判读的：
> **真机 A/B 若"开了 MT 却一点没变"，第一嫌疑就是它或 `sysconf`。**
>
> **⑧ 宿主 A/B 实测：MT 值 1.4–1.75×**（同一台机器、同一份二进制，只差一个 `-DZIPSFX`）。
> 方法：MinGW 下 `_WIN32 ⇒ `_WIN_ALL` ⇒ `os.hpp:43` 自动定义 `RAR_SMP`，所以**我们过去所有
> 宿主基准跑的都是多线程路径**；反过来单线程基线只能靠 `-DZIPSFX`（该宏在整棵源码树里
> **只出现一次**，就是 `os.hpp:43`，干净可用）。回执：`nm` 查 `unpack.o`，base 的
> `Unpack5MT` 符号数 = 0、mt = 1。样本：341 MiB 现实混合数据（127 MiB 真实二进制
> + 158 MiB 短匹配文本 + 48 MiB 随机），RAR5 固实 `-m3`，压缩后 125–153 MB（比率 37–45%）：
>
> | 样本 | base（单线程） | mt | 加速 |
> |---|---:|---:|---:|
> | 固实 `-md1m` | 69.7 MiB/s | 97.3 MiB/s | **1.40×** |
> | 固实 `-md256m` | 63.4 MiB/s | 110.6 MiB/s | **1.74×** |
> | 分卷 `-md256m`（96+23 MiB） | 64.2 MiB/s | 100.8 MiB/s | **1.57×** |
>
> 两点附带信息：①**字典越大 MT 越划算** —— 单线程随字典从 1 MiB 涨到 256 MiB 掉到
> 63–70 MiB/s（内存局部性），而 MT 稳定在 97–110；用户的包字典 4 GB，比这里最大的样本
> 还大 16×，**方向上有理由期望 MT 收益不小于 1.6×**。②**跨机器外推不算结论**：
> 宿主单线程 63–70 MiB/s vs PS5 的 28.1 MiB/s 是不同 CPU，只作量级参考。
> ⇒ 若 PS5 上同样拿到 1.5–1.75×，11 分钟 → **约 6.3–7.3 分钟**。
>
> **⑧b 与上面「第二次更正」块的 2.45× 不矛盾 —— 差在样本形状，不在实现。**
> 那块用 rarlab 自带 **UnRAR 7.23 CLI** 的 `-mt1` vs `-mt8`，夹具是 `-m1 -md4g`、**5.27:1**
> 可压缩的 2 GB ／16 文件；我这边是 `-m3`、只有 **2.4:1** 的短匹配数据。方向一致：
> **数据越可压缩（匹配越长）MT 越划算** —— 长匹配让一条符号吐出更多字节，串行 apply 被摊薄、
> 并行解码占比上升，同时更容易越过 `unpack50mt.cpp:150-152` 的 `LargeBlockSize=0x20000` 退化阈值。
> ⇒ **对用户这个包应以 ~2.4× 为预期**（它是 `-m1 -md4g`、5.09:1，正落在那个夹具的形状上），
> 我测到的 1.4–1.75× 作为**更难数据下的下界**。绝对吞吐那 10× 的差距（386 MB/s vs 63–70 MiB/s）
> **纯粹是夹具可压缩性差异，不能拿来比较两个实现**（这正是"基准代表性"那类错误）。
> 综合估计：11 分钟 → **约 4.6 分钟**；悲观情形（1.5×）→ 6.3–7.3 分钟。
>
> **⑨ 样本代表性教训（第一版 A/B 是废的）**：初版样本是「同一个 60 KiB 区块重复 2400 次」，
> 压缩到 0.2%、解码 **602 MiB/s** —— 长匹配让范围解码器的每条符号吐出上千字节，
> 这个形状**真实归档里不存在**，而且它恰恰是 MT 最不擅长的形状（串行 apply 占主导）。
> 后改用"真实二进制 + 短匹配文本 + 随机"混合，比率 37–45%，吞吐落到 63–110 MiB/s 的正常带。
> 另：初版把 `-v96m` 的目标名写成 `vol.part1.rar`，rar 会翻倍成 `vol.part1.part1.rar`
> （`tests/make-rar-fixtures.bat` 里已记过这个坑，我复现了一遍）。
>
> **⑩ 顺带查出两个真实缺陷/隐患**：
> 1. **字典 > 4 GiB 的归档会直接失败** —— **已修，但修的是"说清楚"，不是"放行"**。
>    机制：`extract.cpp:1748-1767 CheckWinLimit()` 在 `WinSize > Cmd->WinSizeLimit` 时调
>    `uiDictLimit()`；DLL/silent 构建里 `uisilent.cpp:65-73` 只在
>    `Cmd->Callback(UCM_LARGEDICT, …) == 1` 时才放行，否则 `DllError=ERAR_LARGE_DICT` 并跳过
>    该文件。我们的回调原先**只处理 `UCM_PROCESSDATA`**，其余一律返回 0。默认
>    `Cmd->WinSize`/`WinSizeLimit` = `0x2000000` / `0x100000000`（`options.cpp:12-13`）
>    ⇒ 分界线正好是 4 GiB（比较是 `<=`）。
>
>    **⚠️ 三条订正（2026-09-23 18:40，把先前"1 行可修"的判断推翻）：**
>    - **RAR5 根本到不了这里。** `arcread.cpp:871` 把 RAR5 字典读成
>      `0x20000 << ((CompInfo>>10) & 0x0f)` —— **只有 4 bit** ⇒ 格式自身上限 = `0x20000<<15`
>      = **正好 4 GiB**，与我们的 limit 相等。⇒ 任何 `-ma5`（含用户这个包）**永远不触发**。
>      只有 **RAR7 头**（`UnpVer==1`，5 bit，上限 `UNPACK_MAX_DICT` = 64 GiB）才可能超。
>    - **而 RAR7 造不出来。** 实测 `Rar.exe 7.23`：`-ma4` / `-ma6` / `-ma7` **全部 exit 7**
>      （命令行错误），只有 `-ma5` 可用。⇒ 本机连验证样本都得手工合成。
>    - **"放行"是陷阱不是修复。** 放行后 unrar 会去 `new` 一个**完整的字典窗口**；rarlab 自己的
>      CLI 对这个样本的答复是：「8 GB 字典超过 4 GB 限制，而且需要大于 8 GB 内存来解压缩。
>      使用 `-md8g` 或 `-mdx8g` 参数来解压缩。」PS5 只有 16 GB **共享**内存 ⇒ >4 GiB 字典在
>      该设备上本就解不动；**中途被 OOM 杀掉（整个 payload/UI 一起没）比干净失败更糟**。
>      ⇒ 决定：**保持拒绝**，与上游 CLI 默认一致。
>
>    **实际改动（在生产代码里，2026-09-23）：** 拒绝时不再把锅甩给条目。原先
>    `ERAR_LARGE_DICT → ZIPX_ERR_LIMIT_FILE` → i18n `extract_entry_too_large`
>    ⇒ 用户看到的是「**压缩包内单个文件过大: hello.txt**」，而那个条目只有 7 KB —— 完全错。
>    现在新增 `ZIPX_ERR_LIMIT_DICT`（`src/zip_extract.h`）+ `extract_dict_too_large`
>    （中/英），并在 `rar_data_cb` 里从 `UCM_LARGEDICT` 的 `p1/p2` 取回真实数字，
>    报成「需要 8192 MiB（上限 4096 MiB）」。回归用例 `tests/test_rar_extract.c:test_dict_limit`
>    + 固定样本 `tests/fixtures/dict-8g.rar`（合成器 **`tests/make_fixtures.py:bigdict()`**，
>    纯 Python 手写最小合法 RAR5 归档、不依赖任何压缩器；说明见该函数 docstring）。
>    ⚠️ **样本必须由 `make_fixtures.py` 生成** —— `fresh()` 会 `shutil.rmtree()` 整个
>    `tests/fixtures/`，提交进去的二进制会被抹掉，所以不能单独放一个生成脚本。
> 2. **归档里的目录条目 +「覆盖」策略 = 第二次解压到同一目录必失败**。
>    `src/rar_extract.c:884-892`：目标已存在且是目录、而归档条目也是目录时，
>    只有 `ZIPX_CONFLICT_MERGE` 能过；`OVERWRITE` 报 `ZIPX_ERR_CONFLICT`
>    （状态串是 "target already exists"，`detail` 才是 "directory already exists"）。
>    `zip_extract.c:1123` / `sevenz_extract.c:1526` 同构。⇒ 用户对含目录的包用「覆盖」
>    解两次，第二次会失败。**这条是我在搭 A/B 时被挡了才知道的**，需要确认是否设计意图。
>
> 下文数据与推论**保留原文**（它们是当时判断的依据），但**结论以本块为准**。

### 数据
用户在真机上解一个 **18 GB 的包**，UI 上报的解压速度在 **10–40 MB/s** 之间摆动；
随后补上两个关键数字：**总耗时 11 分钟（660 s）**、**条目数 1252**（平均 14.7 MB/条目）；
再确认 **18 GB 是压缩包自身的大小**（解压后多大未知）。

⇒ **平均吞吐 ≥ 28 MiB/s**（18 GB = 18 432 MiB ÷ 660 s；解压后更大则更高，故为下限）。
**这个数字才是基线**，UI 上那个 10–40 的区间只是瞬时值。

「18 GB = 压缩包大小」顺带给出一个**与解码无关的硬上界**：源盘必须在 660 s 内交出
18 GB 归档数据 ⇒ 整条流水线的平均吞吐上界就 ≈ 28 MiB/s。**无论解码多快，源盘只有这个交付速度。**
这也是为什么第 1 步的 `T_copy`（同一个 18 GB 文件的纯搬运）能与 660 s 直接比大小。

顺带排除一项：**per-entry（小文件）开销不是主因** —— 1252 个文件、平均 14.7 MB，不是
"几万个小文件"那种形态，建文件 + rename 分摊到 0.53 s/文件里微乎其微。

口径先确认（不是猜）：进度条的"字节"是**解压后的字节**——
`src/zip_extract.c:651-678` 把 `bytes_total` 累加自 `info->uncompressed_size`；
`:875` 的 `mz_zip_entry_read()` 返回解压字节，`:900/910` 用它累加 `bytes_done`。
所以 10–40 MB/s 是**吐出数据的速度**，正是用户关心的那个口径。

### 推论一：摆动说明"负载不恒定"，但它是弱证据
solid 块（同一字典、同一条码流）的解码速率**几乎是恒定的**。要出现 4× 的摆动，
更像是 I/O 侧在变：源盘读取、目标盘写入、逐条目同步开销、或存储设备自身在忙。

但这条**不能单独定案** —— 那个 MB/s 是 250 ms 窗口 + 1 MiB 上报阈值的**瞬时值**
（`src/zip_extract.c:123`、`src/task.c:202-206`），写缓冲突发本身就能在窗口里造成大幅跳动。
它能说的只有"负载不恒定"，不构成"解码无罪"的证明。有解释力的是推论二（量级）和推论三（粒度）。

### 推论二：量级上差 3–10×，解码没有解释力
| | 吞吐 |
|---|---:|
| PC / WSL，329 MiB 混合数据，8 线程（本仓实测） | **427 MiB/s** |
| PS5 单线程解码的乐观上界（按核数×频率外推，**未实测**） | ~100 MB/s |
| **真机实测（18 GB 包，端到端）** | **10–40 MB/s** |

18 GB @ 10–40 MB/s = **7.7 ~ 31 分钟**；同样数据在 PC 上纯解码约 43 s。
⇒ 解码最多占 10–25%，**很可能远低于此**。

### 推论三：三个引擎的读请求粒度都只有复制路径的 1/32–1/64
粒度审计（已核对源码）：

| 路径 | 源侧读粒度 | 目标侧写粒度 |
|---|---|---|
| 复制（`copy_file_pipeline`，≥256 MiB） | **8 MiB** × 3 slot，4096 对齐，独立读线程 | 8 MiB |
| 7z | chain `SZ_IN_CHUNK = 256 KiB`（`src/sevenz_chain.c:87`）；MT 路径 `inBufSize_MT = 1 MiB` | `SZ_OUT_CHUNK = 64 KiB`（`:94`） |
| ZIP | `ZIPX_IO_BUFFER = 128 KiB`（`src/zip_extract.c:32`） | 128 KiB |
| RAR | UnRAR `File::CopyBufferSize() = 4 MiB`（`third_party/unrar7/file.hpp:148-153`） | 4 MiB |

都不是 4 KB 那种「小读」灾难，但**都比复制小 32–64 倍**。在延迟主导的设备上，
吞吐 ≈ 单次请求大小 ÷ 每次请求的等效延迟：

```
256 KiB / 10 ms =  25 MB/s    ← 正好落在实测 10–40 MB/s 的中间
  8 MiB / 10 ms = 800 MB/s    ← 复制路径不会撞这个上限
```

若源与目标在**同一块盘**（例如外置 USB HDD 上解压到同一块盘），读流与写流并存，
磁头来回跑、read-ahead 被写回刷打断 → 每次请求退化成一次寻道，上面这个算术即成立。
**这是目前唯一可疑的代码级病因**，而它的改动面极小：所有 7z 的读都只经过
`src/sevenz_volstream.c` 的 `vol_read()` 一个函数（ZIP/RAR 同理在 `src/zipx_volstream.c`）。

### ~~因此：剩余解码优化项全部搁置~~（**已撤回，见文首更正块**）
原文在这一段把 CRC 硬件化、MT 扩到 BCJ2 / 多 folder、ZIP inflate 换 libdeflate、AES-NI 全部判为
「在 10–40 MB/s 的现实面前没有意义」。**建立在「解码不是瓶颈」之上，而那个前提已撤回。**

更正后的分层判断（按真实工作负载 **RAR** 重排）：

| 项 | 更正后的判断 |
|---|---|
| **UnRAR RAR5 多线程解压（`RAR_SMP`）** | **❌ 不做（2026-09-23 用户决定，见文首「终局」块）**。收益已实测（rarlab CLI `-mt1` vs `-mt8` **2.45×**，见「第二次更正」块 ③；我们引擎在更难数据上 1.40–1.74×，块 ⑧b），接线也只是 **1 个编译开关 + 1 个链接开关、零源码改动**（块 ④）—— **但 `T_copy` 没做，无法排除「写路径上限 30–40 MB/s」**；MT 只并行解码、写盘恒为主线程串行（`unpack50mt.cpp:190` / `:283,475,587`）⇒ 收益可能是 1.0×。刷机 + 重跑 11 分钟的成本压在不确定收益上 ⇒ 搁置 |
| CRC 硬件化（4–5%） | 仅对 7z / ZIP 有意义；**对 RAR 完全无关**（CRC 由 UnRAR 自己算） |
| MT 扩到 BCJ2 / 多 folder | 仅 7z；RAR 工作负载下无意义 |
| ZIP inflate 换 libdeflate / AES-NI | 同上，与 RAR 无关 |
| 读粒度 / read-ahead（#48） | **对 RAR 无效**（UnRAR 按路径自读，回调只收解压后数据）；只对 7z/ZIP 有意义 |

「先 profile 再排序」这个结论**仍然成立**，但目标从「查清为什么只有 10–40 MB/s」变成
**「先把 RAR 单线程这条确认掉 / 排除掉」** —— 见 `docs/REAL-CONSOLE-PROFILE.md`。

### 尚未定案的部分
- ~~**归档格式未知**~~ → **已确认（2026-09-23）：格式是 RAR**，包原本在 PC 上、经插件上传进
  PS5（上传速度 30–40 MB/s）。⇒ 解码用的是 rarlab 的 UnRAR 库本身，**我们自己的代码在 RAR
  解码路径上只剩一层很薄的 facade**；「我们的解码器慢」这个方向基本不成立，
  但「**我们的构建没打开 UnRAR 的多线程**」成立（见文首更正块 ②）。
  仍未确认：**RAR4 还是 RAR5**（`Unpack5MT` 只对 RAR5/7.0 生效）、是否固实、是否加密、
  **解压后多大**（决定输出吞吐）。
- ~~**18 GB 是压缩后还是解压后**~~ → **已确认（2026-09-23）：18 GB 是压缩包自身的大小**。
  于是有了一个**不需要知道解压后大小的硬上界**：源盘要在 660 s 内交出 18 GB 归档数据
  ⇒ 整条流水线平均吞吐上界 ≈ **28 MiB/s**；`T_copy` 与 660 s 可直接比大小。
- **存储未知**：包在哪个设备上（内置 SSD / 外置 USB / 同盘还是异盘）、输出写到哪个设备。
- ~~**下一步（决定性、零改动）**~~ → **未执行（2026-09-23 用户决定搁置，见文首「终局」块）**：
  用插件自己的**复制**功能把那个包搬到解压输出所在的那块盘上，记耗时。复制的
  `copy_file_pipeline` 是 8 MiB × 3 slot 的双缓冲搬运，⇒ 它就是"同一台机器、同一对设备、
  只把解压换掉"的对照。**将来重开时这是第一个动作**，判读见 `docs/REAL-CONSOLE-PROFILE.md` 第 1 步。

> 一句话：**"我们比 7-Zip 慢 1.59×" 这个议题在真机上不成立** —— 两边都被同一个 I/O
> 上限压着，谁先撞墙取决于存储，而不是解码器。真机目标从"提速解码"改成"查清 28 MiB/s"。
> ~~目前指向一个具体、可改的位置：**读请求粒度只有复制路径的 1/32–1/64**（推论三），
> 而全部 7z 读都经过 `vol_read()` 一个函数。**先用复制做基线验证它，再决定改不改。**~~
> → **推论三对 RAR 不适用**（那是 7z 的 256 KiB 数字；UnRAR 按路径自读，见文首更正块 ④）。
> 更正后：真机工作负载是 RAR，而**我们的 RAR 解码是单线程的**（`RAR_SMP` 未定义 ⇒
> `unpack50mt.cpp` 不参与编译）—— 这是一项有据可查、且直接针对真实负载的改进点，
> 在真实形状上**已实测 2.4×**。
> ~~**但优先级已被文首二次更正块 ⑤ 改写**：先做零改动的 `T_copy`，确认墙在解码还是在写路径。~~
> → **闭环（2026-09-23 18:15）：`T_copy` 没做，用户决定不做了** ⇒ 见文首「终局」块。
> 单线程现状保留，此项转入"已评估、搁置（重开先测 `T_copy`）"。

---

## 七、复现

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
