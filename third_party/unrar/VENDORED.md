# Vendored: dmc_unrar 1.7.0

## Source

- Repository: https://github.com/DrMcCoy/dmc_unrar
- Upstream commit: master @ 2026-09-05 (DRMcCoy/dmc_unrar master)
- Author: Sven Hesse (DrMcCoy) <drmccoy@drmccoy.de>
- License: GPL-2.0-or-later (see COPYING in this directory)

We vendor only the implementation source file `dmc_unrar.c` (11598 LOC, ~365 KiB).
The repository does not ship a separate header; the entire API is exposed
inline in the `.c` file (see top-of-file docstrings + `example.c`).

## What is supported

- RAR 1.5 / 2.0 / 2.6 / 2.9 / 3.0 / 3.6 / 4.0 / 5.0 archive formats
- Solid blocks, dictionary sizes up to 4 MiB (RAR4) / 32 MiB (RAR5)
- PPMd decompression (used by RAR 3.0+)
- Archive comments (ASCII / UTF-16LE)
- Encrypted archive/file *detection* (returns an explicit error)

## What is NOT supported (by upstream design)

- **Encrypted RAR files** — `DMC_UNRAR_(ARCHIVE|FILE)_UNSUPPORTED_ENCRYPTED`.
  We surface this as `ZIPX_ERR_UNSUPPORTED` in the wrapper. Upstream
  intentionally omits decryption to avoid patent complications.
- **Multi-volume (split) RAR archives** — `DMC_UNRAR_ARCHIVE_UNSUPPORTED_VOLUMES`.
  We surface this as `ZIPX_ERR_UNSUPPORTED`. dmc_unrar does not chain
  `.partNN.rar` siblings; users must join / unrar on a PC first.
- Symbolic links, FIFOs, sockets, devices — `DMC_UNRAR_FILE_UNSUPPORTED_LINK`.

## Why dmc_unrar and not the upstream rarlab UnRAR

The rarlab UnRAR source tree (https://www.rarlab.com/rar/unrarsrc-*.tar.gz,
mirrored at https://github.com/opello/unrar) supports encrypted and
multi-volume archives, but it is a 150-file C++17 codebase with its own
filesystem abstraction, Windows registry probe, and threading pool that
requires `-DRAR_SMP`. Integrating it cleanly into the PS5 toolchain would
need:

1. A separate CXX linker step in the Makefile.
2. Verification that `prospero-clang` (-std=c++17, freestanding) builds it
   without libc++ dependencies beyond what `prospero-pkg-config` exposes.
3. A new exit-error contract for the `--strip` toolchain.
4. ~10× the audit surface (every `#include <windows.h>` etc.).

We decided the cost > benefit for v1.8: single-volume RAR is the dominant
PS5 Web File Manager use case (uploading an unzipped payload, dumping a
single .pkg as `.rar` for transfer). Users needing multi-volume /
encrypted RAR are explicitly informed via the web UI error message that
they should extract on a PC first.

## Upgrading to a fuller library (v1.9 plan)

When (if) demand justifies it, the recommended upgrade path is:

1. Vendor `https://github.com/opello/unrar` (a faithful mirror of rarlab's
   UnRAR 7.x) into `third_party/unrar/` (replacing dmc_unrar.c).
2. Add a CXX compilation rule in `Makefile`:
   ```
   THIRD_PARTY_CPP_SRCS := $(wildcard third_party/unrar/*.cpp)
   PS5_TP_CPP_OBJS   := $(patsubst %.cpp,ps5-obj/%.o,$(THIRD_PARTY_CPP_SRCS))
   $(BIN): ... $(PS5_TP_CPP_OBJS)
   	$(CXX) -o $@ ... $(PS5_TP_CPP_OBJS) ...
   ```
3. Build dmc_unrar-style driver code against the DLL API
   (`-DRARDLL`, link via `dll.cpp`). The RAR API entry points
   `RAROpenArchiveEx`, `RARReadHeaderEx`, `RARProcessFileW` accept the
   password via `RARSetPassword`.
4. Rewrite `src/rar_extract.c` to call the new engine while preserving
   the `rar_extract()` signature and the `zipx_status_t` error mapping.
5. Tests in `tests/test_rar_extract.c` should not need to change — only
   the engine behind `rar_extract()`.

## Modifications to dmc_unrar.c

None. We pin the upstream file verbatim to keep the upgrade path trivial.