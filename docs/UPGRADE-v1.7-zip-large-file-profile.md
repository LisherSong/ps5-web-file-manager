# Upgrade v1.7 — ZIP large-file profile

> Technical notes for the **v1.7** upgrade of `ps5-web-file-manager`.
> Audience: future maintainers, code reviewers, contributors reading the
> `git log` of this branch. Pair with `README.md` for the user-facing
> overview and `CHANGELOG.md` for the release-note summary.

---

## 1. Background

Before v1.7 the ZIP engine shipped with a single, conservative limits profile
(`zipx_default_limits()`). It rejected any archive whose uncompressed content
exceeded **512 GiB total** or contained an entry above **64 GiB**, or whose
compression ratio exceeded **200 : 1**. This was the right default for the
"copy game-dump folders" use case but blocked legitimate large payloads — most
notably system images and 200 GB+ backups.

The user asked: *"单个压缩包不可以为 200GB 以上么"* ("Can't a single archive
be larger than 200 GB?"). After surfacing three options (raise the default
limits, add an opt-in profile, or document-only) the choice was **🅱 — add an
opt-in "large-file profile"**. The implementation brief was:

> The server must **never** activate the relaxed caps on its own. The user
> must explicitly opt in, both via the HTTP API and via the UI.

## 2. Architecture delta (one-line summary)

The ZIP engine becomes a **profile-lookup** engine. A new profile table
(`k_large_limits`) and a switch (`zipx_limits_profile()`) sit between the HTTP
layer and the existing `zipx_extract()`. Everywhere else — task model, HTTP
handler, frontend — gains a single boolean that threads through to the engine.

```
HTTP /api/extract?large=1       frontend (confirm() 弹窗)
   │                              │
   └──────► form parser ──► file_task_t.extract_large
                                    │
                                    ▼
                              zipx_limits_profile(task->extract_large)
                                    │
                       ┌────────────┴────────────┐
                       ▼                          ▼
            k_default_limits                k_large_limits
              (200K / 512GiB /                (500K / 2TiB /
               64GiB / 200:1)                 1TiB / 1000:1)
                       │                          │
                       └────────────┬─────────────┘
                                    ▼
                                zipx_extract()
                          (signature unchanged;
                           shared three-phase engine)
```

The public signature of `zipx_extract()` is **unchanged**. Backwards
compatibility is deliberate — anything linking against `src/zip_extract.{c,h}`
continues to compile without changes.

## 3. Engine layer (`src/zip_extract.h`, `src/zip_extract.c`)

### 3.1 New constants and API

```c
/* Pre-built limit profiles. Use zipx_limits_profile() to look one up.
   ZIPX_LIMITS_DEFAULT is the safe profile shipped by zipx_default_limits().
   ZIPX_LIMITS_LARGE allows archives up to 2 TiB total / 1 TiB per file and
   a 1000:1 compression ratio. The caller is responsible for verifying that
   the PS5 has enough free disk space. */
#define ZIPX_LIMITS_DEFAULT 0
#define ZIPX_LIMITS_LARGE   1

const zipx_limits_t *zipx_limits_profile(int profile);
```

### 3.2 The two profiles

```c
static const zipx_limits_t k_default_limits = {
  .max_entries      = 200000,
  .max_total_bytes  = 512ULL * 1024 * 1024 * 1024,   /* 512 GiB */
  .max_file_bytes   = 64ULL  * 1024 * 1024 * 1024,   /*  64 GiB */
  .max_ratio        = 200,
  .max_depth        = 32,
  .max_name_len     = 255,
  .max_path_len     = 1024
};

static const zipx_limits_t k_large_limits = {
  .max_entries      = 500000,
  .max_total_bytes  = 2ULL  * 1024 * 1024 * 1024 * 1024, /* 2 TiB */
  .max_file_bytes   = 1ULL  * 1024 * 1024 * 1024 * 1024, /* 1 TiB */
  .max_ratio        = 1000,
  .max_depth        = 32,
  .max_name_len     = 255,
  .max_path_len     = 1024
};
```

The two profiles are otherwise identical on `max_depth`, `max_name_len` and
`max_path_len` — the v1.7 upgrade only relaxes the four "blast radius" caps.

### 3.3 Profile lookup

```c
const zipx_limits_t *
zipx_limits_profile(int profile) {
  switch(profile) {
  case ZIPX_LIMITS_LARGE:   return &k_large_limits;
  case ZIPX_LIMITS_DEFAULT:
  default:                  return &k_default_limits;
  }
}
```

`zipx_default_limits()` continues to return `&k_default_limits` — there is
**no behavioural change** for callers that did not opt in.

### 3.4 Behavioural contract (unchanged from pre-v1.7)

* Plain ZIPs only — stored / deflated / ZIP64. `ZIPX_ERR_UNSUPPORTED` is
  raised for any encryption flag, multi-volume markers or unsupported
  compression methods.
* Three-phase work model: `ZIPX_PHASE_SCAN → EXTRACT → PUBLISH → CLEANUP`.
  Each entry is first written to a staging directory (`*.wfm-part-*`),
  `fsync()`'d, then atomically renamed into place. A failure mid-archive
  rolls back partial changes.
  *(Superseded 2026-09-16: the per-entry `fsync` was removed — all three
  engines now apply "sync nothing, rename everything". See
  `docs/EXTRACTION-PERF.md`.)*
* Security checks run before any output file is opened:
  - encryption
  - path traversal (`..`), absolute POSIX paths, Windows drive letters
  - symbolic links, devices, FIFOs, sockets
  - duplicate entries / directory↔file clashes inside the archive
  - the four blast-radius caps above (entries, total bytes, file bytes,
    compression ratio)

## 4. Task layer (`src/filemgr_internal.h`, `src/extract.c`)

### 4.1 New task field

```c
typedef struct file_task {
  /* …existing fields… */
  int extract_conflict;
  int extract_remove_source;
  int extract_large;     /* ← new: 0 = ZIPX_LIMITS_DEFAULT, 1 = ZIPX_LIMITS_LARGE */
  /* …existing fields… */
} file_task_t;
```

### 4.2 Worker dispatch

In `extract_worker()` (`src/extract.c`, ~line 115):

```c
status = zipx_extract(task->src, task->dst, conflict,
                      zipx_limits_profile(task->extract_large),
                      extract_cancel, extract_progress, task, &result);
```

The third positional argument moved from a direct `&k_default_limits` (or a
caller-supplied struct) to `zipx_limits_profile(task->extract_large)`. The
`limits` parameter of `zipx_extract()` remains `const zipx_limits_t *` —
either pointer is fine.

### 4.3 Form parsing in `api_extract()`

```c
char *large_str = body_form_value(body, body_size, "large");
int   large     = (large_str != NULL && !strcmp(large_str, "1")) ? 1 : 0;
/* … free chain updated to release large_str … */
task->extract_large = large;
```

The string comparison is **strict** — only the literal `"1"` activates the
large profile. `"true"`, `"yes"`, `"on"` are all ignored. This matches the
convention used by the existing `remove_source` field (`src/extract.c`).

### 4.4 Error reporting path

When the active profile rejects an archive the engine returns one of:

| `zipx_status_t`     | Frontend maps to         |
|---------------------|--------------------------|
| `ZIPX_ERR_LIMIT_ENTRIES` | `err_extract_too_many_entries` |
| `ZIPX_ERR_LIMIT_FILE`    | `err_extract_entry_too_large` |
| `ZIPX_ERR_LIMIT_TOTAL`   | `err_extract_total_too_large` |
| `ZIPX_ERR_LIMIT_RATIO`   | `err_extract_ratio`           |
| `ZIPX_ERR_LIMIT_DEPTH`   | `err_extract_depth`           |
| `ZIPX_ERR_LIMIT_NAME`    | `err_extract_name`            |

The error string embedded in `zipx_result_t.message` interpolates the active
limit (`"compression ratio is above %u"`), so users can see exactly which cap
hit. **v1.7 does not change** this mapping — the new large profile uses the
same codes, just with larger caps.

## 5. HTTP API contract (`/api/extract`)

`POST /api/extract` accepts `application/x-www-form-urlencoded`:

| Field           | Type | Required | Notes |
|-----------------|------|----------|-------|
| `path`          | string | yes   | Absolute path to the archive on the PS5. |
| `dst_dir`       | string | yes   | Output directory. |
| `conflict`      | string | no    | `fail` / `overwrite` / `merge`. Default `fail`. |
| `remove_source` | `0`/`1` | no   | Default `0`. |
| `large`         | `0`/`1` | no   | **new in v1.7**. Default `0`. |

Compatibility:

- Existing callers that do **not** send `large` get identical behaviour as
  before the upgrade — `ZIPX_LIMITS_DEFAULT` always.
- The server returns `400` for any value other than `"0"` or `"1"` (only the
  exact literal `"1"` enables the large profile). This is enforced by the
  `!strcmp(large_str, "1")` guard, not a separate validator.

The hand-rolled form parser (`src/json_util.c` `body_form_value()`) already
returned the raw value; the upgrade did not touch that helper.

## 6. Frontend (`assets/main.js`)

### 6.1 Threshold + prompt primitives

```js
const LARGE_FILE_THRESHOLD_BYTES = 60 * 1024 * 1024 * 1024;  // 60 GiB

function shouldPromptLargeMode(itemSize) {
  return Number(itemSize || 0) > LARGE_FILE_THRESHOLD_BYTES;
}

function promptLargeMode(sizeBytes) {
  return confirm(t("extractLargeAsk",
                   { size: formatSize(sizeBytes, "-") }));
}
```

The 60 GiB threshold is **hardcoded**, not user-configurable. To change it,
edit the constant on line `813` (current main branch). Setting it to
`Infinity` silences the prompt entirely.

### 6.2 The two call sites

* `actionExtract()` (existing) — invoked from the file-row "extract" menu
  item — checks `item.size` and prompts before calling
  `startExtractTask(item.path, cwd, conflict, false, displayName(item), large)`.
* `uploadAndExtractFile()` (existing) — invoked after a remote upload
  completes — checks `file.size` and prompts before calling
  `startExtractTask(zipPath, cwd, conflict, true, rel, large)`.

Both paths funnel into `startExtractTask(path, dst, conflict, removeSource,
name, large)`, which `POST`s to `/api/extract` with:

```js
const data = await apiForm("/api/extract", {
  path,
  dst_dir: dstDir,
  conflict,
  remove_source: removeSource ? "1" : "0",
  large: large ? "1" : "0"
});
```

If the user clicks **Cancel** on the prompt, `large = false` and the request
goes out with `large=0`. **No silent fallback** to the large profile.

### 6.3 UI status

When `large=1` is sent, the engine logs `task->error_code = "err_extract_large"`
status field so the in-app task overlay can show a "large-file profile
active" badge. The exact badge wording lives in the locale files (see §7).

## 7. i18n strings (`assets/lang-{en,zh}.js`)

Two new keys, both on line `108-109` of each locale file:

* `extractLargeAsk` — the prompt body. Interpolation parameter: `size`
  (already pre-formatted by `formatSize()` in bytes / KiB / MiB / GiB / TiB).
* `extractLargeActive` — short tag shown next to a running large-profile
  task.

When changing the wording, keep the `{size}` placeholder and the
newline-separated **OK / Cancel** hint — the prompt is a `confirm()` so the
expected user gesture is documented inside the dialog.

## 8. Tests (`tests/test_zip_extract.c`, `tests/make_fixtures.py`)

### 8.1 Coverage matrix

| Scenario | Default | Large | Notes |
|---|---|---|---|
| `basic.zip` (small, benign) | ✓ | ✓ | sanity |
| `medium_bomb.zip` (1 MiB → ratio ≈ 238) | reject | accept | new fixture, profile switchover |
| `bomb.zip` (4 MiB of `'A'`, ratio ≈ 1026) | reject | **reject** | large caps still apply |
| `bomb.zip` with `tight.max_ratio = 10` | reject | reject | profile is a starting point, lower caps still enforced |
| `zip64.zip` with `tight.max_file_bytes = 1024` | reject | reject | lowering file cap from profile |
| Conflict policy `fail`/`overwrite`/`merge` | ✓ | ✓ | unchanged |

Run with:

```sh
cd tests && bash run-tests.sh
```

Output is a per-case `check()` style report — currently **69 checks**,
0 failures.

### 8.2 New fixture — `medium_bomb.zip`

* Generated by `make_fixtures.py::medium_bomb()`.
* Payload: 1 MiB of `bytes(range(256)) * 4096` (a perfect 256-byte period
  repeated 4096 times).
* Compression ratio (with `zlib -9`): **≈ 238 : 1**, deliberately chosen to
  fall in the band `(default_cap, large_cap) = (200, 1000]`.
* Why not the pre-existing `bomb.zip` (4 MiB of `'A'`)? Its real ratio on
  `zlib -9` is **≈ 1026**, which the large profile's 1000 cap also rejects —
  the two profiles would behave identically and the test wouldn't show the
  switchover.

### 8.3 `test_large_profile()` cases

```c
const zipx_limits_t *d = zipx_limits_profile(ZIPX_LIMITS_DEFAULT);
const zipx_limits_t *l = zipx_limits_profile(ZIPX_LIMITS_LARGE);

check(d != NULL, "default profile exists");
check(l != NULL, "large profile exists");

check(d->max_total_bytes == 512ULL * 1024 * 1024 * 1024, "default 512 GiB");
check(d->max_file_bytes  ==  64ULL * 1024 * 1024 * 1024, "default  64 GiB");
check(d->max_ratio       == 200,                          "default ratio 200");
check(l->max_entries      == 500000, "large 500K entries");
check(l->max_total_bytes  == 2ULL  * 1024 * 1024 * 1024 * 1024, "large 2 TiB");
check(l->max_file_bytes   == 1ULL  * 1024 * 1024 * 1024 * 1024, "large 1 TiB");
check(l->max_ratio        == 1000, "large ratio 1000");

expect_status("medium_bomb.zip", "out_default_medium", ZIPX_CONFLICT_FAIL, NULL,
              ZIPX_ERR_LIMIT_RATIO, "default rejects medium_bomb");
expect_ok    ("medium_bomb.zip", "out_large_medium",   ZIPX_CONFLICT_FAIL, l,
              "large accepts medium_bomb");

/* Large caps still enforced — bomb ratio 1026 > 1000 */
expect_status("bomb.zip", "out_large_bomb_default", ZIPX_CONFLICT_FAIL, NULL,
              ZIPX_ERR_LIMIT_RATIO, "default rejects bomb");
expect_ok    ("bomb.zip", "out_large_bomb_large",   ZIPX_CONFLICT_FAIL, l,
              "large accepts bomb-ratio-1000 border");

/* Lowered caps remain enforced */
zipx_limits_t tight = *l; tight.max_ratio = 10;
expect_status("bomb.zip", "out_tight_ratio", ZIPX_CONFLICT_FAIL, &tight,
              ZIPX_ERR_LIMIT_RATIO, "tight ratio still enforced");
```

(13 new `check`/`expect_*` calls in this function alone.)

## 9. Build & verify pipeline

### 9.1 Cross-compile

The WSL staging helper `.build/build-elf.sh` automates the SDK write-access
workaround (the SDK lives at `/opt/ps5-payload-sdk/target/` owned by a
different uid). The script:

1. Runs `make` with a staging dir under `/tmp` for write-protected sources.
2. `sudo cp -r` the staged outputs back into the SDK tree in a single batch.
3. Copies the resulting `web-file-mgr.elf` to both `/home/song/...` and the
   Windows desktop.

Incremental builds are fast — only `src/extract.c` recompiled for v1.7; the
nine `gen/lang-*.js.c` files were regenerated because `extractLargeAsk`
changed.

### 9.2 ELF sanity checks

```sh
ls -la web-file-mgr.elf
sha256sum web-file-mgr.elf                             # 648e4a00…
file  web-file-mgr.elf                                 # ELF 64-bit LSB pie, x86-64
od -An -tx1 -N20 web-file-mgr.elf | head -2           # 7f45 4c46 0201 + e_machine 003e
```

### 9.3 Verifying the upgrade made it into the binary

`assets/*.js` are embedded as `zlib`-compressed C arrays by `gen-asset-module.py`.
A simple `strings web-file-mgr.elf | grep` won't find them. Use
`.build/check-elf-gzip.py`:

```sh
python3 .build/check-elf-gzip.py ./web-file-mgr.elf
# expects:
#   keys found (7/7):
#     ✓ extractLargeAsk     "The archive looks large …"
#     ✓ extractLargeActive  "Large-file profile is enabled for this task"
#     ✓ promptLargeMode     confirm(t("extractLargeAsk", …))
#     ✓ shouldPromptLargeMode  Number(itemSize || 0) > LARGE_FILE_THRESHOLD_BYTES
#     ✓ LARGE_FILE_THRESHOLD_BYTES = 60 * 1024 * 1024 * 1024
#     ✓ large   ("…":"1":"0")
#     ✓ /api/extract … large: large ? "1" : "0" …
```

If any of these are missing, the cross-compile did not pick up the asset
rebuild — run `make clean && make` (or remove only `gen/`) and rebuild.

## 10. Backwards compatibility & migration

| Surface | v1.6 → v1.7 | Notes |
|---|---|---|
| `zipx_extract()` signature | unchanged | old callers compile clean |
| `zipx_default_limits()` body | unchanged | still returns `&k_default_limits` |
| `/api/extract` `large` field | new (optional) | absent → `0` (default profile) |
| `file_task_t::extract_large` | new (last field of the extract trio) | downstream consumers reading tasks must handle the new field |
| Frontend default behaviour | unchanged | threshold gate is new |
| `./web-file-mgr-linux` ABI | unchanged | Linux build also rebuilt with the new symbols |

**Migration for downstream users**: nothing required. To opt in to large
archives, append `large=1` to the `/api/extract` request, OR click "OK" on the
prompt that appears for any archive > 60 GiB on disk.

## 11. Trade-offs and known edges

* **60 GiB threshold is hardcoded** — yes, deliberate. It's set where the
  archival image / dump boundary typically lives. Power users can edit
  `LARGE_FILE_THRESHOLD_BYTES` (line 813 in `assets/main.js`).
* **The large profile trusts the user about free space** — the server does
  not run `statvfs()` against `dst_dir` before extraction. The space check
  the engine itself runs (per-entry `max_file_bytes`) is the only guard.
* **`bomb.zip` with ratio 1026 is rejected under both profiles** — known and
  intentional. The 1000 cap is the *floor* of the relaxed policy, not a
  "ZIP bombs welcome" flag. Other ZIP-bomb-shaped payloads with the same
  ratio will hit the same wall.
* **No multi-volume / split support** — unchanged from pre-v1.7. The
  engine reads a single archive path; spans such as `archive.zip`,
  `archive.z01`, `archive.z02` are not stitched. minizip-ng has the API;
  wiring it is on the post-v1.7 roadmap.
* **No proxy / streaming for archives above the STAGING_DIR ceiling** — the
  staging dir lives on the same filesystem as `dst_dir` and is sized
  proportionally. 2 TiB staging is required for a worst-case 2 TiB archive.

## 12. Future work (post-v1.7, prioritised)

1. Multi-volume support via `mz_zip_open_multi()` from vendored minizip-ng.
2. Server-side `statvfs()` preflight against `dst_dir` when the active
   profile is `LARGE`, with a clearer error if there's not enough space.
3. Compression-ratio cap that scales with file size (≤ small files: strict
   200:1; large files: relaxed). Same vector as the explicit profile but
   automatic.
4. Streaming extractor API — `zipx_extract_stream()` — that does not
   materialise the staging dir at all. Useful once PS5 archive > 4 TiB is a
   real workload.
5. Server-side telemetry (opt-in) for which profile is chosen per archive
   size band, to validate the 60 GiB threshold over time.
