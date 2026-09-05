#!/usr/bin/env bash
# PS5 Web File Manager 一键构建 (v4)
# 修复:
#   (1) staging 模式装 libmicrohttpd -> make install 到 /tmp, 再 sudo cp 到 SDK
#   (2) 显式 sudo -v 刷密码缓存 (v3 的 chown 静默失败了)
#   (3) 增量: 保留 zlib/minizip-ng OBJ 缓存 (不每次 make clean)
#   (4) libmicrohttpd tarball 缓存 ~/.cache/, 失败可重试不重下
#   (5) 同步源用 rsync 增量
#   (6) 失败时把 log 同步到 Windows .build/build-elf.log
#
# 跑法: bash /home/song/build-elf.sh

set -uo pipefail

export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk
SRC_WIN="/mnt/c/Users/songl/Desktop/Web File Manager/ps5-web-file-manager"
PROJ="/home/song/ps5-web-file-manager"
LOG="/home/song/build-elf.log"
STAGE="/tmp/ps5-homebrew-ext4"
CACHE="${HOME}/.cache/libmicrohttpd-1.0.1.tar.gz"

mkdir -p "$(dirname "$LOG")" "$STAGE" "$(dirname "$CACHE")"
: >"$LOG"
exec > >(tee -a "$LOG") 2>&1

echo "=========================================="
echo "PS5 Web File Manager -- ELF 构建 v4"
echo "时间: $(date)"
echo "PS5_PAYLOAD_SDK=$PS5_PAYLOAD_SDK"
echo "user: $(whoami) uid=$(id -u)"
echo "=========================================="

# 0. sudo 密码缓存 (脚本会跑 sudo 几次, 在开头集中要求密码)
echo "[0/7] 预热 sudo (会提示输 1 次密码)..."
sudo -v 2>&1 || { echo "FAIL: sudo 不可用"; exit 99; }

# 1. SDK 验证
if [ ! -x "$PS5_PAYLOAD_SDK/bin/prospero-clang" ]; then
  if [ -f /home/song/ps5-payload-sdk.zip ]; then
    echo "[1/7] 展开 SDK zip 到 $PS5_PAYLOAD_SDK ..."
    sudo mkdir -p "$PS5_PAYLOAD_SDK"
    sudo unzip -q -o /home/song/ps5-payload-sdk.zip -d /tmp/sdk-stage
    sudo cp -r /tmp/sdk-stage/. "$PS5_PAYLOAD_SDK/"
    sudo chmod -R a+rx "$PS5_PAYLOAD_SDK"
  else
    echo "FAIL: 未发现 /home/song/ps5-payload-sdk.zip 也无 $PS5_PAYLOAD_SDK/bin/prospero-clang"
    exit 1
  fi
fi
echo "[1/7] SDK OK: $PS5_PAYLOAD_SDK"

# 2. LLVM 工具链
if ! command -v llvm-config-18 >/dev/null 2>&1; then
  echo "[2/7] 装 LLVM 18 (apt.llvm.org) ..."
  if [ ! -x /tmp/llvm.sh ]; then
    wget -q https://apt.llvm.org/llvm.sh -O /tmp/llvm.sh
    chmod +x /tmp/llvm.sh
  fi
  sudo /tmp/llvm.sh 18 all >/dev/null 2>&1 || true
fi
if ! command -v pkg-config >/dev/null 2>&1; then
  echo "[2/7] 装 pkg-config ..."
  sudo apt-get install -y pkg-config rsync
fi
LLVM_BINDIR="$(llvm-config-18 --bindir 2>/dev/null || llvm-config --bindir)"
echo "[2/7] LLVM bindir: $LLVM_BINDIR"

# 3. 工具链验证
echo "[3/7] prospero-clang --version ..."
"$PS5_PAYLOAD_SDK/bin/prospero-clang" --version | head -1

# 4. 同步源码 (rsync 增量)
echo "[4/7] 同步源码 (Windows -> WSL, 增量) ..."
mkdir -p "$PROJ"
if [ -d "$SRC_WIN/src" ]; then
  rsync -a --delete \
    --exclude='ps5-obj' --exclude='linux-obj' \
    --exclude='web-file-mgr.elf' --exclude='web-file-mgr-linux' \
    --exclude='.build/stub' --exclude='.build/obj' \
    --exclude='.build/probe*' --exclude='.build/probe-work' \
    --exclude='.build/host-test' --exclude='.build/tp' \
    --exclude='.build/*.exe' --exclude='.build/*.txt' \
    --exclude='gen' \
    "$SRC_WIN/" "$PROJ/"
  echo "    已同步自 $SRC_WIN"
else
  echo "    (Windows 端不可见 -- /mnt/c 是否挂载?)"
fi

# 5. 装 libmicrohttpd (staging 模式: make install 到 /tmp, 再 sudo cp 过去)
echo "[5/7] libmicrohttpd (staging + 一次性 sudo cp)..."
if "$PS5_PAYLOAD_SDK/bin/prospero-pkg-config" --modversion libmicrohttpd >/dev/null 2>&1; then
  echo "    已装: $($PS5_PAYLOAD_SDK/bin/prospero-pkg-config --modversion libmicrohttpd) -- 跳过"
else
  # 5a) 下载 tarball (缓存复用)
  if [ ! -f "$CACHE" ] || [ ! -s "$CACHE" ]; then
    echo "    下载 libmicrohttpd-1.0.1.tar.gz..."
    if command -v wget >/dev/null; then
      wget -q https://ftp.gnu.org/gnu/libmicrohttpd/libmicrohttpd-1.0.1.tar.gz -O "$CACHE"
    else
      curl -fsSL https://ftp.gnu.org/gnu/libmicrohttpd/libmicrohttpd-1.0.1.tar.gz -o "$CACHE"
    fi
  fi
  [ -s "$CACHE" ] || { echo "FAIL: 下载失败: $CACHE"; exit 2; }
  echo "    tarball: $CACHE ($(du -h "$CACHE" | cut -f1))"

  # 5b) 解压 + configure + make (普通 user 跑, 输出到临时目录)
  TMPSRC=$(mktemp -d)
  trap 'rm -rf -- "$TMPSRC"' EXIT
  tar xf "$CACHE" -C "$TMPSRC"
  cd "$TMPSRC/libmicrohttpd-1.0.1"
  source "${PS5_PAYLOAD_SDK}/toolchain/prospero.sh"
  export CFLAGS="${CFLAGS:-} -O1 -w"
  echo "    configure (host=x86_64-pc-freebsd)..."
  ./configure --prefix="/user/homebrew" \
    --host=x86_64-pc-freebsd \
    --enable-static --disable-shared \
    --disable-doc --disable-curl --disable-examples \
    --disable-https --disable-openssl \
    >/dev/null 2>&1
  echo "    make -j$(nproc)..."
  make -j"$(nproc)" 2>&1 | tail -10

  # 5c) install 到 STAGE (普通 user 写到 /tmp)
  rm -rf "$STAGE"
  mkdir -p "$STAGE"
  echo "    make install DESTDIR=$STAGE ..."
  make install DESTDIR="$STAGE" 2>&1 | tail -5

  # 5d) 验证 stage 里有产物
  if [ ! -f "$STAGE/user/homebrew/include/microhttpd.h" ]; then
    echo "FAIL: stage 中无 microhttpd.h -- stage 内容:"
    find "$STAGE" -maxdepth 4 -type f | head -10
    exit 2
  fi

  # 5e) 一次性 sudo 拷到 SDK
  echo "    sudo cp stage -> SDK homebrew..."
  sudo mkdir -p "$PS5_PAYLOAD_SDK/target/user/homebrew"
  sudo cp -r "$STAGE/user/homebrew/." "$PS5_PAYLOAD_SDK/target/user/homebrew/"
  echo "    验证 prospero-pkg-config:"
  "$PS5_PAYLOAD_SDK/bin/prospero-pkg-config" --modversion libmicrohttpd
  "$PS5_PAYLOAD_SDK/bin/prospero-pkg-config" --libs libmicrohttpd
fi

# 6. 编译 (不 make clean, 复用 zlib/minizip-ng OBJ 缓存)
echo "[6/7] make all (增量编译, 复用第三方 obj)..."
cd "$PROJ"
export PS5_PAYLOAD_SDK="/opt/ps5-payload-sdk"
# 仅当二进制缺失或源码变更才全量重编
# 重要: 必须包含 assets/* 和 gen-asset-module.py —— 它们经 gen-asset-module.py
# 生成 gen/*.c 进而影响 ELF, 不在列表里就会跳过 make 产生伪"无变更"(v1.8.3
# 被这个 bug 坑过, ELF sha256 没变)。
if [ -f web-file-mgr.elf ]; then
  echo "    已存在 web-file-mgr.elf, 检查源码变更..."
  NEEDS_REBUILD=""
  for src in src/*.c Makefile assets/* gen-asset-module.py \
             third_party/minizip-ng/include/*.h third_party/zlib/include/*.h; do
    [ -e "$src" ] || continue
    if [ "$src" -nt web-file-mgr.elf ]; then
      NEEDS_REBUILD="$src"
      break
    fi
  done
  if [ -z "$NEEDS_REBUILD" ]; then
    echo "    无源码变更 -- 跳过 make"
  else
    echo "    源码变更: $NEEDS_REBUILD"
    if ! make all 2>&1 | tail -60; then
      echo "FAIL: make all 失败(见上)。注意: 旧 ELF 仍留在原地, 但不算新产物"
      exit 6
    fi
  fi
else
  if ! make all 2>&1 | tail -60; then
    echo "FAIL: make all 失败(见上)"
    exit 6
  fi
fi

# 7. 验证 + 同步回 Windows
echo "[7/7] 验证产物 + 同步..."
ELF="$PROJ/web-file-mgr.elf"
if [ ! -f "$ELF" ]; then
  echo "FAIL: ELF 未生成 -- 日志: $LOG"
  # 把日志同步到 Windows .build/ 方便排查
  cp -f "$LOG" "$SRC_WIN/.build/build-elf.log" 2>/dev/null && \
    echo "    日志已同步: $SRC_WIN/.build/build-elf.log"
  exit 3
fi
SIZE=$(stat -c%s "$ELF")
HASH=$(sha256sum "$ELF" | cut -d' ' -f1)
echo "OK: $ELF"
echo "    size: $SIZE bytes (~$((SIZE/1024)) KiB)"
echo "    sha256: $HASH"
echo "    header bytes (期望 ELF64 magic 7f454c46 + class 2 + little-endian 1 + e_machine 0x003e=x86-64):"
head -c 20 "$ELF" | xxd | head -2
echo ""
# 用 od 直接读 offset 18 起的 1 个 16-bit 小端 word = e_machine
EM_RAW=$(od -An -tx2 -j 18 -N 2 "$ELF" | tr -d ' \n')   # e.g. "3e00"
EM_NUM=$((16#${EM_RAW}))                                  # decimal
echo "e_machine = 0x$EM_RAW ($EM_NUM)"
case "$EM_NUM" in
  62) echo "    -> x86-64 (PS5 真机 target: x86_64-sie-ps5)" ;;
  183) echo "    -> aarch64 (注意: PS5 实际是 x86-64, 此结果可疑)" ;;
  *) echo "    -> 未知架构 (期望 62=0x3e, 当前 e_machine=$EM_NUM)" ;;
esac

# 同步 ELF 回项目根
mkdir -p "$SRC_WIN" 2>/dev/null
cp -f "$ELF" "$SRC_WIN/web-file-mgr.elf" 2>&1 && \
  echo "" && echo "[bonus] 已同步回: $SRC_WIN/web-file-mgr.elf" && \
  ls -lh "$SRC_WIN/web-file-mgr.elf"