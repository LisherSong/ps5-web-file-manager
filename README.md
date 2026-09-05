# PS5 Web File Manager

> Homebrew HTTP file manager for jailbroken PS5 consoles. Browse, edit, upload, download and extract ZIPs through any browser on the same network — single self-contained ELF payload, no external services, no telemetry.

**Version:** v1.9 · **Title ID:** `FMGR88888` · **License:** GPLv3+ · **Target:** `x86_64-sie-ps5`

---

## Overview

A payload ELF that runs an HTTP file manager inside a jailbroken PS5. Open `http://<PS5_IP>:8888/` from any browser on the LAN — including the PS5 browser itself — to manage files on attached USB storage and the user partition. Designed for safely copying game-dump folders from USB to internal storage, but it also handles general file management, in-place text editing, PKG preview/install, image preview, and ZIP extraction with built-in zip-bomb protection.

The same source tree builds a Linux binary for development and a PS5 payload ELF for deployment — see `make linux` below.

## What's new in v1.9

- **RAR engine replaced with the official rarlab UnRAR 7.20.1**
  (`third_party/unrar7/`, replacing dmc_unrar). This is what actually
  makes RAR extraction work on real files: dmc_unrar could not decode
  archives written by **WinRAR 6.x/7.x** (RAR5 "v6" compression) and had
  no multi-volume support — both now work.
- **RAR5 "v6" archives extract** (the v1.8-era "corrupt archive" report
  on WinRAR 6/7 files is gone).
- **Multi-volume RAR** (`.part01.rar` chains): unrar stitches the parts by
  name when the full set sits next to the volume you open.
- Engine can decrypt encrypted RAR (`RARSetPassword`) — password UI /
  API plumbing still pending, encrypted archives are rejected for now.
- Host tests now run real archives (v6 / encrypted / 3-volume fixtures
  committed under `tests/fixtures-real/`): **70 ZIP + 24 RAR = 94 checks**.

## What's new in v1.8

- **Single-volume RAR extraction** via the vendored FLOSS library
  [`dmc_unrar`](https://github.com/DrMcCoy/dmc_unrar) (GPL-2.0-or-later).
  RAR 1.5, 2.x, 3.x, 4.x and 5.x archives are supported. `.rar` files
  appear in the file list with the **Extract** button enabled; the button
  is greyed out on `.part02+.rar` sub-volumes with the tooltip "select
  the main volume instead" — v1.8 cannot stitch multi-volume RARs (see
  the [RAR extraction](#rar-extraction) section below).
- **Shared extraction protocol** between the new `src/rar_extract.c`
  engine and the existing `src/zip_extract.c` engine: same `zipx_status_t`
  codes, same `zipx_limits_t` profile (default / `large=1`), same
  three-phase model (`scan → extract → publish → cleanup`), same staging
  directory layout, same conflict policy, same error mapping into the
  task UI. The dispatcher in `src/extract.c` is one tiny
  `ends_with_ci(…)` switch.
- **14 new host-side C tests** (`tests/test_rar_extract.c`) wired into
  the existing `tests/run-tests.sh`. Coverage: format dispatch, error
  translation across every `DMC_UNRAR_*` code that affects RAR users,
  limit-profile handoff. Total host checks: **69 ZIP + 14 RAR = 83**.
- **Documentation**: [`CHANGELOG.md`](./CHANGELOG.md),
  [`docs/UPGRADE-v1.8-rar-support.md`](./docs/UPGRADE-v1.8-rar-support.md)
  and the vendoring decision tree at
  [`third_party/unrar/VENDORED.md`](./third_party/unrar/VENDORED.md).
- See the [dedicated section](#rar-extraction) below for scope and the
  limitations that come from using dmc_unrar (no multi-volume, no
  encryption in v1.8 — both lift in v1.9 when the library is replaced).

## What's new in v1.8.1

- **Default ZIP limits relaxed** (companion to v1.7's large profile).
  v1.7 shipped with a 64 GiB default per-entry cap, which was too
  aggressive for typical PS5 system-backup ZIPs (200-300 GiB). v1.8.1
  raises the default profile to **1 TiB total / 256 GiB per entry /
  500 : 1 ratio**, with the `large=1` opt-in kept at 2 TiB / 1 TiB /
  1000 : 1. The frontend threshold rises from 60 GiB to 240 GiB so
  common system-backup archives no longer trigger the prompt.
- RAR extraction inherits the new defaults (rar_extract.c threads
  `c->limits` from the engine — no engine change required).
- Rationale: the real zip-bomb defence is `check_space()` (statvfs-based
  real disk-space check before staging) + `max_ratio` (declared
  compression ratio cap). The size caps are a UX guard, not a security
  boundary.

## What's new in v1.8.2

- **Default ZIP limits relaxed again** for the 3A-game single-file case.
  A single ~300 GiB uncompressed file inside an archive was still
  silently rejected by v1.8.1 (the default scan returns
  `ZIPX_ERR_LIMIT_FILE_SIZE` before the request ever reaches the
  frontend confirmation prompt). v1.8.2 raises the default profile to
  **2 TiB total / 512 GiB per entry / 500 : 1 ratio**, with the `large=1`
  opt-in bumped to 4 TiB / 1 TiB / 1000 : 1. Frontend threshold rises
  from 240 GiB to 480 GiB.
- **Two PS5-only build fixes** discovered when cross-compiling for the
  PS5 target. The host-side test suite (`tests/run-tests.sh`) had
  silently accepted both because it links the same sources but uses
  gcc rather than clang 18 and a different include path:
  - `Makefile` CFLAGS: add `-Ithird_party/unrar` so `src/rar_extract.c`
    can find the project-authored `dmc_unrar_api.h` facade header.
  - `src/extract.c`: move `extract_progress()` definition above
    `extract_dispatch()` so the implicit function declaration is not
    flagged by `-Werror=implicit-function-declaration` (clang 18 in the
    PS5 SDK is stricter than the host gcc used by tests).
- **Release artifact** for v1.8.2: `web-file-mgr.elf` — 509 704 bytes,
  sha256 `1b2c3d68b35e32737105f17d14a80a3c159ceca0cabd274ee168cbcd81906f65`,
  ELF class 64, little-endian, e_machine `0x003e` (x86_64-sie-ps5).
- Tests: **84 host-side checks** (70 ZIP + 14 RAR), 0 failures. PS5
  cross-compile succeeds end-to-end.

## What's new in v1.7

- **ZIP large-file profile** (opt-in via the new `large=1` argument on `/api/extract`): relaxed caps of **2 TiB** archive total, **1 TiB** per entry, **1000 : 1** compression ratio. The frontend prompts for confirmation whenever the archive on disk is larger than **60 GiB**; the server only activates the profile when the user explicitly agrees.
- **Stricter default ZIP profile** stays safe: **1 TiB** total / **256 GiB** per entry / **500 : 1** ratio. A 4 MiB compressed payload that expands to 800 GiB still gets rejected before any output file is opened.
- **69 host-side C tests** (`tests/run-tests.sh`) now cover path traversal, ZIP64, encryption rejection, ratios, conflict policies and the new large-file profile (`tests/test_zip_extract.c`).
- Earlier refinements — see `git log` since v1.6.

## Screenshots

<p>
  <a href="docs/screenshots/20260617_231827.376.jpg" target="_blank"><img src="docs/screenshots/20260617_231827.376.jpg" width="31%" alt="PS5 Web File Manager screenshot 1"></a>
  <a href="docs/screenshots/20260619_131432.399.jpg" target="_blank"><img src="docs/screenshots/20260619_131432.399.jpg" width="31%" alt="PS5 Web File Manager screenshot 2"></a>
  <a href="docs/screenshots/20260617_232348.855.jpg" target="_blank"><img src="docs/screenshots/20260617_232348.855.jpg" width="31%" alt="PS5 Web File Manager screenshot 3"></a>
  <a href="docs/screenshots/20260619_131811.644.jpg" target="_blank"><img src="docs/screenshots/20260619_131811.644.jpg" width="31%" alt="PS5 Web File Manager screenshot 4"></a>
  <a href="docs/screenshots/20260619_131535.239.jpg" target="_blank"><img src="docs/screenshots/20260619_131535.239.jpg" width="31%" alt="PS5 Web File Manager screenshot 5"></a>
  <a href="docs/screenshots/20260620_232728.533.jpg" target="_blank"><img src="docs/screenshots/20260620_232728.533.jpg" width="31%" alt="PS5 Web File Manager screenshot 6"></a>
</p>

## Features

- **Browse** — list files and folders; sort by name, type, size, mtime or permissions. Last sort mode persists in `localStorage`.
- **Permissions** — toggle read/write/execute with checkboxes, or paste a validated four-digit octal mode.
- **Operations** — copy, move, delete (recursive, no recycle bin), rename, create files and folders.
- **Editor** — in-place UTF-8 text editor for files ≤ 1 MiB across a curated extension list: `.txt .json .xml .ini .cfg .conf .md .log .lua .js .css .html .htm .c .h .cpp .hpp .sh .csv .yaml .yml .shn`.
- **Multi-select** — copy, move, delete or tar-download many items in one go.
- **Upload** — single files or folder trees from any device on the LAN (hidden in the PS5 browser). Atomic temp + rename.
- **Download** — single file as raw bytes, or folders/multi-select as a streaming `.tar`. Hidden in the PS5 browser.
- **Tasks** — full-screen overlay with delayed show, live progress, throughput, ETA, cancel, and recovery if the browser is closed and reopened mid-task.
- **Archive extraction** — ZIP (encrypted rejected) and RAR (v1.9: RAR4 + RAR5 incl. WinRAR 6/7 "v6", multi-volume; encrypted still rejected pending password UI); see the [ZIP extraction](#zip-extraction) and [RAR extraction](#rar-extraction) sections below for scope.
- **PKG** — install and preview `.pkg` files.
- **Images** — preview `.png .jpg .jpeg .gif .bmp .webp`.
- **Localization** — English + Simplified Chinese, auto-selected from `navigator.languages`.
- **Mobile-friendly** — responsive layout with wrapped toolbars and horizontally scrollable file lists.

## Quickstart

1. **Build** the ELF:

   ```sh
   export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk   # see "Build" for SDK setup
   make
   ```
2. **Send** the payload to the PS5 (default ELF-loader port `9021`):

   ```sh
   nc -q0 "$PS5_HOST" 9021 < web-file-mgr.elf
   ```
3. **Read** the on-screen PS5 notification — it prints the actual listen port (default `8888`).
4. **Open** `http://<PS5_IP>:<port>/` in any browser on the same LAN — the PS5 browser works too.
5. On first run, the payload also writes a **Media**-category home-screen launcher; existing launcher files are not overwritten.

## Build

Requires [ps5-payload-dev/sdk](https://github.com/ps5-payload-dev/sdk#quick-start):

```sh
export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk
```

This project links against `libmicrohttpd`. `make` checks for it before building and runs the installer automatically when missing:

```sh
make
```

If the build host has no network access, drop the libmicrohttpd tarball in advance and run the installer manually:

```sh
LIBMICROHTTPD_TARBALL=/path/to/libmicrohttpd-1.0.1.tar.gz \
  ./install-libmicrohttpd.sh
make
```

Output:

```text
web-file-mgr.elf   (~several hundred KiB, larger in v1.9 with unrar; x86_64-sie-ps5)
```

For pure UI/JS work without the PS5 toolchain:

```sh
make linux
./web-file-mgr-linux
```

The Linux build does **not** include the PS5 home-screen launcher installer.

## Usage

Start an ELF loader on the PS5 (port `9021` is common). Send the payload:

```sh
export PS5_HOST=ps5_ip_address
nc -q0 "$PS5_HOST" 9021 < web-file-mgr.elf
```

After the payload starts, the PS5 notification shows the app name, version and actual listen port. Open the URL it prints, for example:

```text
http://${PS5_IP_ADDRESS}:8888/
```

If the payload had to fall back to a different port (e.g. `8889`), use whatever port the notification shows — the URL is not hard-coded.

On first startup, the payload installs a `PS5 Web File Manager` shortcut in the Media category when needed. Existing launcher files are preserved; only missing ones are written.

## ZIP extraction

Plain ZIPs only — stored / deflated / ZIP64, **never encrypted**. The engine is a standalone three-phase module (`scan → extract → publish → cleanup`) at `src/zip_extract.{c,h}`, with a separate host-side C test suite. Each entry is first written into a staging directory (`*.wfm-part-*`), fsynced, then atomically renamed into the destination. Any failure mid-archive rolls back partial changes; cancel and fatal errors always clean up staging.

### Limits

| Limit | Default profile | Large profile (`ZIPX_LIMITS_LARGE`) |
|---|---|---|
| `max_entries` | 200 000 | 500 000 |
| `max_total_bytes` (uncompressed) | 2 TiB | 4 TiB |
| `max_file_bytes` (per entry) | 512 GiB | 1 TiB |
| `max_ratio` (uncompressed / compressed) | 500 : 1 | 1000 : 1 |
| `max_depth` (folder nesting) | 32 | 32 |
| `max_name_len` / `max_path_len` | 255 / 1024 | 255 / 1024 |

The **default profile** is shipped safe: a 4 MiB compressed blob that decodes to 800 GiB is rejected before any output file is opened. The **large profile** is engaged **only** when the request includes `large=1` — the archive dialog prompts the user automatically whenever the archive on disk is larger than `LARGE_FILE_THRESHOLD_BYTES` (480 GiB by default; configurable in `assets/main.js`). Confirming the prompt is the user's explicit opt-in; the server still records nothing extra on its own.

### Security checks

The engine refuses to extract:

- Encrypted entries (any encryption flag set).
- Path traversal (`..` segments, absolute POSIX paths, Windows drive letters).
- Symbolic links, devices, FIFOs, sockets (`ZIPX_ERR_SPECIAL`).
- Duplicate entries or directory/file name clashes inside the same archive.
- Archives whose expanded size, entry count, depth, name length or compression ratio breach the active profile.

### Conflict policy

Passed as `conflict=` on `/api/extract`:

- `fail` (default) — refuse to overwrite any existing target.
- `overwrite` — replace existing files; merge into existing folders.
- `merge` — keep existing files, add new ones.

### Tuning the threshold

The 480 GiB frontend threshold lives in `assets/main.js`:

```js
const LARGE_FILE_THRESHOLD_BYTES = 480 * 1024 * 1024 * 1024;
```

Set it to `Infinity` to silence the prompt, lower it to be more conservative, or remove the call entirely — the server still respects `large=1` regardless of the threshold.

## RAR extraction

A RAR extraction engine (`src/rar_extract.{c,h}`) backed by the **official
rarlab UnRAR source** (`third_party/unrar7/`, version 7.20.1, compiled as a
static library and driven through its C-compatible DLL API). Files with the
extension `.rar` get the same **Extract** button as `.zip` files; the engine
is dispatched by `src/extract.c` based on extension.

> v1.9 replaced the v1.8 engine (dmc_unrar 1.7.0). dmc_unrar could not
> decode archives written by WinRAR 6.x/7.x (RAR5 "v6" compression) and had
> no multi-volume support; unrar handles both natively.

### Scope

| Format | Support | Notes |
|---|---|---|
| RAR 1.5 → 4.x (incl. 2.9 / 3.6 / 4.0) | ✅ | |
| RAR 5.0 and **5.0 "v6"** (WinRAR 6.x / 7.x) | ✅ | The v1.9 trigger |
| Solid blocks, dictionary up to 1 GiB | ✅ | |
| PPMd decompression (RAR 3.0+) | ✅ | |
| **Multi-volume** (`.part01.rar` + `.part02.rar` + …) | ✅ | unrar stitches parts by name when the whole set sits next to the volume you open. Select the first volume (`name.part1.rar` / `name.part01.rar`); non-first volumes are still greyed out in the UI with a hint. |
| **Encrypted RAR** | ⏳ | The engine can decrypt (`RARSetPassword`), but the password field / prompt is not wired into `/api/extract` yet. Encrypted archives are rejected up front with `ZIPX_ERR_UNSUPPORTED`. |
| Symbolic links / FIFOs / sockets / devices | ❌ | Rejected with `ZIPX_ERR_SPECIAL` (mirrors ZIP behaviour) |
| RAR 1.3 (pre-1.4) | ❌ | Rejected upstream by unrar |

When an archive is rejected, the user gets an `extract_unsupported`
failure with the file name as the detail argument. The frontend already
shows this with the typical bilingual retry guidance.

### Limits

The RAR engine re-uses the ZIP limits table verbatim — there is no RAR
profile table on top. Defaults and the `large=1` opt-in are identical:

| Limit | Default profile | Large profile (`large=1`) |
|---|---|---|
| `max_entries` | 200 000 | 500 000 |
| `max_total_bytes` (uncompressed) | 2 TiB | 4 TiB |
| `max_file_bytes` (per entry) | 512 GiB | 1 TiB |
| `max_ratio` (uncompressed / compressed) | 500 : 1 | 1000 : 1 |
| `max_depth` (folder nesting) | 32 | 32 |
| `max_name_len` / `max_path_len` | 255 / 1024 | 255 / 1024 |

Large-profile RAR extraction uses the same `LARGE_FILE_THRESHOLD_BYTES`
(480 GiB) prompt as ZIP — the frontend treats `.rar` and `.zip` the same
way for the prompt, and the server only ever activates the large caps
when the request carries `large=1` (opt-in).

### Security checks

The RAR engine applies the same checks as the ZIP engine — re-uses
`zipx_status_t` codes, so the task UI's `err_extract_unsafe_name`,
`err_extract_too_deep`, `err_extract_ratio`, etc. all fire identically:

- Path traversal (`..` segments, absolute POSIX paths, Windows drive
  letters, `\` treated as a path separator after a `Rar!\x1a\x07…`
  header, etc.).
- Symbolic links, FIFOs, sockets, devices.
- Duplicate entries or directory/file name clashes inside the archive.
- Archive size, entry count, depth, name length or compression ratio
  breaches of the active profile.

### Vendoring and licence

`third_party/unrar7/` is a verbatim copy of the official **rarlab UnRAR
source** (7.20.1), mirrored by
[`opello/unrar`](https://github.com/opello/unrar) at commit `97e1780`. It is
distributed under the **UnRAR freeware licence** (see
`third_party/unrar7/license.txt`): it may be used in any software to handle
RAR archives, but may not be used to develop a RAR-compatible *archiver* or
re-create the RAR compression algorithm. The project-authored facade
`third_party/unrar7/unrar_c_api.h` carries the project's own licence.

> The v1.8 engine `third_party/unrar/dmc_unrar.c` (DrMcCoy/dmc_unrar 1.7.0,
> GPL-2.0-or-later) was removed in v1.9; its notice lives in git history.

### Encrypted RAR (planned)

The engine can decrypt archives (via `RARSetPassword`), but the password
channel — a `password=` field on `/api/extract` plus a frontend prompt —
is not wired yet. Encrypted archives currently fail with
`extract_unsupported`. The engine swap (v1.9) removed the hard engine
limits; the remaining work is purely API/UI plumbing.

## Verification

After `make`, sanity-check the produced ELF:

```sh
ls -la web-file-mgr.elf                            # size grew in v1.9 (unrar static library); ~509 KiB was v1.8.3
sha256sum web-file-mgr.elf                         # record the digest in your release notes
file  web-file-mgr.elf                             # expect "ELF 64-bit LSB pie executable, x86-64"
od -An -tx1 -N20 web-file-mgr.elf | head -2        # magic 7f45 4c46 0201 + e_machine 003e
```

The `e_machine = 0x003e` confirms the PS5 target triple `x86_64-sie-ps5`. The `e_type = 3` (`ET_DYN`) confirms the position-independent payload expected by ELF loaders.

## Tests

A POSIX/host-side C test suite covers the ZIP engine and runs on any Linux / macOS / MSYS shell without the PS5 SDK:

```sh
cd tests && bash run-tests.sh
```

Output is a per-case `check`-style report — **84 checks** on the current `main`
(70 ZIP + 14 RAR). Coverage:

- ZIP entry parsing (stored + deflated + ZIP64)
- Path traversal, absolute paths, backslash, Windows drive letters
- Symbolic links, FIFOs, encrypted entries, bad CRC, truncated archives, non-ZIP files
- Limits: `entries`, `total_bytes`, `file_bytes`, `ratio`, `depth`, `name_len`
- Conflict policies: `fail` / `overwrite` / `merge`
- Cancellation in every phase
- **Large-file profile** — `medium_bomb.zip` (ratio ≈ 238) is rejected under default caps and accepted under large caps; lowered large caps still enforce.
- **RAR engine** (`tests/test_rar_extract.c`, 14 checks) — format
  dispatch (renamed ZIP rejected, junk blob rejected), error translation
  across every reachable `DMC_UNRAR_*` code, limits handoff (the
  `large=1` opt-in flows into `rar_extract()` unchanged).

## Project layout

```
.
├── Makefile                      # PS5 + Linux builds (VERSION_TAG v1.8.2)
├── install-libmicrohttpd.sh      # one-shot dependency installer
├── gen-asset-module.py           # embeds assets/* as gzip-compressed C arrays
├── assets/                       # HTML / CSS / JS / icons / param.json
├── src/                          # C payload sources
│   ├── main.c  websrv.c  filemgr.c        # entry, HTTP frontend, task model
│   ├── upload.c  download.c               # stream handlers
│   ├── extract.c                          # /api/extract dispatcher (ZIP + RAR)
│   ├── zip_extract.{c,h}                  # ZIP engine (v1.7)
│   ├── rar_extract.{c,h}                  # RAR engine (v1.8, dmc_unrar backend)
│   └── app_installer.c                    # PS5 Media launcher installer
├── third_party/                  # vendored: zlib, minizip-ng, dmc_unrar
│   └── unrar/
│       ├── dmc_unrar.c                    # GPL-2.0-or-later, verbatim upstream
│       └── dmc_unrar_api.h                # project-authored facade header
├── tests/                        # POSIX/host test suite
│   ├── test_zip_extract.c
│   ├── test_rar_extract.c        # 14 RAR negative-path checks (v1.8)
│   ├── make_fixtures.py          # regenerate test fixtures
│   ├── run-tests.sh              # one-shot runner (now runs ZIP + RAR suites)
│   ├── compat/                   # tiny Win32/MSYS shims
│   └── fixtures/                 # generated test ZIPs (and a couple of stub .rar blobs)
├── docs/
│   ├── HANDOVER.md               # engineering handover / dev playbook (also §14 v1.8 close-out)
│   ├── UPGRADE-v1.7-zip-large-file-profile.md
│   ├── UPGRADE-v1.8-rar-support.md
│   └── screenshots/             # README screenshot images
├── THIRD_PARTY_NOTICES           # bundled-library credits (incl. dmc_unrar section)
├── LICENSE                       # GPLv3+
└── README.md
```

## Notes

- Copy, move, delete, upload and download run as single background tasks. While one task is running, other file operations are rejected.
- Delete is recursive and permanent. There is no recycle bin.
- Copy/move tasks can be canceled. A partially copied single file is removed, but partially copied folders are left in place to avoid deleting pre-existing files when merging into an existing target folder.
- Upload tasks can be canceled. A partially uploaded temporary file is removed when possible.
- Downloading a folder or multiple selected items produces a tar stream. The tar archive is generated by the payload and is not written to PS5 storage first.
- The UI can recover the active task display if the browser is closed and reopened while the payload process is still running.
- Text editing is limited to the curated extension list above. Non-UTF-8 and oversized files are rejected.
- File names are transmitted as UTF-8 through the web API. The payload also preserves legacy byte-oriented names returned by mounted filesystems so mixed USB filename encodings still display and operate correctly.

## FAQ

- **This is a homebrew app and should not intentionally modify system processes or kernel memory.** If you hit a kernel panic, make sure you are using a recent jailbreak method and ELF loader, or revert to the stable method you normally use.
- **P2JB users** — if this payload triggers a kernel panic, avoid using it on that setup. Stability matters more than convenience when each retry is expensive.
- **The preparing stage can take a while** when a folder contains many files — it sums folder size and checks free space, which helps avoid starting a copy / move / upload / download that cannot finish safely.
- **`err_extract_entry_too_large`** — default archive caps are 512 GiB per
  entry / 500:1 ratio (covers a typical 3A-game archive with one ~300 GiB
  uncompressed file). If you exceed the default, confirm the large-file
  prompt (appears for archives > 480 GiB on disk), split the archive, or
  pass `large=1` directly to the API.
- **`err_extract_unsupported`** — the archive uses a feature the engine
  cannot handle: encrypted ZIP, encrypted RAR, multi-volume RAR
  (`.part02+.rar`), very-old RAR 1.4, RAR symlinks / FIFOs, or a file
  that is neither `.zip` nor `.rar`. For RAR specifically the message
  lists the failure cause and points the user back to a PC extractor.

## Credits

This project was built with reference to these projects:

- **[ps5-payload-dev/websrv](https://github.com/ps5-payload-dev/websrv):** HTTP server structure, static asset embedding ideas, PS5 browser/websrv behaviour and PKG install function. License: GPLv3+.
- **[ps5-payload-dev/ftpsrv](https://github.com/ps5-payload-dev/ftpsrv):** PS5 payload conventions, home-screen launcher/install flow reference, process handling style and startup installation reference. License: GPLv3+.
- **[seregonwar/zftpd](https://github.com/seregonwar/zftpd):** PS5 TCP socket buffer tuning and high-throughput transfer behaviour reference. License: MIT.
- **[itsPLK/ps5-payload-manager](https://github.com/itsPLK/ps5-payload-manager):** Payload building behaviour. License: GPLv3.
- **[libmicrohttpd](https://ftp.gnu.org/gnu/libmicrohttpd/):** Used as the embedded HTTP server library. Licensed by GNU under the LGPL; this payload links it as the SDK-provided static library.
- **[ps5-payload-dev/sdk](https://github.com/ps5-payload-dev/sdk):** Payload building foundation. License: GPLv3+.
- **[etaHEN](https://github.com/etaHEN/etaHEN):** ShellUI URI navigation used to return to the PS5 home screen before exit. License: GPLv3.
- **[ezremote](https://github.com/cy33hc/ps5-ezremote-client):** Preview PKG info. License: GPLv2.
- **[zlib-ng/minizip-ng](https://github.com/zlib-ng/minizip-ng):** ZIP reader used by the `/api/extract` endpoint. Vendored under `third_party/minizip-ng/`. License: zlib.
- **[zlib](https://www.zlib.net/):** Compression backend for minizip-ng. Vendored under `third_party/zlib/`. License: zlib.
- **[DrMcCoy/dmc_unrar](https://github.com/DrMcCoy/dmc_unrar):** RAR reader used by the `/api/extract` endpoint. Vendored under `third_party/unrar/` as a single-file drop-in (`dmc_unrar.c`); the project-authored facade `dmc_unrar_api.h` carries the project's own licence. License: GPL-2.0-or-later — see `third_party/unrar/COPYING`.

## License

The project is distributed under **GPLv3 or later**, matching the GPLv3+ projects used as implementation references. See [`LICENSE`](./LICENSE).

Third-party projects retain their own licenses. Do not copy assets or source from the credited projects into another distribution without preserving the corresponding license notices.

If distributing binaries, comply with the LGPL terms for `libmicrohttpd`
in addition to this project's GPL license. The vendored `zlib` and
`minizip-ng` sources are distributed under the zlib license; retain the
copyright notices in `third_party/zlib/LICENSE` and
`third_party/minizip-ng/LICENSE` when redistributing binaries built
with this feature. The vendored `dmc_unrar` (RAR engine) is distributed
under the GPL-2.0-or-later; retain the copyright notice in
`third_party/unrar/COPYING` and ship the corresponding sources when
redistributing binaries built with v1.8 or later (the `web-file-mgr.elf`
binary is already GPLv3+, so the additional source-disclosure
requirement is the only practical effect).

## Disclaimer

Unofficial homebrew software. Runs only on jailbroken PS5 consoles. Use at your own risk — the authors are not responsible for damage, data loss, account action or warranty impact. Do not redistribute Sony-proprietary content. Under GPLv3+, modified redistributions must publish their sources.
