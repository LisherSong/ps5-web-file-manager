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
PYTHON="${PYTHON:-python3}"
CC="${CC:-gcc}"

# Archives the engine cannot read yet.  Each entry needs a reason; when one of
# them starts passing the script says so, so the list cannot rot.
KNOWN_GAPS="aes aeshe vol.7z.001"
#   aes         - 7zAES coder not implemented yet
#   aeshe       - encrypted header (-mhe=on), same coder
#   vol.7z.001  - multi-volume input; needs the volume-set stream

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

# The engine module is held to the same strictness as the rest of src/.
"$CC" -c -O2 -Wall -Wextra -Werror -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE \
  -I"$SEVENZ_DIR" -o "$BUILD/sevenz_chain.o" "$ROOT/src/sevenz_chain.c"

# unrar-style extra libs are only needed by the Windows path of 7zFile.c.
EXTRA_LIBS=()
case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*) EXTRA_LIBS=(-lole32 -loleaut32 -luuid -ladvapi32 -luser32 -lshell32) ;;
esac

"$CC" -O2 -w -I"$SEVENZ_DIR" -I"$ROOT/src" -o "$BUILD/sevenz_chain_e2e" \
  "$ROOT/tests/sevenz_chain_e2e.c" "$BUILD/sevenz_chain.o" "${VENDOR_OBJS[@]}" \
  "${EXTRA_LIBS[@]}"

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
  local name="$1" archive="$2"
  local out log

  # A fresh directory per case keeps the run repeatable without deleting a tree
  # of previous results, which guarded shells refuse to do.
  out="$(mktemp -d "$BUILD/out/XXXXXX")" || return
  log="$out.log"

  if "$BUILD/sevenz_chain_e2e" "$archive" "$out" >"$log" 2>&1 &&
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
  run_case "$a" "$FIXTURES/$a.7z"
done
if [ -f "$FIXTURES/vol.7z.001" ]; then
  run_case "vol.7z.001" "$FIXTURES/vol.7z.001"
fi

echo
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
