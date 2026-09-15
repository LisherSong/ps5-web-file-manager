#!/usr/bin/env bash
# ===========================================================================
# ps5-web-file-manager -- Windows 端一键构建入口
#
# 在 Windows 的 Git Bash / MinGW64 bash 里这样跑（注意用 /usr/bin/bash）：
#     /usr/bin/bash .build/build-win.sh
#
# 它只做一件事：把 WSL 脚本喂给 wsl.exe 执行，然后透传退出码。
# 真正的 sync / make / verify / 拷回都在 .build/build-elf-wsl.sh 里。
#
# 注意：
#   * 用 `bash < script` 走 stdin，不要用 `bash -c '...'` ——
#     路径含空格时 -c 的参数会被 wsl.exe 拆断。
#   * 别写裸 `bash .build/build-win.sh`，那个 bash 可能解析到
#     C:\Windows\System32\bash.exe（WSL 启动器），脚本会跑进 Linux 环境。
# ===========================================================================

set -uo pipefail

REPO='/c/Users/songl/Desktop/Web File Manager/ps5-web-file-manager'
WSL_DISTRO='Ubuntu-22.04'
WSL_DIR='/home/song/ps5-web-file-manager/.build'
HOST_SCRIPT="$REPO/.build/build-elf-wsl.sh"

[ -f "$HOST_SCRIPT" ] || { echo "[FAIL] 找不到 $HOST_SCRIPT"; exit 1; }

# 先把 WSL 脚本推进去（.build/ 被 rsync 排除，WSL 侧可能没有）
if ! wsl.exe -d "$WSL_DISTRO" -- test -f "$WSL_DIR/build-elf-wsl.sh" 2>/dev/null; then
  echo "[setup] WSL 侧缺少脚本，拷贝中..."
  wsl.exe -d "$WSL_DISTRO" -- mkdir -p "$WSL_DIR" || exit 1
  wsl.exe -d "$WSL_DISTRO" -- cp \
    '/mnt/c/Users/songl/Desktop/Web File Manager/ps5-web-file-manager/.build/build-elf-wsl.sh' \
    "$WSL_DIR/build-elf-wsl.sh" || {
      echo "[FAIL] 无法写入 WSL 文件系统"
      exit 1
    }
fi

echo "[run] wsl.exe -d $WSL_DISTRO -- bash < build-elf-wsl.sh"
echo "=================================================================="
wsl.exe -d "$WSL_DISTRO" -- bash < "$HOST_SCRIPT"
rc=$?
echo "=================================================================="

if [ "$rc" -eq 0 ]; then
  echo "[OK] 构建完成 -> $REPO/web-file-mgr.elf"
else
  echo "[FAIL] 构建失败 (exit=$rc)"
fi
exit "$rc"
