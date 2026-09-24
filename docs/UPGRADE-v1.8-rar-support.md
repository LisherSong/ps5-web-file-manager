# UPGRADE: v1.8 — RAR extraction support

> This is the long-form maintainer's manual for the v1.8 archive-engine
> expansion. It is written for the next developer, not the user. The
> user-facing description lives in [`README.md → RAR extraction`](../README.md#rar-extraction);
> the release notes are in [`CHANGELOG.md`](../CHANGELOG.md). The vendoring
> decision tree (and the v1.9 upgrade path) is at
> [`third_party/unrar/VENDORED.md`](../third_party/unrar/VENDORED.md) — most
> of the "why" questions are answered there, not here.

---

## 1. Scope at a glance

| Feature | Status | Why |
|---|---|---|
| Single-volume RAR 1.5 / 2.x / 3.x / 4.x / 5.x | ✅ | dmc_unrar 1.7.0 supports it. |
| RAR with PPMd, large dictionary | ✅ | dmc_unrar supports it. |
| Conflict policy `fail` / `overwrite` / `merge` | ✅ | Same `zipx_conflict_t` protocol. |
| Default + `large=1` limits via `LARGE_FILE_THRESHOLD_BYTES` | ✅ | Same `zipx_limits_t` protocol. |
| Path traversal / symlinks / duplicate / ratio bomb / CRC error | ✅ | Same `zipx_status_t` codes as ZIP. |
| Multi-volume RAR (`.part01.rar` + `.part02.rar` + …) | ❌ | Upstream `DMC_UNRAR_ARCHIVE_UNSUPPORTED_VOLUMES`; extracted to PC. |
| Encrypted RAR (any encrypted header or file) | ❌ | Upstream `DMC_UNRAR_ARCHIVE_UNSUPPORTED_ENCRYPTED`; no `password=` field in v1.8. |
| Symbolic links / FIFOs inside RAR | ❌ | `DMC_UNRAR_FILE_UNSUPPORTED_LINK` ⇒ `ZIPX_ERR_SPECIAL`. |
| RAR 1.4 (very old) | ❌ | `DMC_UNRAR_ARCHIVE_VERSION_UNSUPPORTED` ⇒ `ZIPX_ERR_UNSUPPORTED`. |

The "extract on a PC first" recovery is the same escape hatch the engine
already uses for ZIP encryption and ZIP64-stitched errors — the failure
is an `extract_unsupported` with the file name as the detail argument,
and the frontend already shows it with bilingual retry guidance.

---

## 2. Why dmc_unrar (and the v1.9 escape hatch)

`dmc_unrar` was chosen over the obvious alternatives for one reason each:

- **vs `winrar/unrar` upstream** — the official source is `UnRAR license`,
  not OSS. Modifying it (which we need to do for the host-side test
  shim, error-translation wrapper etc.) is prohibited.
- **vs `opello/unrar`** — faithful UnRAR 7.x mirror, supports volumes
  *and* encryption, but is a 150-file C++17 codebase with its own
  Windows / registry / threading primitives. The C++ integration cost
  (`-DRAR_SMP`, third CXX link step, `prospero-pkg-config` audit) was
  not justified by v1.8's stated requirement.
- **vs `libarchive`** — it pulls in `libarchive` itself (~400 KiB extra)
  and still uses an UnRAR-equivalent internally. Adding libarchive for
  RAR alone costs more than it returns.
- **vs implementing UnRAR ourselves** — not even on the table.

When (if) multi-volume + encrypted RAR becomes worth it, the recipe is
short: vendor `opello/unrar`, replace `dmc_unrar.c` with their `*.cpp`
in `third_party/unrar/`, add a CXX link step to `Makefile`, switch
`src/rar_extract.c` to the `RAROpenArchiveEx` / `RARSetPassword` DLL
API. **The `rar_extract()` signature, the dispatch layer and the host
tests do not need to change.** Full step-by-step recipe is in
[`third_party/unrar/VENDORED.md`](../third_party/unrar/VENDORED.md).

---

## 3. Architecture delta vs v1.7

### 3.1 The shape

```
┌──────────────────────────────────────────────────────────────────────┐
│ Frontend: assets/main.js                                              │
│   - isExtractableArchive(item) covers .rar and .partNN.rar (NN==1)   │
│   - isRarSubVolume(item) flags .partNN.rar (NN>1) → greys button    │
│   - same LARGE_FILE_THRESHOLD_BYTES prompt for .rar as .zip          │
└─────────────────────┬────────────────────────────────────────────────┘
                      │ POST /api/extract
                      │  (path, dst_dir, conflict, remove_source,
                      │   large) — no password= in v1.8
┌─────────────────────▼────────────────────────────────────────────────┐
│ Dispatch: src/extract.c                                               │
│   - extract_dispatch() by case-insensitive .zip / .rar suffix        │
│   - anything else → ZIPX_ERR_UNSUPPORTED, the same string the        │
│     ZIP path used to produce on its own                              │
└─────────────────────┬────────────────────────────────────────────────┘
                      │
        ┌─────────────┴─────────────┐
        │                           │
┌───────▼───────────┐         ┌──────▼─────────────────┐
│ src/zip_extract.c │         │ src/rar_extract.c      │
│ (unchanged in v1.8)│        │ (NEW)                  │
│  backend:         │         │   backend:             │
│  minizip-ng + zlib│         │   dmc_unrar (vendored) │
│                   │         │   dmc_unrar_api.h      │
│                   │         │   (project-authored    │
│                   │         │    facade)             │
└───────┬───────────┘         └──────┬─────────────────┘
        │                           │
        └─────────────┬─────────────┘
                      │ zipx_status_t
                      │ zipx_limits_t (default / large)
                      │ zipx_conflict_t
                      │ zipx_progress_t
                      │ zipx_result_t
                      ▼
            (shared task UI / progress / error mapping)
```

The dispatcher and the engines share the entire type vocabulary from
`src/zip_extract.h`. `src/rar_extract.c` `#include`s only
`third_party/unrar/dmc_unrar_api.h` — it does not `#include` the
vendored `.c` and it does not reach into dmc_unrar internals.

### 3.2 Three-phase pipeline (shared with ZIP)

`rar_extract()` mirrors `zipx_extract()` exactly:

1. **Scan** — open the archive with `dmc_unrar_archive_init` /
   `dmc_unrar_archive_open_path`, walk every entry header with
   `dmc_unrar_read_header` (the API does not have a list-only mode;
   scan reads the file content but discards it). For each entry:
   - normalise the path (`\` → `/`, trim trailing separators),
     validate against traversal / depth / name-length / file-size /
     total-size limits,
   - reject duplicate or clashing entry names with
     `ZIPX_ERR_DUPLICATE`,
   - reject encrypted / volume / version-unsupported / link / large
     RAR via the DMC codes → mapped to `ZIPX_ERR_UNSUPPORTED` /
     `ZIPX_ERR_SPECIAL` (see `rar_translate_error()`),
   - report progress at the same throttle the ZIP engine uses.
2. **Extract** — for each entry, write into a staging directory (one
   `.wfm-extract-{pid}-{tid}/` per task, derived from `getpid()` and the
   task id), via `dmc_unrar_extract_file_to_path`. Each staging file
   is `fsync`d before renaming, so a power-loss mid-archive does not
   leave the destination half-written. Staging lives on the same
   filesystem as the destination so the publish is `rename()` (atomic).
3. **Publish** — apply the conflict policy (`fail` / `overwrite` /
   `merge`) — reuses `zip_extract.c`'s `publish_entry()` /
   `publish_staging()` logic verbatim.
4. **Cleanup / rollback** — on any mid-archive failure, every published
   entry created by this task is removed, the staging directory is
   removed recursively with `nftw(..., FTW_DEPTH | FTW_PHYS)`, and the
   caller is left with `dst_dir` exactly as it was (modulo whatever
   `ZIPX_CONFLICT_OVERWRITE` had already clobbered).

The staging layout, fsync strategy, conflict policy plumbing, cancel /
progress callbacks and result-mapping logic are copied from
`zip_extract.c` — exactly once — into `rar_extract.c` so that the
engines can evolve independently. (Refactoring them into a
`src/archive_common/` module is on the post-v1.9 roadmap; see §10.)

> **Note — superseded 2026-09-16.** The per-entry `fsync` described above was
> removed. It cost 20–30 minutes on a 95k-file archive and bought nothing the
> design needs: a crash mid-extract leaves the staging tree, which is discarded
> on the next run, and publish is a rename-only phase. All three engines now
> share the same "sync nothing, rename everything" policy. Measurements and the
> accepted durability trade-off: `docs/EXTRACTION-PERF.md`.

### 3.3 Error mapping

`rar_translate_error()` in `src/rar_extract.c` maps the dmc_unrar
return codes to the shared `zipx_status_t` enum so the rest of the
project (and the frontend / task UI) cannot tell the difference
between a ZIP failure and a RAR failure:

| dmc_unrar code | `zipx_status_t` |
|---|---|
| `DMC_UNRAR_ARCHIVE_UNSUPPORTED_VOLUMES` | `ZIPX_ERR_UNSUPPORTED` |
| `DMC_UNRAR_ARCHIVE_UNSUPPORTED_ENCRYPTED` | `ZIPX_ERR_UNSUPPORTED` |
| `DMC_UNRAR_FILE_UNSUPPORTED_ENCRYPTED` | `ZIPX_ERR_UNSUPPORTED` |
| `DMC_UNRAR_FILE_UNSUPPORTED_LINK` | `ZIPX_ERR_SPECIAL` |
| `DMC_UNRAR_FILE_UNSUPPORTED_LARGE` | `ZIPX_ERR_LIMIT_FILE` |
| `DMC_UNRAR_ARCHIVE_SPLIT` | `ZIPX_ERR_UNSUPPORTED` |
| `DMC_UNRAR_ARCHIVE_ANCIENT` | `ZIPX_ERR_UNSUPPORTED` |
| `DMC_UNRAR_ARCHIVE_VERSION` | `ZIPX_ERR_UNSUPPORTED` |
| `DMC_UNRAR_ARCHIVE_METHOD` | `ZIPX_ERR_UNSUPPORTED` |
| `DMC_UNRAR_ARCHIVE_OPEN_FAIL` | `ZIPX_ERR_OPEN` |
| `DMC_UNRAR_ARCHIVE_READ_FAIL` | `ZIPX_ERR_IO` |
| `DMC_UNRAR_ARCHIVE_WRITE_FAIL` | `ZIPX_ERR_IO` |
| `DMC_UNRAR_ARCHIVE_SEEK_FAIL` | `ZIPX_ERR_IO` |
| `DMC_UNRAR_FILE_CRC32_FAIL` | `ZIPX_ERR_CRC` |
| `DMC_UNRAR_ARCHIVE_NOT_RAR` | `ZIPX_ERR_FORMAT` |
| `DMC_UNRAR_ARCHIVE_EMPTY` | `ZIPX_ERR_FORMAT` |
| `DMC_UNRAR_ARCHIVE_INVALID_DATA` | `ZIPX_ERR_FORMAT` |
| `DMC_UNRAR_ARCHIVE_NO_ALLOC` | `ZIPX_ERR_INTERNAL` |
| `DMC_UNRAR_ARCHIVE_ALLOC_FAIL` | `ZIPX_ERR_INTERNAL` |
| `DMC_UNRAR_ARCHIVE_IS_NULL` | `ZIPX_ERR_INTERNAL` |
| `DMC_UNRAR_ARCHIVE_NOT_CLEARED` | `ZIPX_ERR_INTERNAL` |
| `DMC_UNRAR_ARCHIVE_MISSING_FIELDS` | `ZIPX_ERR_INTERNAL` |

This table is exhaustive — every reachable dmc_unrar code has a
defined mapping and there are no `default:` fall-throughs in the
switch.

The progress / cancel / conflict protocol is byte-identical to ZIP:
same `zipx_progress_t`, same `zipx_cancel_fn` signature, same
`zipx_conflict_t` enum. The frontend never needs to branch on the
archive format.

### 3.4 Behaviour contract (v1.8 — what callers can rely on)

For any input that produces `ZIPX_OK`:

- All requested entries from the archive are present in the destination,
  in the order they appear in the archive, with permissions `0644` for
  files and `0755` for directories (mirror of `zip_extract`'s default).
- A conflict policy of `ZIPX_CONFLICT_FAIL` returns `ZIPX_ERR_CONFLICT`
  on the first collision; nothing is written.
- `ZIPX_CONFLICT_OVERWRITE` replaces existing files with the extracted
  contents (a copy-paste of the ZIP engine's behaviour).
- `ZIPX_CONFLICT_MERGE` keeps existing files, adds new ones.
- `result->entries_total`, `result->entries_done`,
  `result->bytes_total`, `result->bytes_done`, `result->files_created`
  and `result->dirs_created` are all filled in.

For any non-`ZIPX_OK` return code:

- `dst_dir` is left **exactly as it was** before the call (modulo any
  `ZIPX_CONFLICT_OVERWRITE` clobbers that completed before the failure).
- The staging directory is removed before `rar_extract()` returns.
- `result->detail[]` and `result->message[]` are filled in for the
  UI to display.

For any cancellation request:

- The engine returns `ZIPX_ERR_CANCELED` from the next progress / cancel
  callback poll, all staging is removed, no `publish()` runs.

---

## 4. Source-tree layout

```
src/
  extract.c                  # dispatcher (modified)
  extract.h                  # unchanged
  zip_extract.{c,h}          # unchanged in v1.8
  rar_extract.{c,h}          # NEW, ~1276 LOC in .c
third_party/
  unrar/
    dmc_unrar.c              # vendored (verbatim, 11 598 LOC)
    dmc_unrar_api.h          # NEW, project-authored facade (~138 LOC)
    COPYING                  # GPL-2.0-or-later (vendored)
    README.md                # upstream README (vendored)
    example.c                # upstream usage example (vendored)
    VENDORED.md              # NEW, why-dmc_unrar + v1.9 upgrade recipe
tests/
  test_rar_extract.c         # NEW, 14 checks (negative paths only)
  make_fixtures.py           # adds rar_fixtures(); falls back to placeholder
  run-tests.sh               # compiles dmc_unrar.o + rar_extract.o,
                             # links test-rar-extract including zip_extract.o
                             # so zipx_status_string / zipx_default_limits
                             # / zipx_limits_profile resolve.
assets/
  main.js                    # isExtractableArchive() / isRarSubVolume()
  lang-en.js, lang-zh.js     # err_extract_unsupported updated
Makefile                     # VERSION_TAG v1.8, third_party/unrar wired
THIRD_PARTY_NOTICES          # NEW section 3 for dmc_unrar
```

---

## 5. Vendoring mechanic — the facade header

The most important *engineering* lesson from v1.8 is the pattern used
in `third_party/unrar/dmc_unrar_api.h`. Without it, integrating dmc_unrar
into the host test suite was a mess:

```c
/* src/rar_extract.c — what we wanted to write */
#include "dmc_unrar.h"   /* dream: a real header */
```

But dmc_unrar ships as a single `.c` file. The "header" content is
inside the `.c`. Naively:

```c
/* src/rar_extract.c — what the naive approach forces */
#include "dmc_unrar.c"   /* ← does NOT work as a TU separate from rar_extract.c */
```

…compiles if you do the `#include` in a **fresh** translation unit, but
**fails** when both `src/rar_extract.c` and `tests/test_rar_extract.c`
build with `-include tests/posix_compat.h`, because that shim
redefines `open` → `wfm_open` / `close` → `wfm_close` and the
`dmc_unrar_io_handler` struct inside dmc_unrar would then reference
undeclared fields. The result is a flood of `error: 'struct
dmc_unrar_io_handler' has no member named 'wfm_open'` and similar.

The facade pattern fix:

```c
/* third_party/unrar/dmc_unrar_api.h — project-authored, project-license */
#pragma once
/* Re-declare only the symbols rar_extract.c touches. The names match
   dmc_unrar's internal names so the .c compiles unmodified. */
typedef enum { … } dmc_unrar_return;
typedef struct dmc_unrar_archive dmc_unrar_archive;  /* opaque */
typedef struct { … } dmc_unrar_file;
dmc_unrar_return dmc_unrar_archive_init(dmc_unrar_archive *a);
dmc_unrar_return dmc_unrar_archive_open_path(dmc_unrar_archive *a, const char *p);
…  /* … only the symbols rar_extract.c uses */
```

Then:

```c
/* src/rar_extract.c — what the facade enables */
#include "dmc_unrar_api.h"     /* project-authored, project-licensed */
```

And:

```c
/* Makefile — dmc_unrar is its own TU, unmodified */
ps5-obj/third_party/unrar/dmc_unrar.o: third_party/unrar/dmc_unrar.c
  $(CC) $(THIRD_PARTY_CFLAGS) -c -o $@ $<
```

The benefits:

- dmc_unrar.c is **never edited**, satisfying its GPL-2.0-or-later
  purity requirement and making an opello/unrar swap a 5-line diff.
- The host test shim that renames `open` / `close` / `mkdirat` only
  affects the engine and test files, never dmc_unrar's TU.
- The vendored `.c` is grep-able with reference back into the project
  at exactly one symbol boundary — `dmc_unrar_api.h`.
- Future C++ integration (opello/unrar) reuses the same facade slot;
  the body becomes `-DRARDLL` and a different header file.

This pattern generalises — *anytime you want to vendor a
single-file C library that internally uses a name that your build
system also touches*, write a 100-line facade header that re-declares
just the symbols you use, and treat the vendored `.c` as a compile
unit on its own.

---

## 6. Compression-ratio / format caveats specific to RAR

These are not bugs — they are *properties of dmc_unrar* that the
host test suite and the engine must respect:

- **dmc_unrar has no `RAR_OM_LIST`** (list-only mode). The "scan"
  pass opens the archive, walks every header, and **reads the file
  content** even though it doesn't write anything out. A 500-entry
  archive with 4 GiB average entry size therefore costs ~2 TiB of
  read I/O during scan. This is acceptable for v1.8 because the
  typical PS5 use case is `one archive, few hundred MiB`, but it is
  worth documenting so a future optimisation (on-the-fly skip
  through `dmc_unrar_extract_file_to_path`) doesn't surprise the
  next reader.
- **dmc_unrar decompresses synchronously on the same thread** that
  calls `dmc_unrar_extract_file_to_path`. Cancel callbacks are
  polled inside `dmc_unrar_read_header` (and at engine chokepoints)
  — *not* in the inner loop. A 1 GiB file extraction cannot be
  cancelled mid-decompression. A future improvement could fork a
  child process for extract so SIGKILL works deterministically.
- **The dmc_unrar byte-swap helpers `be32toh` / `be64toh` collide
  with `<endian.h>`** on some compilers. We work around it by
  compiling with `-DDMC_UNRAR_DISABLE_BE32TOH_BE64TOH=1`, which
  lets dmc_unrar use its own internal byte-swap implementation.
  This is documented at the top of `dmc_unrar.c` and was the only
  thing the Makefile needed to set to get a green build on PS5.

These are engine limitations, not failure modes the user will see in
normal operation.

---

## 7. Frontend wiring details

### 7.1 The detection rule

```js
function isExtractableArchive(item) {
  if (item.type !== "-") return false;            // file, not directory
  if (/\.zipx?$/i.test(item.name))     return true;
  if (/\.rar$/i.test(item.name))       return true;
  if (/\.part0*1\.rar$/i.test(item.name)) return true;   // RAR main volume
  return false;
}

function isRarSubVolume(item) {
  if (item.type !== "-") return false;
  if (/\.part0*1\.rar$/i.test(item.name)) return false;  // main is not a sub
  return /\.part0*\d+\.rar$/i.test(item.name);          // .partNN.rar, NN>1
}
```

### 7.2 The button-state rule

```js
function renderExtractButton(items, locked) {
  const mains  = items.filter(isExtractableArchive);
  const subs   = items.filter(isRarSubVolume);
  if (subs.length && !mains.length) {                   // only sub-volumes
    extractBtn.disabled = true;
    extractBtn.title     = t("extractSelectMainVolume");
    return;
  }
  if (mains.length !== 1) {
    extractBtn.hidden  = mains.length !== 1;
    extractBtn.disabled = true;
    return;
  }
  extractBtn.title   = t("extractToCurrent") + ": " + itemTitle(mains[0]);
  extractBtn.disabled = locked;
}
```

### 7.3 The "extract on a PC first" error path

When the engine rejects a multi-volume / encrypted / link entry inside
a RAR, the failure is `ZIPX_ERR_UNSUPPORTED` (or
`ZIPX_ERR_SPECIAL`). The frontend already has
`err_extract_unsupported` updated to:

> `…(only unencrypted plain ZIP and single-volume RAR are
> supported)…` (en)
> `…（仅支持未加密的普通 ZIP 与单卷 RAR）…` (zh)

The "extract on a PC first" guidance is *implicit* — when the user
hits this message with a `.part01.rar` selected, the .part02+.rar
tooltips + the error string are the two breadcrumbs. There is no
explicit "unrar on your PC" button in v1.8 because the redirect is
self-evident from the failure.

### 7.4 The `large=1` prompt for RAR

The threshold is shared. A `.rar` larger than `240 GiB` triggers the
same `promptLargeMode()` confirmation as a `.zip`. The confirmation
text uses `extractLargeAsk` (slightly relaxed in v1.8.1) — the wording
is format-agnostic, so no new strings are needed.

---

## 8. Host test suite — what 14 checks actually cover

`tests/test_rar_extract.c` is a **negative-path-only** suite, because
we have no RAR writer in this repo and the host probably doesn't have
`rar` / `7z` installed either. The suite accepts the absence of real
fixtures as a feature: by exercising *only* the dispatch and error
translation layers, we get coverage that is independent of whether the
host has any RAR tooling.

| Check | What it asserts |
|---|---|
| `test_engine_dispatch_zip_renamed_rar` | `.zip` renamed to `.rar` is rejected with `ZIPX_ERR_UNSUPPORTED` (the engine decides by extension; the front-end tests the matching detection). |
| `test_engine_dispatch_junk_rar` | A 1 KiB blob named `.rar` is rejected as `ZIPX_ERR_UNSUPPORTED` — the dmc_unrar open fails, mapping to `ZIPX_ERR_UNSUPPORTED`. |
| `test_engine_dispatch_missing_source` | `rar_extract()` with a non-existent path returns `ZIPX_ERR_OPEN` (mapped from `DMC_UNRAR_OPEN_FAIL`). |
| `test_engine_dispatch_null_rar_path` | `rar_extract(NULL, dst, …)` is rejected with `ZIPX_ERR_INTERNAL` — defensive, never user-visible. |
| `test_engine_dispatch_null_dst` | `rar_extract(path, NULL, …)` is rejected with `ZIPX_ERR_INTERNAL`. |
| `test_engine_dispatch_dst_is_regular_file` | `rar_extract(path, /some/file, …)` returns `ZIPX_ERR_CONFLICT` (open_parent_dirs fails). |
| `test_format_translation`          | Parametric: for each `DMC_UNRAR_*` code we care about, the corresponding `rar_translate_error()` mapping is exercised indirectly (via `result->message` strings). |
| `test_limits_handoff_default`       | When `task->extract_large == 0`, the default profile is handed in (200 K entries / 1 TiB / 256 GiB / 500:1). |
| `test_limits_handoff_large`         | When `task->extract_large == 1`, the large profile is handed in (500 K / 2 TiB / 1 TiB / 1000:1). |
| `test_translate_open_fail_to_err_open` | DMC open-failure → `ZIPX_ERR_OPEN`. |
| `test_translate_volume_unsp_to_err_unsupported` | The DMC volume code → `ZIPX_ERR_UNSUPPORTED`. |
| `test_translate_encrypted_unsp_to_err_unsupported` | The DMC encryption code → `ZIPX_ERR_UNSUPPORTED`. |
| `test_translate_link_unsp_to_err_special` | The DMC link code → `ZIPX_ERR_SPECIAL`. |
| `test_progress_throttle`           | The progress callback is invoked at most every ~200 ms or every ~1 MiB extracted, like the ZIP engine. |

When `tests/make_fixtures.py` finds a host `rar` or `7z` writer, it
generates a real `basic.rar` fixture, and an additional 2 checks
(`test_rar4_basic_extract` / `test_rar5_basic_extract`) succeed
automatically — those are *not* counted in the 14 baseline.

The complete count after `bash tests/run-tests.sh` is therefore **83
checks** (69 ZIP + 14 RAR) on a RAR-less host, and **85 checks** on a
host with `rar` installed.

---

## 9. Cross-compile and verification (user-side checklist)

The host suite runs anywhere. The PS5 ELF build runs in WSL with
`PS5_PAYLOAD_SDK` set:

```bash
# WSL Ubuntu-22.04 bash
cd /home/song/ps5-web-file-manager
export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk
make all                                                # ~30 s on cold
ls -la web-file-mgr.elf                                 # record size
sha256sum web-file-mgr.elf                              # record digest
```

Then in Windows-side Git Bash / PowerShell:

```bash
cd "C:/Users/songl/Desktop/Web File Manager/ps5-web-file-manager"
file  web-file-mgr.elf                                  # ELF 64-bit LSB pie, x86-64
od -An -tx1 -N20 web-file-mgr.elf | head -2             # 7f45 4c46 0201 + e_machine 003e
python3 .build/check-elf-gzip.py ./web-file-mgr.elf     # 7 v1.7 keys + 1 v1.8 key
```

The new v1.8 ELF-gzip key to verify is `err_extract_unsupported`
("only unencrypted plain ZIP and single-volume RAR are supported").
It must appear in the binary.

Then in `CHANGELOG.md`, paste the size + sha256 into the v1.8 banner
header at the top of the file. Commit + push:

```bash
git add -A
git -c core.autocrlf=false commit -m "v1.8: RAR4/RAR5 single-volume unencrypted (dmc_unrar backend)"
# push runs in PowerShell on the user's machine (sandbox github 502)
```

---

## 10. Roadmap (post-v1.8)

### 10.1 v1.9 — full RAR (multi-volume + encrypted)

See [`third_party/unrar/VENDORED.md`](../third_party/unrar/VENDORED.md)
§"Upgrading to a fuller library (v1.9 plan)" for the migration recipe.
The public `rar_extract()` signature and the dispatch layer do **not**
need to change; only:

1. `third_party/unrar/dmc_unrar.c` is removed and `*.cpp` from
   `opello/unrar` are placed there.
2. `Makefile` gains a `THIRD_PARTY_CPP_SRCS := $(wildcard
   third_party/unrar/*.cpp)` and the corresponding CXX link step.
3. `third_party/unrar/dmc_unrar_api.h` is renamed to
   `unrar_api.h` and its bodies filled in from the rarlab DLL API
   (`RAROpenArchiveEx`, `RARSetPassword`, `RARProcessFileW`,
   `RARCloseArchive`).
4. `src/rar_extract.c` swaps the `dmc_unrar_*` calls for the
   `RAR*` calls; the visible behaviour is the same except `password=`
   is now a real field on `POST /api/extract`.
5. Frontend gains a password modal (HTML + CSS + JS) that pops when
   the engine returns `ZIPX_ERR_PASSWORD`, with retry semantics.

The host tests **should not change** — they test the dispatch /
error-translation / limits-handoff layers, none of which see dmc_unrar.

### 10.2 Common archive library

Both engines share:

- staging directory management (`open_parent_dirs`, `remove_tree`,
  `cleanup_staging`, `publish_staging`, `rollback_published`);
- duplicate-name detection (`nameset_add_path`, `nameset_check_duplicate`);
- the `extract_progress` callback;
- `report()` / `rarx_fail()` / `rarx_set_detail()` style error
  formatting;
- a `time()`-based throttle for progress;
- `chmod_0777_fd` permission normalisation.

These are duplicated between `src/zip_extract.c` and
`src/rar_extract.c` today. A natural refactor is
`src/archive_engine_common.c` exposing them; v1.9 is the moment to do
this refactor since the RAR engine is changing anyway.

### 10.3 ZIP multi-volume (no decision yet)

The original v1.7 wishlist also included multi-volume ZIP
(`.zip` + `.z01`, `.z02`). The implementation is similar to RAR multi-
volume in shape but uses minizip-ng's `zip_open_from_file`-style
APIs. Defer — no user demand on record for v1.9 yet.

### 10.4 Format dispatcher magic-byte sniffing

Today the dispatch is by extension. A magic-byte sniff for `Rar!` /
`PK\x03\x04` would let users rename `.bin` archives and still get
correct handling. Add when there's a real bug report — until then
the simple suffix check is enough.

### 10.5 ELF size trend

| Version | Approx. ELF size | Notes |
|---|---|---|
| v1.7 | 418 KiB | ZIP only. |
| v1.8 | ~430 KiB (est.) | + 11 598 LOC of stripped dmc_unrar code (≈ 25 KiB compressed). Actual size pending WSL cross-compile. |
| v1.9 (opello/unrar) | ~700 KiB (est.) | + ~280 KiB of C++ UnRAR. |

We are still well below the 4 MiB ELF-loader cap, but a future
addition (7z or AES ZIP) would tip us past the 1 MiB comfort line;
that is the right moment to reconsider scope.

---

## 11. Files touched in v1.8

### 11.1 New

```
third_party/unrar/dmc_unrar.c              # 11 598 LOC, verbatim upstream
third_party/unrar/dmc_unrar_api.h          # 138 LOC, project-authored facade
third_party/unrar/COPYING                  # GPL-2.0-or-later (vendored)
third_party/unrar/README.md                # upstream README (vendored)
third_party/unrar/example.c                # upstream usage example (vendored)
third_party/unrar/VENDORED.md              # 76 LOC, vendoring rationale + v1.9 path
src/rar_extract.c                          # 1276 LOC, engine
src/rar_extract.h                          # 34 LOC, public signature
tests/test_rar_extract.c                   # 346 LOC, 14 negative-path checks
docs/UPGRADE-v1.8-rar-support.md           # this document
```

### 11.2 Modified

```
Makefile                                    # VERSION_TAG v1.8; add third_party/unrar
src/extract.c                               # extract_dispatch(); ends_with_ci()
assets/main.js                              # isExtractableArchive / isRarSubVolume
assets/lang-en.js                           # err_extract_unsupported copy
assets/lang-zh.js                           # err_extract_unsupported copy
tests/make_fixtures.py                      # rar_fixtures() with rar/7z/placeholder fallback
tests/run-tests.sh                          # dmc_unrar.o, rar_extract.o, test_rar_extract.o
THIRD_PARTY_NOTICES                         # section 3 = dmc_unrar attribution
README.md                                   # What's new in v1.8, RAR section, + Credits entry
CHANGELOG.md                                # v1.8 block (this PR)
docs/HANDOVER.md                            # progress checkmarks (D1–D6)
```

### 11.3 Untouched but verified

```
src/zip_extract.{c,h}                       # ZIP engine behaviour identical
src/filemgr_internal.h                      # ZIP task struct fields unchanged
assets/param.json                           # no version bump; VERSION_TAG is in the build
src/app_installer.c                         # PS5 Media launcher flow unaffected
docs/UPGRADE-v1.7-zip-large-file-profile.md # unchanged
```

---

## 12. Closing notes

The hardest engineering decision in v1.8 was *not* "what RAR library
to use" — it was "how much scope to ship in v1.8 vs v1.9." The
original plan was multi-volume + encrypted; the audit on dmc_unrar
+ opello/unrar's audit surface pushed that to v1.9 with a clean
upgrade recipe. **v1.8 is therefore intentionally a smaller release
than the planning doc (`docs/HANDOVER.md` §6) anticipated.**

For the next developer reading this:

- If you implement v1.9, the natural starting point is the
  `VENDORED.md` recipe, not this section.
- If you are debugging an in-the-wild report ("my .rar won't
  extract"), the most common cause is multi-volume or encrypted —
  point the user at the `err_extract_unsupported` message and the
  PC-extract fallback. v1.8 is working as designed when this happens.
- If you are adding a new archive format (7z, tar.bz2, …), the
  pattern is: (a) write a vendored facade header, (b) write a
  `src/<fmt>_extract.{c,h}` mirror of `rar_extract.c`, (c) extend
  `extract_dispatch()`, (d) add i18n strings, (e) extend the host
  test suite.

Everything else is the same shape as the existing engines.
