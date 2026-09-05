# Changelog

All notable changes to **PS5 Web File Manager** are documented in this file.
The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

> Release artifact for v1.8.2:
> `web-file-mgr.elf` — size 509 704 bytes (~497 KiB)
> sha256 `1b2c3d68b35e32737105f17d14a80a3c159ceca0cabd274ee168cbcd81906f65`
> ELF class 64, little-endian, e_machine `0x003e` (x86_64-sie-ps5)
>
> Source delta vs v1.8.1: 5 files relaxed (`src/zip_extract.c` k_default_limits
> + k_large_limits, `assets/main.js` `LARGE_FILE_THRESHOLD_BYTES`,
> `assets/lang-{en,zh}.js` copy, `tests/test_zip_extract.c` advertised-number
> assertion, README/HANDOVER numeric references) + 2 PS5-only build fixes
> (`Makefile` CFLAGS `-Ithird_party/unrar`, `src/extract.c` forward
> declaration of `extract_progress`); no vendored or engine changes.

## [v1.8.2] — 2026-09-05

**Hotfix: default ZIP extraction limits cover 3A-game single-file archives.**

The default `k_default_limits` profile is now **2 TiB total / 512 GiB per
entry / 500 : 1 ratio** (was 1 TiB / 256 GiB / 500 : 1 in v1.8.1). The
frontend threshold `LARGE_FILE_THRESHOLD_BYTES` is bumped from 240 GiB to
**480 GiB** to match. The `large=1` profile is widened to **4 TiB total
/ 1 TiB per entry / 1000 : 1 ratio** (was 2 TiB / 1 TiB / 1000 : 1); the
large profile must always be strictly more permissive than default.

Why: a 3A-game archive with a single ~300 GiB uncompressed file was
**silently rejected by the default profile** (`scan_archive` returns
`ZIPX_ERR_LIMIT_FILE_SIZE` in `src/zip_extract.c` line ~717 — the request
never reaches the frontend confirmation prompt, so the user just sees
"卡壳"). The 256 GiB default cap was tuned for PS5 system backups (which
have many smaller entries, not a single huge file) and was wrong for the
3A-game single-file case. The default cap is now 512 GiB so a typical
3A archive extracts under the default profile without prompting.

The safety argument is unchanged from v1.8.1: zip-bomb defence is
`check_space()` (`statvfs`-based real disk space check before staging) +
`max_ratio` (declared compression ratio cap). The size caps are a UX
guard, not a security boundary.

RAR extraction inherits the new defaults automatically — rar_extract.c
threads `c->limits` through from the engine, so no rar-side change is
required.

### Changed

- `src/zip_extract.c` — `k_default_limits` relaxed:
  - `max_total_bytes`: 1 TiB → **2 TiB**
  - `max_file_bytes`: 256 GiB → **512 GiB**
  - `max_ratio`: 500 → **500** (unchanged)
- `src/zip_extract.c` — `k_large_limits` widened (must stay > default):
  - `max_total_bytes`: 2 TiB → **4 TiB**
  - `max_file_bytes`: 1 TiB → **1 TiB** (unchanged)
  - `max_ratio`: 1000 → **1000** (unchanged)
- `assets/main.js` — `LARGE_FILE_THRESHOLD_BYTES`: 240 GiB → **480 GiB**
- `assets/lang-{en,zh}.js` — `extractLargeAsk` copy updated to reflect
  the new numbers (default 512 GiB / 2 TiB; large 1 TiB / 4 TiB)
- `tests/test_zip_extract.c` — `test_large_profile` advertised-number
  assertion updated: `max_total_bytes == 4 TiB` (was 2 TiB)
- `README.md` — both limit tables (ZIP + RAR), the "Tuning the threshold"
  snippet, and the `err_extract_entry_too_large` FAQ entry bumped to the
  new numbers
- `docs/HANDOVER.md` — `LARGE_FILE_THRESHOLD_BYTES`, the
  `k_default_limits` / `k_large_limits` ASCII diagram, the user-scenario
  description, the 480 GiB popup note, and the RAR limits paragraph
  bumped to the new numbers

### Unchanged

- `src/rar_extract.c` — already threads `c->limits` from the engine,
  picks up the new defaults for free
- `max_ratio` — both profiles unchanged (500 : 1 default / 1000 : 1 large)
- `check_space()` — unchanged; still the real disk-space guard
- `max_entries` — 200 000 default / 500 000 large, unchanged
- `docs/UPGRADE-v1.7-zip-large-file-profile.md` — historical v1.7
  document left as-is so the v1.7 → v1.8.2 evolution is traceable
- Test fixture `medium_bomb.zip` (ratio ≈ 238) still exercises both
  rejection under the default 500 : 1 cap and acceptance under the
  `large=1` 1000 : 1 cap
- 84 host-side checks (70 ZIP + 14 RAR), 0 failures

### Migration notes

- **Forward-compatible** — users with v1.8.1 deployments who never trigger
  `err_extract_entry_too_large` see no difference (defaults are strictly
  more permissive)
- **3A-game single-file archives now extract silently** — no prompt, no
  manual `large=1` API call required for files up to 512 GiB
- **No data loss** — the relaxation only widens accepted archives; the
  real security guards (`check_space`, `max_ratio`, `path traversal`)
  are untouched
- **No frontend UX change for typical use** — only archives > 480 GiB
  on disk now trigger the confirmation prompt (previously 240 GiB)

---

## [v1.8.1] — 2026-09-05

**Hotfix: relaxed default ZIP extraction limits.**

The default `k_default_limits` profile is now **1 TiB total / 256 GiB per
entry / 500 : 1 ratio** (was 512 GiB / 64 GiB / 200 : 1). The frontend
threshold `LARGE_FILE_THRESHOLD_BYTES` is bumped from 60 GiB to 240 GiB
to match. The `large=1` profile (1 TiB / 1 TiB / 1000 : 1) is unchanged.

Why: the previous default was a UX-oriented early-fail guard, not a
security guard — `check_space()` already enforces available ≥ bytes_total
before staging begins, and `max_ratio` already rejects classic zip
bombs. A user with a multi-hundred-GiB system image shouldn't have to
click through a confirmation prompt for what's a perfectly safe archive.
The relaxed default still rejects any archive whose declared
uncompressed total exceeds the destination's free space (real check,
not a declared-vs-fs assertion) and any archive with a declared ratio
above 500 : 1 (real zip-bomb guard).

RAR extraction inherits the new defaults automatically — rar_extract.c
threads `c->limits` through from the engine, so no rar-side change is
required.

### Changed

- `src/zip_extract.c` — `k_default_limits` relaxed:
  - `max_total_bytes`: 512 GiB → **1 TiB**
  - `max_file_bytes`: 64 GiB → **256 GiB**
  - `max_ratio`: 200 → **500**
- `assets/main.js` — `LARGE_FILE_THRESHOLD_BYTES`: 60 GiB → **240 GiB**
- `assets/lang-{en,zh}.js` — `extractLargeAsk` default-profile copy
  updated to reflect the new numbers
- `README.md` — "Stricter default ZIP profile" line, the limit table
  (two locations), and the `err_extract_entry_too_large` FAQ entry
  bumped to the new numbers; "Tuning the threshold" snippet updated to
  240 GiB
- `docs/HANDOVER.md` and `docs/UPGRADE-v1.8-rar-support.md` — the
  few remaining numeric references in those docs updated

### Unchanged

- `src/rar_extract.c` — already threads `c->limits` from the engine,
  picks up the new defaults for free
- `k_large_limits` — `large=1` profile (1 TiB / 1 TiB / 1000 : 1) is
  unchanged
- `docs/UPGRADE-v1.7-zip-large-file-profile.md` — historical v1.7
  document left as-is so the v1.7 → v1.8.1 evolution is traceable
- Test fixture `medium_bomb.zip` (ratio ≈ 238) still exercises both
  rejection under the default 500 : 1 cap and acceptance under the
  `large=1` 1000 : 1 cap
- 83 host-side checks (69 ZIP + 14 RAR), 0 failures

### Migration notes

- **Forward-compatible** — users with v1.7 / v1.8 deployments who never
  trigger `err_extract_entry_too_large` see no difference (defaults are
  strictly more permissive)
- **No data loss** — the relaxation only widens accepted archives; the
  real security guards (`check_space`, `max_ratio`, `path traversal`)
  are untouched
- **No frontend UX change for typical use** — only archives > 240 GiB
  on disk now trigger the confirmation prompt (previously 60 GiB)

---

## [v1.8] — 2026-09-05

**RAR extraction: single-volume RAR4 / RAR5 (unencrypted).**

> ⚠️ Scope clarification — v1.8 ships **single-volume unencrypted RAR** only.
> The original RAR wishlist (multi-volume `.partNN.rar`, encrypted RAR with
> password UI) is **deferred to v1.9**; see `docs/UPGRADE-v1.8-rar-support.md`
> and `third_party/unrar/VENDORED.md` for the rationale and the engine
> upgrade path. ZIP behaviour and the large-file profile are unchanged.

### Added

- **`src/rar_extract.{c,h}`** — a new extraction engine that mirrors
  `zip_extract`'s protocol exactly. Internally it wraps the vendored
  `dmc_unrar` 1.7.0 (`third_party/unrar/dmc_unrar.c`). The public entry point
  is `rar_extract(rar_path, dst_dir, conflict, limits, cancel, progress,
  userdata, result)` — same signatures, same `zipx_status_t` codes, same
  `zipx_result_t`, same `zipx_limits_t` profile lookup. ~1276 LOC
  (`src/rar_extract.c`).
- **`third_party/unrar/`**:
  - `dmc_unrar.c` — vendored verbatim from upstream (11 598 LOC, ~365 KiB).
    GPL-2.0-or-later, attributed in `THIRD_PARTY_NOTICES`.
  - `dmc_unrar_api.h` — **project-authored facade header**. Re-declares only
    the `dmc_unrar_*` symbols `rar_extract.c` actually uses, so the engine
    can `#include "dmc_unrar_api.h"` instead of `#include "dmc_unrar.c"`.
    This keeps the vendored `.c` compiling as its own translation unit and
    avoids polluting dmc_unrar's struct / function names with any
    build-system macros (see `tests/posix_compat.h`'s `wfm_open` /
    `wfm_close` rename pattern — that conflict is what motivated the
    facade). The facade carries the project's license; the library body
    remains unmodified.
  - `COPYING`, `README.md` — upstream GPL notice + readme.
  - `VENDORED.md` — explains why we chose `dmc_unrar` over rarlab UnRAR /
    `opello/unrar`, what is and is not supported, and gives a step-by-step
    upgrade plan for moving to a fuller C++ UnRAR in v1.9.
- **`src/extract.c` dispatch layer** — `extract_dispatch()` picks the
  engine by extension (`.zip` → `zipx_extract`, `.rar` → `rar_extract`,
  anything else → `ZIPX_ERR_UNSUPPORTED`). The case-insensitive suffix
  matcher trims trailing path separators. `extract_worker` now calls the
  dispatch instead of going straight to the ZIP engine.
- **Frontend + i18n wiring**:
  - `assets/main.js` recognises `.rar` (single-volume) and `.part0*1.rar`
    (multi-volume master) as extractable, greys out the extract button on
    `.part02+.rar` sub-volumes with a tooltip "select the main volume
    instead". This UX is only useful because v1.8 still rejects
    multi-volume RAR with a friendly error — the visual feedback stops the
    user from selecting a sub-volume and getting confused.
  - `assets/lang-{en,zh}.js` `err_extract_unsupported` updated to:
    "…(only unencrypted plain ZIP and single-volume RAR are supported)…".
- **Host test suite** (`tests/test_rar_extract.c`, **14 checks**) —
  negative paths only (format dispatch, error translation, limits
  handoff). The suite is wired into `tests/run-tests.sh` alongside the
  existing ZIP suite; fixture generation falls back to a placeholder
  blob when no `rar` / `7z` writer is present, so the negative tests
  fire on any host. Total host checks: **69 ZIP + 14 RAR = 83**.

### Changed

- **`Makefile`**:
  - `VERSION_TAG := v1.8` (was `v1.7`).
  - `THIRD_PARTY_SRCS` adds `third_party/unrar/dmc_unrar.c`.
  - `THIRD_PARTY_CFLAGS` adds `-Ithird_party/unrar` and
    `-DDMC_UNRAR_DISABLE_BE32TOH_BE64TOH=1` (dmc_unrar's own byte-swap
    helpers are avoided so we don't need an extra `byteswap.h` shim on
    the SDK).
- **`src/extract.c` / `src/extract.h`** — no public-API break. The HTTP
  surface (`POST /api/extract`) accepts the same fields as v1.7 plus
  the existing `large=1`; there is **no** `password=` field because
  v1.8 cannot decrypt. (The wire format is forward-compatible — a v1.9
  `password=` field will be additive.)
- **`THIRD_PARTY_NOTICES`** — adds a section `3. dmc_unrar` crediting
  Sven Hesse (DrMcCoy), summarising the GPL-2.0-or-later obligations on
  the resulting binary, and noting that `dmc_unrar_api.h` is
  project-authored and licensed with the project.

### Limitations (v1.8 scope)

- **Multi-volume RAR** (`.part02+.rar`, `.part1+.rar`, numbered
  continuations) is rejected with `ZIPX_ERR_UNSUPPORTED` and the error
  message "extract on a PC first". Upstream dmc_unrar does not chain
  companion volumes by design. When opello/unrar replaces dmc_unrar
  in v1.9 this becomes a one-line error-code drop.
- **Encrypted RAR** (any encrypted header / file flag) is rejected with
  `ZIPX_ERR_UNSUPPORTED`. Same root cause — dmc_unrar omits decryption
  to avoid patent complications. There is **no** password prompt in
  the UI; there is **no** `password=` field in `/api/extract`.
- **Symbolic links, FIFOs, sockets, devices** inside a RAR archive are
  rejected with `ZIPX_ERR_SPECIAL` (mirrors ZIP behaviour).
- **RAR 1.4** (very old) is not supported by dmc_unrar and is rejected
  upstream with `DMC_UNRAR_ARCHIVE_VERSION_UNSUPPORTED`; the wrapper
  maps that to `ZIPX_ERR_UNSUPPORTED`. RAR 1.5 through RAR 5.0 are
  supported.

### Verification

```sh
make                                                       # builds web-file-mgr.elf (in WSL)
ls -la web-file-mgr.elf                                    # record size
sha256sum web-file-mgr.elf                                 # record digest (paste into the v1.8 banner above)
file  web-file-mgr.elf                                     # ELF 64-bit LSB pie, x86-64
od -An -tx1 -N20 web-file-mgr.elf                         # 7f45 4c46 0201 + e_machine 003e

(cd tests && bash run-tests.sh)                            # 69 + 14 = 83 checks, 0 failures

python3 .build/check-elf-gzip.py ./web-file-mgr.elf        # 7/7 v1.7 keys + 1 v1.8 key (err_extract_unsupported)
```

### Technical notes

A long-form technical write-up of this upgrade lives in
[`docs/UPGRADE-v1.8-rar-support.md`](./docs/UPGRADE-v1.8-rar-support.md).
The vendoring decision tree (and the v1.9 plan) is in
[`third_party/unrar/VENDORED.md`](./third_party/unrar/VENDORED.md).

### Credits

Same as v1.7 — see [README.md → Credits](./README.md#credits). dmc_unrar
is credited in [`THIRD_PARTY_NOTICES`](./THIRD_PARTY_NOTICES).

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
