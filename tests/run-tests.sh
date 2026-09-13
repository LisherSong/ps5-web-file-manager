#!/usr/bin/env bash
# Host test runner for the ZIP and RAR extraction engines.
# Builds the vendored minizip-ng/zlib/dmc_unrar sources, both engines and
# the test suites with the host compiler and runs each suite.
#
#   ./tests/run-tests.sh
#
# On Windows this expects MinGW gcc in PATH (Git Bash is fine).

set -e

ROOT="$(cd "$(dirname "$0")/.." && pwd -W 2>/dev/null || pwd)"
BUILD="$ROOT/.build/host-test"
PYTHON="${PYTHON:-python3}"
CC="${CC:-gcc}"

# Clean previous build outputs without nuking the whole tree (avoids
# bulk-delete guards); stale fixture copies in work-* dirs are fine because
# make_fixtures.py rewrites fixtures/ and the suites recreate their workdirs.
find "$BUILD" -maxdepth 1 -type f -name '*.o' -delete 2>/dev/null || true
find "$BUILD" -maxdepth 1 -type f -name 'test-*' -delete 2>/dev/null || true
find "$BUILD" -maxdepth 1 -type f -name '*.log' -delete 2>/dev/null || true
mkdir -p "$BUILD"

"$PYTHON" "$ROOT/tests/make_fixtures.py"
"$PYTHON" "$ROOT/tests/make_split_fixtures.py"

MZ_CFLAGS=(-I"$ROOT/third_party/minizip-ng/include" -DHAVE_ZLIB -DZLIB_COMPAT -D_FILE_OFFSET_BITS=64)
RAR_CFLAGS=(-I"$ROOT/third_party/unrar" -DDMC_UNRAR_DISABLE_BE32TOH_BE64TOH=1)
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

# unrar 7.20.1 (RARDLL source set; compiled with the host C++ compiler).
UNRAR7_SRCS="$ROOT/third_party/unrar7"
UNRAR7_CFLAGS=(-O2 -w -std=c++17 -DRARDLL -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE)
CXX="${CXX:-g++}"
for src in \
  archive arcread blake2s cmddata consio crc crypt dll encname errhnd extinfo \
  extract filcreat file filefn filestr find getbits global hash headers isnt \
  largepage match motw options pathfn qopen rar rarpch rarvm rawread rdwrfn \
  rijndael rs rs16 scantree secpassword sha1 sha256 smallfn strfn strlist \
  system threadpool timefn ui unicode unpack volume; do
  "$CXX" -c "${UNRAR7_CFLAGS[@]}" -o "$BUILD/unrar7_$src.o" \
    "$UNRAR7_SRCS/$src.cpp" || exit 1
done

COMPAT_INC="$ROOT/tests/compat"

# The engines and the test suites get the POSIX shim on Windows hosts.
"$CC" -c -O2 -Wall -Wextra -Wno-unused-parameter \
  -I"$ROOT/third_party/minizip-ng/include" -I"$ROOT/src" -I"$COMPAT_INC" \
  -include "$ROOT/tests/posix_compat.h" \
  -o "$BUILD/zip_extract.o" "$ROOT/src/zip_extract.c"

# Format-independent helpers (limits profiles + status string) live here.
"$CC" -c -O2 -Wall -Wextra -Wno-unused-parameter -I"$ROOT/src" \
  -I"$COMPAT_INC" -include "$ROOT/tests/posix_compat.h" \
  -o "$BUILD/zipx_common.o" "$ROOT/src/zipx_common.c"

# Volume support: the concatenating stream and the volume set detector.
"$CC" -c -O2 -Wall -Wextra -Wno-unused-parameter \
  -I"$ROOT/third_party/minizip-ng/include" -I"$ROOT/src" -I"$COMPAT_INC" \
  -include "$ROOT/tests/posix_compat.h" \
  -o "$BUILD/zipx_volstream.o" "$ROOT/src/zipx_volstream.c"

"$CC" -c -O2 -Wall -Wextra -Wno-unused-parameter -I"$ROOT/src" \
  -I"$COMPAT_INC" -include "$ROOT/tests/posix_compat.h" \
  -o "$BUILD/zipx_volume.o" "$ROOT/src/zipx_volume.c"

"$CC" -c -O2 -Wall -Wextra -Wno-unused-parameter \
  -I"$ROOT/third_party/minizip-ng/include" -I"$ROOT/third_party/unrar7" -I"$ROOT/src" -I"$COMPAT_INC" \
  -include "$ROOT/tests/posix_compat.h" \
  -o "$BUILD/rar_extract.o" "$ROOT/src/rar_extract.c"

"$CC" -c -O2 -Wall -Wextra -Wno-unused-parameter -I"$ROOT/src" \
  -I"$COMPAT_INC" -include "$ROOT/tests/posix_compat.h" \
  -o "$BUILD/test_zip_extract.o" "$ROOT/tests/test_zip_extract.c"

"$CC" -c -O2 -Wall -Wextra -Wno-unused-parameter -I"$ROOT/src" \
  -I"$COMPAT_INC" -include "$ROOT/tests/posix_compat.h" \
  -o "$BUILD/test_rar_extract.o" "$ROOT/tests/test_rar_extract.c"

objs=()
rar_objs=()
for obj in "$BUILD"/*.o; do
  case "$obj" in
    */zip_extract.o|*/zipx_common.o|*/rar_extract.o|*/test_zip_extract.o|*/test_rar_extract.o) continue ;;
    */unrar7_*.o) rar_objs+=("$obj"); continue ;;
  esac
  objs+=("$obj")
done

"$CC" -O2 -o "$BUILD/test-zip-extract" \
  "$BUILD/zip_extract.o" "$BUILD/zipx_common.o" "$BUILD/test_zip_extract.o" "${objs[@]}"

# The RAR test links the unrar7 objects, so it needs the C++ driver.
# Windows unrar system.cpp references SetSuspendState (PowrProf).
RAR_LIBS=()
[ "$HOST_KIND" = windows ] && RAR_LIBS=(-lpowrprof)
"$CXX" -O2 -o "$BUILD/test-rar-extract" \
  "$BUILD/rar_extract.o" "$BUILD/test_rar_extract.o" \
  "$BUILD/zip_extract.o" "$BUILD/zipx_common.o" "${objs[@]}" "${rar_objs[@]}" "${RAR_LIBS[@]}"

"$BUILD/test-zip-extract" "$ROOT/tests/fixtures" "$BUILD/work-zip"
# Real RAR fixtures (v6 / multi-volume / encrypted) live in fixtures-real/,
# generated by tests/make-rar-fixtures.bat (WinRAR required); fixtures/
# itself is wiped by make_fixtures.py on every run.
"$BUILD/test-rar-extract" "$ROOT/tests/fixtures" "$BUILD/work-rar" \
  "$ROOT/tests/fixtures-real"
