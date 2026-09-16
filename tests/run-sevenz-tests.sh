#!/usr/bin/env bash
# Host test runner for the 7z engine.
#
#   ./tests/run-sevenz-tests.sh
#
# Builds the vendored LZMA SDK subset plus the project's own folder decoder
# (src/sevenz_chain.c), generates real .7z fixtures with a 7-Zip binary,
# extracts every fixture through that decoder and compares the result byte for
# byte against the source tree.
#
# On Windows this expects MinGW gcc in PATH and must be started with the MSYS
# bash explicitly (`/usr/bin/bash tests/run-sevenz-tests.sh`) -- a bare `bash`
# can resolve to C:\Windows\System32\bash.exe, i.e. the WSL launcher.

set -e

ROOT="$(cd "$(dirname "$0")/.." && pwd -W 2>/dev/null || pwd)"
BUILD="$ROOT/.build/sevenz-test"
SEVENZ_DIR="$ROOT/third_party/7z"
FIXTURES="$ROOT/tests/fixtures-7z"
COMPAT_INC="$ROOT/tests/compat"
PYTHON="${PYTHON:-python3}"
CC="${CC:-gcc}"

# Archives the engine cannot read yet.  Each entry needs a reason; when one of
# them starts passing the script says so, so the list cannot rot.
KNOWN_GAPS="aeshe"
#   aeshe       - the header itself is encrypted (-mhe=on).  Reading it means
#                 decrypting a standalone 7z stream *before* any folder is
#                 known, i.e. a header parser of our own; the vendored SDK
#                 refuses with SZ_ERROR_UNSUPPORTED before we are involved.

# Must match PASSWORD in tests/make_sevenz_fixtures.py.
FIXTURE_PASSWORD="Secret123"

find "$BUILD" -maxdepth 1 -type f \( -name '*.o' -o -name '*.exe' \) -delete 2>/dev/null || true
mkdir -p "$BUILD"

# ---------------------------------------------------------------- vendor
CFLAGS_7Z=(-O2 -w -DZ7_PPMD_SUPPORT -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE
           -DNDEBUG -D_REENTRANT)

VENDOR_OBJS=()
for src in "$SEVENZ_DIR"/*.c; do
  name="$(basename "$src" .c)"
  "$CC" -c "${CFLAGS_7Z[@]}" -o "$BUILD/$name.o" "$src"
  VENDOR_OBJS+=("$BUILD/$name.o")
done

# The engine modules are held to the same strictness as the rest of src/.
# sevenz_volstream reuses the ZIP side's volume-set detector, so zipx_volume
# is built here as well.
"$CC" -c -O2 -Wall -Wextra -Werror -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE \
  -I"$SEVENZ_DIR" -I"$ROOT/src" -o "$BUILD/sevenz_chain.o" \
  "$ROOT/src/sevenz_chain.c"
"$CC" -c -O2 -Wall -Wextra -Werror -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE \
  -I"$SEVENZ_DIR" -I"$ROOT/src" -o "$BUILD/sevenz_volstream.o" \
  "$ROOT/src/sevenz_volstream.c"
"$CC" -c -O2 -Wall -Wextra -Werror -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE \
  -I"$SEVENZ_DIR" -I"$ROOT/src" -o "$BUILD/sevenz_mt.o" "$ROOT/src/sevenz_mt.c"
"$CC" -c -O2 -Wall -Wextra -Werror -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE \
  -I"$ROOT/src" -o "$BUILD/zipx_volume.o" "$ROOT/src/zipx_volume.c"

# The extraction facade is engine code too, so it gets the host POSIX shim as
# well as the same strictness (see tests/run-tests.sh for the ZIP/RAR pair).
# zipx_common carries the limit profiles and the status text that all three
# engines share.
"$CC" -c -O2 -Wall -Wextra -Werror -I"$ROOT/src" -o "$BUILD/zipx_common.o" \
  "$ROOT/src/zipx_common.c"
"$CC" -c -O2 -Wall -Wextra -Werror -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE \
  -I"$SEVENZ_DIR" -I"$ROOT/src" -I"$COMPAT_INC" \
  -include "$ROOT/tests/posix_compat.h" \
  -o "$BUILD/sevenz_extract.o" "$ROOT/src/sevenz_extract.c"

ENGINE_OBJS=("$BUILD/sevenz_chain.o" "$BUILD/sevenz_volstream.o"
             "$BUILD/sevenz_mt.o" "$BUILD/zipx_volume.o"
             "$BUILD/zipx_common.o")

FACADE_OBJS=("$BUILD/sevenz_extract.o" "${ENGINE_OBJS[@]}")

# unrar-style extra libs are only needed by the Windows path of 7zFile.c.
EXTRA_LIBS=()
case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*) EXTRA_LIBS=(-lole32 -loleaut32 -luuid -ladvapi32 -luser32 -lshell32) ;;
esac

"$CC" -O2 -w -I"$SEVENZ_DIR" -I"$ROOT/src" -o "$BUILD/sevenz_chain_e2e" \
  "$ROOT/tests/sevenz_chain_e2e.c" "${ENGINE_OBJS[@]}" "${VENDOR_OBJS[@]}" \
  "${EXTRA_LIBS[@]}"

# The facade driver: the same strict flags as the engine, plus the POSIX shim,
# because a MinGW host has neither statvfs() nor a two-argument mkdir().
"$CC" -O2 -Wall -Wextra -I"$SEVENZ_DIR" -I"$ROOT/src" -I"$COMPAT_INC" \
  -include "$ROOT/tests/posix_compat.h" \
  -o "$BUILD/test_sevenz_extract" "$ROOT/tests/test_sevenz_extract.c" \
  "${FACADE_OBJS[@]}" "${VENDOR_OBJS[@]}" "${EXTRA_LIBS[@]}"

# The SDK-baseline driver is kept buildable: it is the fastest way to tell an
# engine bug from an SDK one when a fixture starts failing.
"$CC" -O2 -w -I"$SEVENZ_DIR" -o "$BUILD/sevenz_e2e" \
  "$ROOT/tests/sevenz_e2e.c" "${VENDOR_OBJS[@]}" "${EXTRA_LIBS[@]}"

# ---------------------------------------------------------------- fixtures
if [ ! -f "$FIXTURES/lzma2.7z" ]; then
  echo "== generating 7z fixtures =="
  if ! "$PYTHON" "$ROOT/tests/make_sevenz_fixtures.py"; then
    echo "SKIP: no 7z tool available, cannot build fixtures" >&2
    exit 0
  fi
fi

# ---------------------------------------------------------------- matrix
pass=0
fail=0

is_gap() {
  for g in $KNOWN_GAPS; do
    [ "$g" = "$1" ] && return 0
  done
  return 1
}

run_case() {
  local name="$1" archive="$2" password="${3:-}"
  local out log rc

  # A fresh directory per case keeps the run repeatable without deleting a tree
  # of previous results, which guarded shells refuse to do.
  out="$(mktemp -d "$BUILD/out/XXXXXX")" || return
  log="$out.log"

  rc=0
  if [ -n "$password" ]; then
    "$BUILD/sevenz_chain_e2e" "$archive" "$out" "$password" >"$log" 2>&1 || rc=$?
  else
    "$BUILD/sevenz_chain_e2e" "$archive" "$out" >"$log" 2>&1 || rc=$?
  fi

  if [ "$rc" -eq 0 ] &&
     diff -r "$FIXTURES/_src" "$out/_src" >/dev/null 2>&1; then
    if is_gap "$name"; then
      printf '  %-12s GAP CLOSED (remove from KNOWN_GAPS)\n' "$name"
      fail=$((fail + 1))
    else
      printf '  %-12s ok\n' "$name"
      pass=$((pass + 1))
    fi
    return
  fi

  if is_gap "$name"; then
    printf '  %-12s known gap (%s)\n' "$name" "$(head -1 "$log")"
    pass=$((pass + 1))
  else
    printf '  %-12s FAIL\n' "$name"
    sed -n '1,20p' "$log" | sed 's/^/      /'
    fail=$((fail + 1))
  fi
}

echo "== 7z fixture matrix (engine: src/sevenz_chain.c) =="
for a in store lzma2 lzma ppmd bcj delta utf8 bcj2 solidoff bcj2off aes aeshe; do
  [ -f "$FIXTURES/$a.7z" ] || continue
  case "$a" in
    aes|aeshe) run_case "$a" "$FIXTURES/$a.7z" "$FIXTURE_PASSWORD" ;;
    *) run_case "$a" "$FIXTURES/$a.7z" ;;
  esac
done
if [ -f "$FIXTURES/vol.7z.001" ]; then
  run_case "vol.7z.001" "$FIXTURES/vol.7z.001"
fi

# ------------------------------------------------- extraction facade
# The same fixtures again, but through src/sevenz_extract.c: staging, publish,
# limits, conflict policy and name validation all have to agree with the
# decoder before the format is wired into the server.
echo
echo "== 7z extraction facade (engine: src/sevenz_extract.c) =="
mkdir -p "$BUILD/fx"
for a in store lzma2 lzma ppmd bcj delta utf8 bcj2 solidoff bcj2off aes; do
  [ -f "$FIXTURES/$a.7z" ] || continue
  out="$(mktemp -d "$BUILD/fx/XXXXXX")" || continue
  target="$out/$a"
  mkdir -p "$target"
  rc=0
  case "$a" in
    aes) "$BUILD/test_sevenz_extract" "$FIXTURES/$a.7z" "$target" \
           "$FIXTURE_PASSWORD" >"$out.log" 2>&1 || rc=$? ;;
    *)   "$BUILD/test_sevenz_extract" "$FIXTURES/$a.7z" "$target" \
           >"$out.log" 2>&1 || rc=$? ;;
  esac
  if [ "$rc" -eq 0 ] && diff -r "$FIXTURES/_src" "$target/_src" >/dev/null 2>&1; then
    printf '  %-12s ok\n' "$a"
    pass=$((pass + 1))
  else
    printf '  %-12s FAIL\n' "$a"
    sed -n '1,20p' "$out.log" | sed 's/^/      /'
    fail=$((fail + 1))
  fi
done

# A byte-split set goes through the same facade.
if [ -f "$FIXTURES/vol.7z.001" ]; then
  out="$(mktemp -d "$BUILD/fx/XXXXXX")" || out=
  if [ -n "$out" ]; then
    rc=0
    "$BUILD/test_sevenz_extract" "$FIXTURES/vol.7z.001" "$out/vol" \
      >"$out.log" 2>&1 || rc=$?
    if [ "$rc" -eq 0 ] && diff -r "$FIXTURES/_src" "$out/vol/_src" >/dev/null 2>&1; then
      printf '  %-12s ok\n' "vol.7z.001"
      pass=$((pass + 1))
    else
      printf '  %-12s FAIL\n' "vol.7z.001"
      sed -n '1,20p' "$out.log" | sed 's/^/      /'
      fail=$((fail + 1))
    fi
  fi
fi

echo
echo "== 7z error and policy paths =="
mkdir -p "$BUILD/cases"
cases_work="$(mktemp -d "$BUILD/cases/XXXXXX")"
if "$BUILD/test_sevenz_extract" --cases "$FIXTURES" "$cases_work"; then
  pass=$((pass + 1))
else
  fail=$((fail + 1))
fi

echo
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
