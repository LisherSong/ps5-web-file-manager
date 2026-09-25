<div align="right">
  <a href="README.md">English</a> · <a href="README.zh-CN.md">简体中文</a>
</div>

# PS5 Web File Manager

<p align="center">
  <a href="https://github.com/LisherSong/ps5-web-file-manager/releases/latest"><img src="https://img.shields.io/github/v/release/LisherSong/ps5-web-file-manager" alt="Latest release"></a>
  <a href="LICENSE"><img src="https://img.shields.io/github/license/LisherSong/ps5-web-file-manager?color=blue" alt="License"></a>
  <img src="https://img.shields.io/badge/target-x86__64--sie--ps5-blue" alt="Target platform: x86_64-sie-ps5">
  <a href="https://github.com/LisherSong/ps5-web-file-manager/releases"><img src="https://img.shields.io/github/downloads/LisherSong/ps5-web-file-manager/total?color=green" alt="Total downloads"></a>
</p>

<p align="center">
  <a href="https://github.com/LisherSong/ps5-web-file-manager/releases/latest"><img src="https://img.shields.io/badge/Download-ELF%20payload-2ea44f?style=for-the-badge" alt="Download the ELF payload"></a>
  <a href="docs/USER-GUIDE-zh-CN.md"><img src="https://img.shields.io/badge/%E6%96%B0%E6%89%8B%E4%BD%BF%E7%94%A8%E8%AF%B4%E6%98%8E-%E4%B8%AD%E6%96%87-2563eb?style=for-the-badge" alt="Beginner's guide (Chinese)"></a>
</p>

> A homebrew HTTP file manager for jailbroken PS5 consoles. Browse, edit, upload,
> download and extract archives from any browser on the same network — one
> self-contained ELF payload, no external helper file, no telemetry.

**Version:** v1.9.3M · **Title ID:** `FMGR88888` · **License:** GPLv3+ · **Target:** `x86_64-sie-ps5`

**Download:** [latest release](https://github.com/LisherSong/ps5-web-file-manager/releases/latest) · **First time here?** [Beginner's guide (中文)](docs/USER-GUIDE-zh-CN.md)

---

## What it is

A single payload ELF that runs an HTTP file manager inside a jailbroken PS5.
Send it to the console's ELF loader, and the console starts an HTTP service on
port `8888` (it walks up to the next free port if that one is taken). Open
`http://<PS5_IP>:8888/` from any browser on the LAN — including the PS5's own
browser — and manage files on attached USB storage and the user partition.

It was written to make one job safe and fast: **copying game-dump folders from
USB storage to internal storage.** Everything else it does — browsing, sorting,
permissions, in-place text editing, image preview, PKG install, multi-select
copy/move/delete, upload and download — exists to make that job practical. On
top of that this fork adds **native archive extraction** for ZIP, RAR and 7z,
with the safety rails ("zip bomb", path traversal, disk-full) that the upstream
helper approach does not have.

The same source tree also builds a Linux binary, so the whole UI can be worked
on without a console or the PS5 SDK:

```sh
make linux && ./web-file-mgr-linux-v1.9.3M
```

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

**Files and folders**

- **Browse and sort** — list files and folders; sort by name, type, size,
  modified time or permissions. The chosen sort mode persists in
  `localStorage`.
- **Permissions** — toggle read / write / execute from the permissions column
  with checkboxes, or paste a validated four-digit octal mode.
- **Copy and move** — the two-step, clipboard-style flow: select the sources,
  then paste (copy) or move them into the folder you browse to next. Conflict
  prompts appear for overwriting files and for merging folders.
- **Delete** — recursive and permanent; there is no recycle bin.
- **Create** — new folders and new empty text files.
- **Multi-select** — copy, move, delete or tar-download many items in one go.
- **Copied and moved files are chmod'ed `0777`** where the filesystem supports
  Unix permissions. FAT/exFAT-style filesystems may ignore the chmod — that is
  the filesystem's answer, not an error.

**Content**

- **Text editor** — in-place UTF-8 editing for files up to 1 MiB, across a
  curated extension list: `.txt .json .xml .ini .cfg .conf .md .log .lua .js
  .css .html .htm .c .h .cpp .hpp .sh .csv .yaml .yml .shn`. Non-UTF-8 and
  oversized files are refused rather than mangled.
- **Image preview** — `.png .jpg .jpeg .gif .bmp .webp`, served straight from
  the console.
- **PKG** — install `.pkg` files and preview their metadata.

**Moving data in and out**

- **Upload** — a "Upload ▾" menu offering *single file* and *folder tree*;
  full-page drag and drop works too, and the footer says so. Files are written
  to a temporary name and renamed into place when the transfer completes.
  Hidden in the PS5 browser, since the point is to drive the console from
  another device.
- **Download** — a single file as raw bytes, or folders / multi-selection as a
  streamed `.tar` that is never written to console storage first. Hidden in the
  PS5 browser.
- **Upload and extract** — pick an archive, choose "extract after upload", and
  the extraction starts as soon as the upload lands. If it turns out to be
  encrypted, the password prompt appears immediately.

**Archive extraction** — see [Archive support](#archive-support) for the full
matrix. In short: ZIP, RAR and 7z, plain or encrypted, single or split, all
behind the same size / ratio / traversal / disk-space protection, and all
implemented inside this payload — no second file to install.

**Everything else**

- **Task overlay** — a full-screen overlay with delayed appearance, live
  progress, throughput, ETA, cancel, and recovery of the active task display if
  the browser is closed and reopened while the payload keeps running.
- **Localization** — English and Simplified Chinese, selected from
  `navigator.languages` / `navigator.language` (`zh*` → Chinese, everything
  else → English).
- **Mobile-friendly** — responsive layout with wrapped toolbars and
  horizontally scrollable file lists.
- **Start-up notification and home-screen launcher** — the notification shows
  the app name, the version and the actual listen port; on first start the
  payload installs a "PS5 Web File Manager" shortcut in the Media category
  without overwriting launcher files that already exist. The launcher icon and
  the browser favicon are the same embedded `icon0.png`, so the icon is stored
  once in the ELF.
- **Filenames survive mixed encodings** — names are transported as UTF-8 over
  the web API, but the payload also preserves the byte-oriented names returned
  by mounted filesystems, so a USB stick holding GBK names still displays and
  operates correctly. (This is a fork fix; see [Notes](#notes).)

## Archive support

Three engines, dispatched by extension in `src/extract.c`, sharing one
three-phase pipeline (`scan → extract to staging → publish by rename`) and one
set of limit profiles and conflict policies. Vendoring decisions and the
per-library licence position are in
[`third_party/unrar7/VENDORED.md`](third_party/unrar7/VENDORED.md) and
[`THIRD_PARTY_NOTICES`](THIRD_PARTY_NOTICES).

| | ZIP | RAR | 7z |
|---|---|---|---|
| Engine | `src/zip_extract.{c,h}` | `src/rar_extract.{c,h}` | `src/sevenz_extract.{c,h}` |
| Backend | vendored minizip-ng 4.2.2 + zlib | vendored **rarlab UnRAR 7.20.1** (official source) | LZMA SDK 26.03 decode subset + self-written codec chain |
| Stored / deflated | ✅ | n/a | ✅ (Copy / LZMA / LZMA2 / PPMd) |
| 64-bit sizes | ✅ ZIP64 | ✅ | ✅ |
| Filters / converters | — | — | ✅ Delta, BCJ2, PPC / IA64 / ARM / ARMT / SPARC |
| Multi-volume | ✅ parts offered by the engine | ✅ unrar stitches by name | ✅ |
| Traditional password | ✅ PKWARE "ZipCrypto" (`zip -e`) | ✅ `-p` | — |
| AES encryption | ✅ WinZip AES-128/192/256 | ✅ | ✅ 7zAES (AES-256-CBC) |
| Encrypted file names | — | ✅ `-hp` header encryption | ✅ `-mhe=on` encrypted header |
| Password prompt | on failure, retried | on failure, retried | asked up front |

**Volume naming that works**

| Format | Accepted | Note |
|---|---|---|
| ZIP | `name.zip.001…` (7-Zip), `name.part1.zip…` (WinRAR), `name.z01…` + `name.zip` (Info-ZIP) | Any part can be selected; the engine finds the rest in the same folder |
| RAR | `name.part1.rar` / `name.part01.rar` (first volume) | Select the **first** volume. Other volumes are greyed out with a hint |
| 7z | `name.7z.001…` | Any volume works; the engine walks the directory for the rest |

### Size and safety limits

Two profiles. The default is shipped safe; the large profile is engaged **only**
when the request carries `large=1`, and the UI asks for that opt-in through a
confirmation prompt.

| Limit | Default | Large (`large=1`) |
|---|---|---|
| `max_entries` | 200 000 | 500 000 |
| `max_total_bytes` (uncompressed) | 2 TiB | 4 TiB |
| `max_file_bytes` (per entry) | 512 GiB | 1 TiB |
| `max_ratio` (uncompressed ÷ compressed) | 500 : 1 | 1000 : 1 |
| `max_depth` (folder nesting) | 32 | 32 |
| `max_name_len` / `max_path_len` | 255 / 1024 | 255 / 1024 |

The default caps are sized for the console's real workload: a 3A title packed
as one ~300 GiB file inside an archive extracts without any prompt.

### Security checks

Extraction refuses, before creating a single output file:

- **Path traversal** — `..` segments, absolute POSIX paths, Windows drive
  letters, `\` treated as a separator inside RAR.
- **Special files** — symbolic links, devices, FIFOs, sockets
  (`ZIPX_ERR_SPECIAL`).
- **Duplicate entries**, and directory / file name clashes inside one archive.
- **Limit breaches** — expanded size, entry count, nesting depth, name length or
  compression ratio over the active profile.
- **Disk space** — `check_space()` consults `statvfs` for the *expanded* total
  before staging begins, so a download that cannot finish is never started.

Handing over a password does **not** skip the scan phase: an encrypted archive
gets the same limits as a plain one.

### Conflict policy

Passed as `conflict=` on `/api/extract`:

- `fail` (default) — refuse to overwrite anything that already exists.
- `overwrite` — replace existing files, merge into existing folders.
- `merge` — keep existing files, add the new ones.

### Password handling

A missing or wrong password comes back as `ZIPX_ERR_PASSWORD`
(`err_extract_password` in the UI). The frontend shows a password box and
re-sends **the same request** — same conflict policy, same large-file opt-in —
up to three times; cancelling or submitting an empty box falls back to the
original failure report. The first failure says the archive is encrypted rather
than blaming a password you were never asked for.

7z is the exception: because `-mhe=on` hides the file names inside the header,
the prompt comes **up front**, before the scan — otherwise an encrypted 7z would
cost a wasted scan before anyone could ask.

### Tuning the large-file prompt

The frontend threshold lives in `assets/main.js`:

```js
const LARGE_FILE_THRESHOLD_BYTES = 480 * 1024 * 1024 * 1024;   // 480 GiB
```

An archive larger than this on disk triggers the confirmation prompt. Set it to
`Infinity` to silence the prompt, lower it to be more conservative, or remove
the call — the server honours `large=1` regardless of what the frontend does.

### What this build refuses, on purpose

- **Formats other than ZIP / RAR / 7z.** `.tar`, `.tar.gz` / `.tgz`, `.gz`,
  `.xz`, `.bz2`, `.zst`, `.cab`, `.arj`, `.lzh`, `.cpio`, `.xar` and the rest of
  the long tail are not recognised. Upstream covers ~30 extensions by shipping
  a full 7-Zip as an external helper process; this fork deliberately does not —
  see [How this fork differs from upstream](#how-this-fork-differs-from-upstream).
- **ZIP entries using a compression method other than stored / deflated**, 7z
  folders with an unsupported coder, RAR older than 1.4.
- **RAR volume sets named `x.rar.001`.** unrar chains its own `x.partN.rar`
  naming; rename the parts (`.rar.001` → `.part1.rar`, `.002` → `.part2.rar`, …)
  and it works. Split ZIP and 7z sets accept the `.001` style directly.
- **RAR dictionaries above 4 GiB.** Such an archive is refused with its own
  `err_extract_dict_too_large` code and a message naming both the required and
  the supported size. Honouring it would mean one single allocation of the whole
  dictionary window — what rarlab's own CLI refuses by default and what a 16 GB
  shared-memory console cannot afford. (RAR5 caps the header field at 4 GiB, so
  this can only come from the newer RAR7 header format.) A wrong password is
  **not** in this category, and neither is a multi-volume set.

## Quickstart

1. **Build** the payload:

   ```sh
   export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk   # see Build below
   make
   ```

2. **Send** it to the console (the usual ELF-loader port is `9021`):

   ```sh
   nc -q0 "$PS5_HOST" 9021 < web-file-mgr-v1.9.3M.elf
   ```

3. **Read** the on-screen notification — it prints the actual listen port
   (usually `8888`).
4. **Open** `http://<PS5_IP>:<port>/` in any browser on the same LAN.
5. On first start the payload also writes a **Media**-category home-screen
   launcher; existing launcher files are left alone.

## Build

Requires the [ps5-payload-dev/sdk](https://github.com/ps5-payload-dev/sdk#quick-start):

```sh
export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk
```

The project links against `libmicrohttpd`. `make` checks for it and runs the
installer automatically when it is missing:

```sh
make
```

On a host without network access, drop the tarball in place and install it
manually first:

```sh
LIBMICROHTTPD_TARBALL=/path/to/libmicrohttpd-1.0.1.tar.gz \
  ./install-libmicrohttpd.sh
make
```

Output:

```text
web-file-mgr-v1.9.3M.elf        # x86_64-sie-ps5, ~882 KiB
```

The version string is part of `VERSION_TAG` and therefore of the output **file
name**, so a build cannot silently shadow another version's artifact. Override
it when needed:

```sh
make VERSION_TAG=v1.9.4M
```

For UI/JS work without the PS5 toolchain:

```sh
make linux
./web-file-mgr-linux-v1.9.3M
```

The Linux build does not include the PS5 home-screen launcher installer.

## Usage

Start an ELF loader on the console (port `9021` is the common one) and send the
payload:

```sh
export PS5_HOST=ps5_ip_address
nc -q0 "$PS5_HOST" 9021 < web-file-mgr-v1.9.3M.elf
```

After it starts, the notification shows the app name, the version and the actual
listen port. Open the URL it prints:

```text
http://${PS5_IP_ADDRESS}:8888/
```

If `8888` was already in use the payload walked up to the next free port — use
whatever the notification says, the URL is not hard-coded. On first start it
installs a `PS5 Web File Manager` shortcut in the Media category when needed;
missing launcher files are written, existing ones are preserved.

## Verifying the build

```sh
ls -la web-file-mgr-v1.9.3M.elf                    # ~882 KiB
sha256sum web-file-mgr-v1.9.3M.elf                 # 8ca47d5a…c9bb for v1.9.3M
file  web-file-mgr-v1.9.3M.elf                     # "ELF 64-bit LSB pie executable, x86-64"
od -An -tx1 -N20 web-file-mgr-v1.9.3M.elf | head -2 # magic 7f45 4c46 0201, e_machine 003e
```

`e_machine = 0x003e` confirms the PS5 target triple `x86_64-sie-ps5`;
`e_type = 3` (`ET_DYN`) confirms the position-independent payload an ELF loader
expects.

The JS/CSS/HTML assets are **gzip-compressed and embedded** in the ELF, so a
plain `strings` search for anything from `assets/` returns nothing useful. Use
the helper script instead:

```sh
python3 .build/check-elf-gzip.py ./web-file-mgr-v1.9.3M.elf uploadMenu extractRetryKey
```

## Tests

A POSIX / host-side C suite covers the ZIP, RAR and 7z engines and runs on any
Linux / macOS / MSYS shell without the PS5 SDK:

```sh
cd tests && bash run-tests.sh     # ZIP + RAR suites
bash run-sevenz-tests.sh          # 7z suite (needs MinGW gcc and a 7-Zip binary)
```

Current `main`: **177 checks** (140 ZIP + 37 RAR), 0 failures, plus **27 7z
cases**, 0 failures. Coverage:

- ZIP entry parsing (stored, deflated, ZIP64), and a byte-for-byte comparison
  of extracted content against real archives
- Path traversal, absolute paths, backslashes, Windows drive letters
- Symbolic links, FIFOs, bad CRC, truncated archives, non-ZIP input
- Every limit (entries, total bytes, file bytes, ratio, depth, name length)
- Conflict policies `fail` / `overwrite` / `merge`
- Cancellation in every phase, and the guarantee that a failure publishes
  nothing and cleans up its staging tree
- **Encrypted archives** — each real fixture is run four ways: no password,
  empty password and wrong password all yield `ZIPX_ERR_PASSWORD`, correct
  password succeeds with a byte-level content check. Two further cases prove the
  limits still apply once a password has been handed over. Fixtures:
  `enc-zipcrypto.zip`, `enc-aes256.zip`, `enc-aes256-store.zip` (ZIP),
  `enc-v6.rar` (RAR), `aeshe.7z` (7z, encrypted header)
- **Large-file profile** — `medium_bomb.zip` (ratio ≈ 238) is rejected under the
  default caps and accepted under the large ones
- **Format dispatch** — a renamed ZIP and a junk blob are both refused

Three frontend/served-page harnesses live in `.build/` (a development-time
directory, outside the gitignore whitelist):

| Script | Covers | Checks |
|---|---|---|
| `ui_retry_test.mjs` | the password retry flow in the real `assets/main.js` against a stubbed DOM: remembered request, retry cap, give-up paths, and the regression case for a non-ASCII folder | 40 |
| `ui_upload_menu_test.mjs` | the markup side: every `data-i18n` key exists in both languages, all 117 `t("…")` keys used in `main.js` are translated, the upload menu is wired to the right handlers, the classes it uses are styled, the row-highlight rules keep their panel scope, and the extract button is never hidden — only disabled | 40 |
| `preview_check.mjs` | the real page against a fixture API in headless Chromium: menu hidden at rest / opens / focus / reaches the file input / closes, footer layout, and the pinned toolbar wrap thresholds | 12 assertions |

## Project layout

```
.
├── Makefile                      # PS5 + Linux builds (VERSION_TAG v1.9.3M)
├── install-libmicrohttpd.sh      # one-shot dependency installer
├── gen-asset-module.py           # embeds assets/* as gzip-compressed C arrays
├── assets/                       # HTML / CSS / JS / icons / param.json
├── src/                          # C payload sources
│   ├── main.c  websrv.c  filemgr.c        # entry, HTTP frontend, task model
│   ├── upload.c  download.c  text.c       # stream and in-place edit handlers
│   ├── extract.c                          # /api/extract dispatcher (ZIP + RAR + 7z)
│   ├── zip_extract.{c,h}  zipx_common.c   # ZIP engine (minizip-ng backend)
│   ├── zipx_volume.c  zipx_volstream.c    # ZIP volume detection + concatenating stream
│   ├── rar_extract.{c,h}                  # RAR engine (rarlab UnRAR 7.20.1 backend)
│   ├── sevenz_extract.{c,h}  sevenz_chain.{c,h}   # 7z engine, self-parsed codec chain
│   ├── sevenz_header.{c,h}                # 7z header reader / -mhe=on decryption
│   ├── sevenz_mt.c  sevenz_volstream.c    # multi-threaded LZMA2 + .7z.001 volumes
│   ├── app_installer.c  pkg_installer.c  pkg_info.c   # PS5 PKG preview / install
│   └── demangle_stub.c  cpu_support_stub.c            # size / portability stubs
├── third_party/                  # vendored libraries
│   ├── unrar7/                            # rarlab UnRAR 7.20.1 — RAR engine
│   ├── minizip-ng/                        # 4.2.2, trimmed to the read path
│   ├── 7z/                                # LZMA SDK 26.03 decode subset
│   └── zlib/                              # minizip's compression backend
├── tests/                        # POSIX/host test suite
│   ├── test_zip_extract.c  test_rar_extract.c  test_sevenz_extract.c
│   ├── sevenz_chain_e2e.c  sevenz_e2e.c  bigfile_e2e.c
│   ├── make_fixtures.py  make_sevenz_fixtures.py  make_split_fixtures.py
│   ├── run-tests.sh                       # one-shot runner (ZIP + RAR suites)
│   ├── run-sevenz-tests.sh                # 7z suite
│   ├── bench_driver.py  bench_formats.py  # throughput benchmarks
│   ├── compat/                            # tiny Win32/MSYS shims
│   └── fixtures/  fixtures-7z/  fixtures-real/
├── docs/
│   ├── USER-GUIDE-zh-CN.md       # beginner's walkthrough (Chinese)
│   ├── DEVICE-TEST-v1.9.3M.md    # the acceptance checklist run before release
│   ├── SIZE-OPTIMIZATION.md      # ELF size analysis + per-symbol ledger
│   ├── EXTRACTION-PERF.md        # decompression benchmarks
│   ├── REAL-CONSOLE-PROFILE.md   # measured on-device throughput
│   ├── UPSTREAM-V1.8-COMPARISON.md  # this fork vs upstream's helper approach
│   ├── REWRITE-FEASIBILITY.md    # engine-extraction study
│   ├── UPGRADE-v1.7-zip-large-file-profile.md
│   ├── UPGRADE-v1.8-rar-support.md
│   └── screenshots/              # README screenshot images
├── CHANGELOG.md                  # per-release history
├── THIRD_PARTY_NOTICES           # per-library licence summary
├── HANDOVER.md                   # current engineering handover
├── LICENSE                       # GPLv3+
└── README.md
```

## How this fork differs from upstream

This project is a fork of
[owendswang/ps5-web-file-manager](https://github.com/owendswang/ps5-web-file-manager).
The web UI, the task model and the PS5 packaging all originate upstream, and the
upstream author's release under GPL-3.0 is what makes this derivative work
possible. From v1.8 onward, upstream outsources extraction to a **separate
helper process** — a full 7-Zip shipped as `wfm-7zip-helper.elf`, which the user
must install at `/data/wfm/` themselves. This fork takes the opposite route: the
decoders are vendored *into* the payload.

| | Upstream | This fork |
|---|---|---|
| Extraction architecture | external `wfm-7zip-helper.elf` (~100 MB, distributed separately, fixed path `/data/wfm/`), driven over a Unix-socket IPC protocol | the engines live **inside the payload**; there is no second file and no IPC |
| Deployment | two files; a missing/misplaced helper means extraction is dead (`archive_helper_not_running`) | one ELF, no external dependency |
| Formats | ~30 extensions (`.tar`, `.gz`, `.xz`, `.bz2`, `.zst`, `.cab`, `.arj`, `.lzh`, `.cpio`, …) | `.zip` / `.rar` / `.7z` and their volume forms — three, each complete |
| Zip-bomb and ratio defence | none | entry count, total size, per-file size, compression ratio, and a 1 GiB exemption so small files are not falsely flagged |
| Disk-space pre-check | none | `statvfs` against the expanded total before staging |
| Path-traversal defence | delegated to 7-Zip | implemented here, with a dedicated test group |
| Failure residue | can leave a half-extracted directory | staging directory + rename; a failure or cancel cleans up and publishes nothing |
| Password prompts | the helper's IPC protocol carries a `PASSWORD_REQUIRED` message | prompt + retry (capped at three attempts) reported as `extract_password`; 7z asks up front |
| Task survivability across a payload restart | ✅ the helper is a separate process, so a job survives | ❌ a restart loses the running task |
| Memory isolation | ✅ extraction runs in its own process | ❌ shares the address space (the LZMA2 dictionary is capped instead) |
| Version identity | plain `vX.Y.Z` | `vX.Y.ZM` — the trailing `M` marks a fork build |

The measurement and the reasoning behind this trade-off are in
[`docs/UPSTREAM-V1.8-COMPARISON.md`](docs/UPSTREAM-V1.8-COMPARISON.md). In one
line: upstream wins on format breadth and process architecture, this fork wins
on safety, deployment and error quality. The format gap is incremental work
inside the existing architecture, not a reason to go back.

## Notes

- Copy, move, delete, upload and download run as single background tasks. While
  one task is running, other file operations are rejected.
- Delete is recursive and permanent. There is no recycle bin.
- Copy/move tasks can be cancelled. A partially copied single file is removed;
  partially copied **folders** are left in place, to avoid deleting pre-existing
  files when merging into an existing target folder.
- Upload tasks can be cancelled; a partially uploaded temporary file is removed
  when possible.
- Downloading a folder or a multi-selection produces a tar stream generated on
  the fly — it is not written to console storage first.
- The UI recovers the active-task display if the browser is closed and reopened
  while the payload is still running.
- Text editing is limited to the extension list above; non-UTF-8 and oversized
  files are refused.
- **Filename encoding:** names travel over the web API as UTF-8, while a mounted
  filesystem may hand back legacy byte sequences (a GBK USB stick, for example).
  Rather than losing those bytes, the API maps every byte ≥ `0x80` to `\u00XX`
  and restores it on the way back, and the frontend decodes to GBK/gb18030 for
  display. The practical consequence is that the same directory has two
  different string representations — the page's and the server's — which is why
  nothing in the frontend may use a path as a cross-request key.

## FAQ

- **This is homebrew software and does not intentionally modify system processes
  or kernel memory.** If you hit a kernel panic, make sure you are on a recent
  jailbreak method and ELF loader, or go back to the setup you normally use.
- **P2JB users** — if this payload triggers a kernel panic on that setup, do not
  use it there. Stability matters more than convenience when every retry is
  expensive.
- **The "preparing" stage can take a while** on a folder with many files — it
  sums the folder size and checks free space, which is what stops a copy, move,
  upload or download that could not finish safely from starting at all.
- **`err_extract_unsupported`** — the archive is one this build cannot read: a
  file that is not `.zip` / `.rar` / `.7z`, a ZIP entry using a compression
  method other than stored/deflated, a 7z folder with an unsupported coder, a
  split set whose naming is not recognised (a RAR set named `x.rar.001` must be
  renamed to `x.part1.rar`, `x.part2.rar`, …), or a RAR older than 1.4.
  **Encrypted and multi-volume archives are not in this category** — both are
  supported. The backend's own sentence is appended in parentheses and names the
  actual cause.
- **`err_extract_entry_too_large`** — the archive exceeds the default caps
  (512 GiB per entry / 500:1 ratio). Confirm the large-file prompt (which
  appears for archives over 480 GiB on disk), split the archive, or pass
  `large=1` to the API directly.
- **`err_extract_dict_too_large`** — the RAR archive declares a compression
  dictionary larger than this build supports (4096 MiB). Recompress it on a PC
  with `-md` at or below 4 GiB, or extract it there.
- **`err_extract_password`** — the archive is encrypted and the password was
  missing or wrong. That includes a 7z with an encrypted header (`-mhe=on`),
  where the file names and entry sizes live inside the header, so nothing can be
  listed until it decrypts.

## Version history

Per-release detail — artefacts, digests, section-size deltas, test counts — lives
in [`CHANGELOG.md`](./CHANGELOG.md).

| Release | Date | Headline |
|---|---|---|
| `v1.9.3M` | 2026-09-24 | Encrypted archives end to end (ZIP ZipCrypto + WinZip AES, RAR `-p`/`-hp`, 7z 7zAES incl. `-mhe=on`), dictionary reporting, and a UI pass (upload menu, drag hint, always-visible extract button) |
| `v1.9.2` | 2026-09-05 | Version-string-only re-release; tag re-cut so tag = source = binary |
| `v1.9.1` | 2026-09-05 | 7z engine, volume sets, 7zAES, and a −15.8 % size / throughput pass |
| `v1.9` | 2026-09-05 | RAR engine replaced with rarlab UnRAR 7.20.1 (RAR5 "v6", multi-volume) |
| `v1.8.3` | 2026-09-05 | "Upload and extract" accepts `.rar` |
| `v1.8.2` | 2026-09-05 | Per-entry cap raised for 3A single-file archives; two PS5-only build fixes |
| `v1.8.1` | 2026-09-05 | Default ZIP caps relaxed for system-backup archives |
| `v1.8` | 2026-09-05 | First RAR support (dmc_unrar), shared extraction protocol |
| `v1.7` | 2026-09-04 | ZIP large-file profile (`large=1`) |

## Credits

This project is a **fork of [owendswang/ps5-web-file-manager](https://github.com/owendswang/ps5-web-file-manager)** (GPL-3.0). The web UI,
the task model and the PS5 packaging all originate there, and the upstream
author's release under GPL-3.0 is what makes this derivative work possible.

**Telling a fork build from an upstream one:** since v1.9.3 the version string
carries an `M` suffix (`vX.Y.ZM`) — *M* for *Modified*. Upstream owendswang
releases are plain `vX.Y.Z`. So `v1.9.2` is upstream/fork-shared numbering while
`v1.9.3M` can only have come from this repository; the same letter appears in the
ELF file name, the PS5 start-up notification, `/api/version` and the web UI
footer. Releases before v1.9.3M predate the convention and keep their plain
numbers.

Built with reference to these projects:

- **[ps5-payload-dev/websrv](https://github.com/ps5-payload-dev/websrv):** HTTP server structure, static asset embedding ideas, PS5 browser/websrv behaviour and PKG install function. License: GPLv3+.
- **[ps5-payload-dev/ftpsrv](https://github.com/ps5-payload-dev/ftpsrv):** PS5 payload conventions, home-screen launcher/install flow reference, process handling style and startup installation reference. License: GPLv3+.
- **[seregonwar/zftpd](https://github.com/seregonwar/zftpd):** PS5 TCP socket buffer tuning and high-throughput transfer behaviour reference. License: MIT.
- **[itsPLK/ps5-payload-manager](https://github.com/itsPLK/ps5-payload-manager):** Payload building behaviour. License: GPLv3.
- **[libmicrohttpd](https://ftp.gnu.org/gnu/libmicrohttpd/):** Used as the embedded HTTP server library. Licensed by GNU under the LGPL; this payload links it as the SDK-provided static library.
- **[ps5-payload-dev/sdk](https://github.com/ps5-payload-dev/sdk):** Payload building foundation. License: GPLv3+.
- **[etaHEN](https://github.com/etaHEN/etaHEN):** ShellUI URI navigation used to return to the PS5 home screen before exit. License: GPLv3.
- **[ezremote](https://github.com/cy33hc/ps5-ezremote-client):** cited for the PKG-preview feature. License: **GPL-2.0-only** — its source files carry no "or later" notice, so it is **not** combinable with this GPL-3.0 codebase. **No code was taken from it:** `src/pkg_info.c` is an independent C99 implementation (it also reads the `.pkg` entry table and `param.json` fields, for which ezremote has no counterpart, and it uses the hand-written tokenizer in `src/json_util.c` rather than json-c). See `docs/REWRITE-FEASIBILITY.md` §2.2.
- **[zlib-ng/minizip-ng](https://github.com/zlib-ng/minizip-ng):** ZIP reader used by the `/api/extract` endpoint. Vendored under `third_party/minizip-ng/`. License: zlib.
- **[zlib](https://www.zlib.net/):** Compression backend for minizip-ng. Vendored under `third_party/zlib/`. License: zlib.
- **[rarlab UnRAR](https://www.rarlab.com/rar_add.htm)** — RAR reader used by the `/api/extract` endpoint since v1.9. Vendored under `third_party/unrar7/` (version 7.20.1, the RARDLL source set). License: **UnRAR freeware license** — see `third_party/unrar7/license.txt`. Note this is a restricted licence rather than a FLOSS one: it permits using the source to handle RAR archives but forbids using it to build a RAR-compatible compressor.
- **[opello/unrar](https://github.com/opello/unrar)** — the mirror the vendored rarlab sources were fetched from (commit `97e1780`).
- **[LZMA SDK](https://www.7-zip.org/sdk.html)** (7-Zip / Igor Pavlov) — 7z decoder used by the `/api/extract` endpoint since v1.9.1, vendored as a decode subset under `third_party/7z/`. License: public domain.
- **[DrMcCoy/dmc_unrar](https://github.com/DrMcCoy/dmc_unrar)** — RAR engine shipped in v1.8 only, superseded in v1.9 by rarlab UnRAR (it could not decode RAR5 "v6" archives or multi-volume sets). Removed from the tree; its licence was GPL-2.0-or-later.

## License

The project is distributed under **GPLv3 or later**, matching the GPLv3+ projects used as implementation references. See [`LICENSE`](./LICENSE).

Third-party projects retain their own licenses. Do not copy assets or source from the credited projects into another distribution without preserving the corresponding license notices.

If distributing binaries, comply with the LGPL terms for `libmicrohttpd`
in addition to this project's GPL license. The vendored `zlib` and
`minizip-ng` sources are distributed under the zlib license; retain the
copyright notices in `third_party/zlib/LICENSE` and
`third_party/minizip-ng/LICENSE` when redistributing binaries built
with this feature.

The vendored `third_party/unrar7/` sources (rarlab UnRAR — the RAR engine
behind `src/rar_extract.c`) are **not** GPL: they ship under the UnRAR
freeware license (see `third_party/unrar7/license.txt`), which forbids
using them to develop a RAR-compatible compressor. Keep that notice and
that restriction intact when redistributing. `THIRD_PARTY_NOTICES` carries
the full per-library summary.

## Disclaimer

Unofficial homebrew software. Runs only on jailbroken PS5 consoles. Use at your own risk — the authors are not responsible for damage, data loss, account action or warranty impact. Do not redistribute Sony-proprietary content. Under GPLv3+, modified redistributions must publish their sources.
