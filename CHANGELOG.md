# Changelog

All notable changes to **PS5 Web File Manager** are documented in this file.
The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

> Release artifact for v1.7:
> `web-file-mgr.elf` — 427 656 bytes
> sha256 `648e4a00afe52669846df52ee5342bab42ea10050d555d5a1d4fa602653f514b`
> ELF class 64, little-endian, e_machine `0x003e` (x86_64-sie-ps5)

---

## [v1.7] — 2026-09-04

**ZIP extraction: large-file profile, three-phase engine, host test suite.**

### Added

- **Large-file profile** (`ZIPX_LIMITS_LARGE`) for ZIP extraction — relaxed
  caps of **500 000** entries, **2 TiB** total uncompressed, **1 TiB** per
  entry, **1000 : 1** compression ratio. Enable via the new `large=1`
  argument on `POST /api/extract`. The UI prompts the user automatically
  whenever the archive on disk is larger than 60 GiB (`LARGE_FILE_THRESHOLD_BYTES`).
- **Standalone three-phase ZIP engine** (`src/zip_extract.{c,h}`) — the model
  `SCAN → EXTRACT → PUBLISH → CLEANUP`. Each entry is written into a staging
  directory first, fsynced, then atomically renamed into the destination. Any
  mid-archive failure rolls back partial changes.
- **Profile lookup helper** `zipx_limits_profile(int)` and the new macros
  `ZIPX_LIMITS_DEFAULT` (0) and `ZIPX_LIMITS_LARGE` (1). The public
  `zipx_extract()` signature is unchanged.
- **Opt-in API field** `large` on `POST /api/extract`, backed by a new
  `extract_large` flag in `file_task_t` (`src/filemgr_internal.h`,
  `src/extract.c`).
- **Frontend wiring** (`assets/main.js`) — `LARGE_FILE_THRESHOLD_BYTES`,
  `shouldPromptLargeMode()`, `promptLargeMode()`, consumed by
  `actionExtract()` and `uploadAndExtractFile()`.
- **Bilingual UI strings** (`assets/lang-{en,zh}.js`) —
  `extractLargeAsk` and `extractLargeActive`.
- **Host-side C test suite** (`tests/`) — POSIX-runnable, no PS5 SDK
  required. **69 checks** at release: traversal, ZIP64, encryption rejection,
  conflict policies, large-file profile switching.
- **Cross-compile helper** (`.build/build-elf.sh`) — staging-mode build that
  works around the read-only SDK install location.
- **Demo page** (`.build/extract-demo.html`) — visualises the three policy
  combinations (fail / overwrite / merge) against the new profile table.

### Changed

- **Engine internals** rewritten around the three-phase model. Public API
  (`zipx_extract()`, `zipx_default_limits()`) is unchanged — old callers
  compile and link clean.
- **`file_task_t`** gains `extract_large` (`src/filemgr_internal.h`).
  Internal-only; downstream consumers reading the task struct need to
  recognise the new field.
- **`gen-asset-module.py`** continues to gzip JS into the binary; the
  helpers in `.build/check-elf-gzip.py` are the supported way to verify that
  a fresh build picked up asset changes (regular `strings` won't see them).

### Security

- Default ZIP caps are unchanged: **512 GiB** total / **64 GiB** per entry /
  **200 : 1** ratio.
- The large profile is **never** activated server-side on its own — the
  client must explicitly send `large=1` (either via the UI prompt or directly
  via the API).
- Encryption, path traversal, symbolic links, FIFOs and unresolved conflicts
  are still rejected before any output file is opened.
- Strict value matching: only the literal `"1"` enables the large profile;
  `true`, `yes`, `on` are all treated as `0`. Matches the existing
  `remove_source=` semantics.

### Known limitations

- Multi-volume / split ZIP archives (`.zip` + `.z01`, `.z02`, …) are not
  stitched by the engine. minizip-ng has the API; wiring it is post-v1.7.
- The 60 GiB frontend threshold for the large-profile prompt is hardcoded
  (`LARGE_FILE_THRESHOLD_BYTES`, `assets/main.js` line 813).
- The large profile does **not** run `statvfs()` against `dst_dir` before
  extraction; free-space preflight is on the post-v1.7 roadmap.
- Bomb-shaped archives with compression ratio **> 1000 : 1** are rejected
  under both profiles (`bomb.zip` with 4 MiB of `'A'` has ratio ≈ 1026 and
  hits this wall under the large profile).

### Verification

```sh
make                                                   # builds web-file-mgr.elf
ls -la web-file-mgr.elf
sha256sum web-file-mgr.elf                             # 648e4a00…
file  web-file-mgr.elf                                 # ELF 64-bit LSB pie, x86-64
od -An -tx1 -N20 web-file-mgr.elf                     # 7f45 4c46 0201 + e_machine 003e

(cd tests && bash run-tests.sh)                        # 69 checks, 0 failures

python3 .build/check-elf-gzip.py ./web-file-mgr.elf    # 7/7 large-mode keys in ELF
```

### Technical notes

A long-form technical write-up of this upgrade (architecture delta, three-phase
model, behaviour contract, trade-offs, future work) lives in
[`docs/UPGRADE-v1.7-zip-large-file-profile.md`](./docs/UPGRADE-v1.7-zip-large-file-profile.md).

### Credits

The same as v1.6 — see [README.md → Credits](./README.md#credits).
