#!/usr/bin/env bash
# ===========================================================================
# ps5-web-file-manager -- 一键在 WSL 里生成 PS5 ELF
#
# 这个脚本在 WSL (Ubuntu-22.04) 里执行，做四件事：
#   1. 把 Windows 仓库的源码 rsync 到 WSL 项目目录（增量，跳过构建缓存）
#   2. make all  （PS5_PAYLOAD_SDK = /opt/ps5-payload-sdk）
#   3. 验证产物：size / sha256 / e_machine
#   4. 把 ELF 拷回 Windows 项目根
#
# 从 Windows 的 Git Bash / MinGW64 bash 里这样跑：
#   wsl.exe -d Ubuntu-22.04 -- bash < \
#     "/c/Users/songl/Desktop/Web File Manager/ps5-web-file-manager/.build/build-elf-wsl.sh"
#
# 注意：必须用 stdin 重定向 `bash < script`，不要 `bash -c '...'` ——
#   路径含空格时 -c 的参数会被 wsl.exe 拆断。
# ===========================================================================

set -uo pipefail

# ---------------------------------------------------------------- 配置 -----
SRC_WIN='/mnt/c/Users/songl/Desktop/Web File Manager/ps5-web-file-manager'
PROJ='/home/song/ps5-web-file-manager'
SDK='/opt/ps5-payload-sdk'

log()  { printf '\033[1;36m%s\033[0m\n' "$*"; }
warn() { printf '\033[1;33m[WARN] %s\033[0m\n' "$*"; }
fail() { printf '\033[1;31m[FAIL] %s\033[0m\n' "$*"; exit 1; }

# ------------------------------------------------------------ jwasm --------
# The LZMA decoder has an asm implementation that is 26% faster than the C one
# (see docs/EXTRACTION-PERF.md). It is MASM syntax, so a MASM-compatible
# assembler is needed. Makefile only enables the optimisation when jwasm is on
# PATH, so a failure here downgrades rather than breaks the build.
JWASM_HOME="$HOME/.cache/wfm-jwasm"
JWASM_BIN="$JWASM_HOME/jwasm"

ensure_jwasm() {
  if command -v jwasm >/dev/null 2>&1; then
    echo "  jwasm: $(command -v jwasm)"
    return 0
  fi
  if [ -x "$JWASM_BIN" ]; then
    export PATH="$JWASM_HOME:$PATH"
    echo "  jwasm: $JWASM_BIN (缓存)"
    return 0
  fi

  echo "  jwasm 不在，正在从源码编译（首次约 30 秒）..."
  mkdir -p "$JWASM_HOME" || return 1
  if [ ! -d "$JWASM_HOME/src" ]; then
    git clone --depth 1 https://github.com/Baron-von-Riedesel/JWasm.git \
      "$JWASM_HOME/src" >/dev/null 2>&1 || return 1
  fi
  make -C "$JWASM_HOME/src" -f GccUnix.mak -j4 >/dev/null 2>&1 || return 1
  cp -f "$JWASM_HOME/src/build/GccUnixR/jwasm" "$JWASM_BIN" || return 1
  chmod +x "$JWASM_BIN" || return 1
  export PATH="$JWASM_HOME:$PATH"
  echo "  jwasm: $JWASM_BIN (已编译)"
}

# ------------------------------------------------------- 1/5 环境检查 -----
log "[1/5] 环境检查"

[ -d "$SRC_WIN" ] || fail "Windows 源码目录不可见: $SRC_WIN （/mnt/c 挂载了吗?）"
[ -x "$SDK/bin/prospero-clang" ] || fail "SDK 缺失: $SDK/bin/prospero-clang"

export PS5_PAYLOAD_SDK="$SDK"
# shellcheck disable=SC1091
source "$SDK/toolchain/prospero.sh" 2>/dev/null || true
export CC="$SDK/bin/prospero-clang"
export CXX="$SDK/bin/prospero-clang++"
export PKG_CONFIG="$SDK/bin/prospero-pkg-config"

"$CC" --version | head -1
"$PKG_CONFIG" --modversion libmicrohttpd 2>/dev/null \
  || fail "libmicrohttpd 未装到 sysroot —— 先跑一次完整的 build-elf.sh v4"

ensure_jwasm || warn "jwasm 不可用 —— 将退回纯 C 解码器（约慢 26%）"
echo

# ------------------------------------------------------- 2/5 同步源码 -----
log "[2/5] 同步源码 Windows -> WSL (rsync 增量)"
mkdir -p "$PROJ"

rsync -a --delete \
  --exclude='/ps5-obj' --exclude='/linux-obj' \
  --exclude='/web-file-mgr-*.elf' --exclude='/web-file-mgr-linux*' \
  --exclude='/gen' --exclude='/.build' --exclude='/tests' \
  --exclude='/docs' --exclude='/HANDOVER.md' \
  --exclude='/README.md' --exclude='/CHANGELOG.md' \
  --exclude='/erssongl*' \
  --include='/src' --include='/assets' \
  --include='/third_party' --include='/Makefile' \
  --include='/gen-asset-module.py' --include='/.gitignore' \
  --exclude='/*' \
  "$SRC_WIN/" "$PROJ/" || fail "rsync 失败"

# rsync 的 --include 只放行目录本身，这几个顶层文件再单独 cp 一次
for f in Makefile gen-asset-module.py .gitignore; do
  [ -f "$SRC_WIN/$f" ] && cp -f "$SRC_WIN/$f" "$PROJ/$f"
done

# 冒烟：今天的关键文件都在不在
for f in src/sevenz_extract.c src/zipx_common.c src/sevenz_volstream.c \
         src/sevenz_chain.c Makefile gen-asset-module.py; do
  [ -f "$PROJ/$f" ] || fail "同步后缺失: $PROJ/$f"
done
[ -d "$PROJ/third_party/7z" ] || fail "同步后缺失: $PROJ/third_party/7z"

# 输出文件名由 Makefile 的 VERSION_TAG 决定（web-file-mgr-<ver>.elf）。
# 从 Makefile 里读，别在脚本里硬编 —— 否则改了版本号脚本还在找旧名字。
VERSION=$(sed -n 's/^VERSION_TAG *[?:]*= *//p' "$PROJ/Makefile" | head -1)
[ -n "$VERSION" ] || fail "读不到 VERSION_TAG: $PROJ/Makefile"
BIN_NAME="web-file-mgr-${VERSION}.elf"
ELF="$PROJ/$BIN_NAME"

echo "  src/ + assets/ + third_party/ + Makefile  OK"
echo "  版本: $VERSION  ->  输出: $BIN_NAME"
echo

# ---------------------------------------------------------- 3/5 编译 ------
log "[3/5] make all"
cd "$PROJ" || fail "cd $PROJ"

make all 2>&1 | tail -120
# make 的退出码被管道吃了，用 PIPESTATUS 取回来
if [ "${PIPESTATUS[0]}" -ne 0 ]; then
  fail "make all 失败（详见上方输出）"
fi
echo

# ---------------------------------------------------------- 4/5 验证 ------
log "[4/5] 验证产物"
[ -f "$ELF" ] || fail "ELF 未生成: $ELF"

SIZE=$(stat -c%s "$ELF")
HASH=$(sha256sum "$ELF" | cut -d' ' -f1)
# ELF header: offset 18 起 2 字节 = e_machine。
# `od -tx2` 按 2 字节小端解释成一个 short 后打印其**值**，所以文件里的
# 字节序 "3e 00" 会输出成 "003e"（不是 "3e00"）。别拿字节序去比对。
EM=$(od -An -tx2 -j 18 -N 2 "$ELF" | tr -d ' \n')
EM_NUM=$((16#$EM))

ls -lh "$ELF"
echo "  size:    $SIZE bytes (~$((SIZE / 1024)) KiB)"
echo "  sha256:  $HASH"
echo "  e_machine = 0x$EM ($EM_NUM)"

case "$EM_NUM" in
  62)  echo "  -> x86-64 / PS5  [OK]" ;;
  183) fail "e_machine=$EM_NUM (0x$EM) 是 aarch64！PS5 是 x86-64，target 三元组错了" ;;
  *)   fail "e_machine=$EM_NUM (0x$EM) 非预期（期望 62 = 0x003e = x86-64）" ;;
esac
echo

# ------------------------------------------------- 5/5 拷回 Windows -------
log "[5/5] 拷回 Windows"
cp -f "$ELF" "$SRC_WIN/$BIN_NAME" || fail "拷回 Windows 失败"
ls -lh "$SRC_WIN/$BIN_NAME"
echo
printf '\033[1;32m[DONE]\033[0m %s\n' "$SRC_WIN/$BIN_NAME"
echo "  $SIZE bytes / sha256 $HASH / e_machine 0x$EM"

# 顺便报告 Windows 侧现在有哪些版本化 ELF，方便挑一个拷进 U 盘
echo
echo "  Windows 项目根现有的 ELF："
ls -1 "$SRC_WIN"/web-file-mgr-*.elf 2>/dev/null | sed 's#.*/##' | sed 's/^/    /' || true
