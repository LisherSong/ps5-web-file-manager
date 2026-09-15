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

# 每次都把最新的 WSL 脚本推进去（.build/ 被 rsync 排除，WSL 侧不会自己更新；
# 只做一次会导致改了脚本还在跑旧版 —— 这个坑踩过）
wsl.exe -d "$WSL_DISTRO" -- mkdir -p "$WSL_DIR" || exit 1
wsl.exe -d "$WSL_DISTRO" -- cp \
  '/mnt/c/Users/songl/Desktop/Web File Manager/ps5-web-file-manager/.build/build-elf-wsl.sh' \
  "$WSL_DIR/build-elf-wsl.sh" || {
    echo "[FAIL] 无法写入 WSL 文件系统"
    exit 1
  }

echo "[run] wsl.exe -d $WSL_DISTRO -- bash < build-elf-wsl.sh"
echo "=================================================================="
wsl.exe -d "$WSL_DISTRO" -- bash < "$HOST_SCRIPT"
rc=$?
echo "=================================================================="

if [ "$rc" -eq 0 ]; then
  # 输出文件带版本号（web-file-mgr-<VERSION_TAG>.elf），列出实际产物
  echo "[OK] 构建完成，产物："
  ls -1 "$REPO"/web-file-mgr-*.elf 2>/dev/null | sed 's#^#    #'
else
  echo "[FAIL] 构建失败 (exit=$rc)"
fi
exit "$rc"
