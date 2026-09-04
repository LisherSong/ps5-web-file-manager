#!/usr/bin/env bash
# Host test runner for the ZIP extraction engine.
# Builds the vendored minizip-ng/zlib sources, the engine and the test suite
# with the host compiler and runs the suite.
#
#   ./tests/run-tests.sh
#
# On Windows this expects MinGW gcc in PATH (Git Bash is fine).

set -e

ROOT="$(cd "$(dirname "$0")/.." && pwd -W 2>/dev/null || pwd)"
BUILD="$ROOT/.build/host-test"
PYTHON="${PYTHON:-python3}"
CC="${CC:-gcc}"

rm -rf "$BUILD"
mkdir -p "$BUILD"

"$PYTHON" "$ROOT/tests/make_fixtures.py"

MZ_CFLAGS=(-I"$ROOT/third_party/minizip-ng/include" -DHAVE_ZLIB -DZLIB_COMPAT)
HOST_KIND=posix
# MinGW has no O_NOFOLLOW; the flag is only a host build workaround.
case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*) MZ_CFLAGS+=(-DO_NOFOLLOW=0); HOST_KIND=windows ;;
esac

# zlib + minizip-ng (plain POSIX sources, no shim needed)
for src in "$ROOT"/third_party/zlib/src/*.c "$ROOT"/third_party/minizip-ng/src/*.c; do
  name="$(basename "$src" .c)"
  extra=()
  # minizip's POSIX backend needs utime/lstat/symlink/readlink, stubbed on Windows.
  case "$name:$HOST_KIND" in
    mz_os_posix:windows) extra=(-include "$ROOT/tests/mingw_host.h") ;;
  esac
  "$CC" -c -O2 -w -I"$ROOT/third_party/zlib/include" -DHAVE_UNISTD_H=1 \
    "${MZ_CFLAGS[@]}" "${extra[@]}" -o "$BUILD/$name.o" "$src"
done

COMPAT_INC="$ROOT/tests/compat"

# The engine and the test suite get the POSIX shim on Windows hosts.
"$CC" -c -O2 -Wall -Wextra -Wno-unused-parameter \
  -I"$ROOT/third_party/minizip-ng/include" -I"$ROOT/src" -I"$COMPAT_INC" \
  -include "$ROOT/tests/posix_compat.h" \
  -o "$BUILD/zip_extract.o" "$ROOT/src/zip_extract.c"

"$CC" -c -O2 -Wall -Wextra -Wno-unused-parameter -I"$ROOT/src" \
  -I"$COMPAT_INC" -include "$ROOT/tests/posix_compat.h" \
  -o "$BUILD/test_zip_extract.o" "$ROOT/tests/test_zip_extract.c"

objs=()
for obj in "$BUILD"/*.o; do
  case "$obj" in
    */zip_extract.o|*/test_zip_extract.o) continue ;;
  esac
  objs+=("$obj")
done

"$CC" -O2 -o "$BUILD/test-zip-extract" \
  "$BUILD/zip_extract.o" "$BUILD/test_zip_extract.o" "${objs[@]}"

"$BUILD/test-zip-extract" "$ROOT/tests/fixtures" "$BUILD/work"
