# PS5 Web File Manager

> Homebrew HTTP file manager for jailbroken PS5 consoles. Browse, edit, upload, download and extract ZIPs through any browser on the same network — single self-contained ELF payload, no external services, no telemetry.

**Version:** v1.7 · **Title ID:** `FMGR88888` · **License:** GPLv3+ · **Target:** `x86_64-sie-ps5`

---

## Overview

A payload ELF that runs an HTTP file manager inside a jailbroken PS5. Open `http://<PS5_IP>:8888/` from any browser on the LAN — including the PS5 browser itself — to manage files on attached USB storage and the user partition. Designed for safely copying game-dump folders from USB to internal storage, but it also handles general file management, in-place text editing, PKG preview/install, image preview, and ZIP extraction with built-in zip-bomb protection.

The same source tree builds a Linux binary for development and a PS5 payload ELF for deployment — see `make linux` below.

## What's new in v1.7

- **ZIP large-file profile** (opt-in via the new `large=1` argument on `/api/extract`): relaxed caps of **2 TiB** archive total, **1 TiB** per entry, **1000 : 1** compression ratio. The frontend prompts for confirmation whenever the archive on disk is larger than **60 GiB**; the server only activates the profile when the user explicitly agrees.
- **Stricter default ZIP profile** stays safe: **512 GiB** total / **64 GiB** per entry / **200 : 1** ratio. A 4 MiB compressed payload that expands to 800 GiB still gets rejected before any output file is opened.
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
- **ZIP extraction** — see the [dedicated section](#zip-extraction) below.
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
web-file-mgr.elf   (~418 KiB, x86_64-sie-ps5)
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
| `max_total_bytes` (uncompressed) | 512 GiB | 2 TiB |
| `max_file_bytes` (per entry) | 64 GiB | 1 TiB |
| `max_ratio` (uncompressed / compressed) | 200 : 1 | 1000 : 1 |
| `max_depth` (folder nesting) | 32 | 32 |
| `max_name_len` / `max_path_len` | 255 / 1024 | 255 / 1024 |

The **default profile** is shipped safe: a 4 MiB compressed blob that decodes to 800 GiB is rejected before any output file is opened. The **large profile** is engaged **only** when the request includes `large=1` — the archive dialog prompts the user automatically whenever the archive on disk is larger than `LARGE_FILE_THRESHOLD_BYTES` (60 GiB by default; configurable in `assets/main.js`). Confirming the prompt is the user's explicit opt-in; the server still records nothing extra on its own.

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

The 60 GiB frontend threshold lives in `assets/main.js`:

```js
const LARGE_FILE_THRESHOLD_BYTES = 60 * 1024 * 1024 * 1024;
```

Set it to `Infinity` to silence the prompt, lower it to be more conservative, or remove the call entirely — the server still respects `large=1` regardless of the threshold.

## Verification

After `make`, sanity-check the produced ELF:

```sh
ls -la web-file-mgr.elf                            # size ~418 KiB
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

Output is a per-case `check`-style report — **69 checks** on the current `main`. Coverage:

- ZIP entry parsing (stored + deflated + ZIP64)
- Path traversal, absolute paths, backslash, Windows drive letters
- Symbolic links, FIFOs, encrypted entries, bad CRC, truncated archives, non-ZIP files
- Limits: `entries`, `total_bytes`, `file_bytes`, `ratio`, `depth`, `name_len`
- Conflict policies: `fail` / `overwrite` / `merge`
- Cancellation in every phase
- **Large-file profile** — `medium_bomb.zip` (ratio ≈ 238) is rejected under default caps and accepted under large caps; lowered large caps still enforce.

## Project layout

```
.
├── Makefile                      # PS5 + Linux builds
├── install-libmicrohttpd.sh      # one-shot dependency installer
├── gen-asset-module.py           # embeds assets/* as gzip-compressed C arrays
├── assets/                       # HTML / CSS / JS / icons / param.json
├── src/                          # C payload sources
│   ├── main.c  websrv.c  filemgr.c        # entry, HTTP frontend, task model
│   ├── upload.c  download.c               # stream handlers
│   ├── extract.c  zip_extract.{c,h}       # /api/extract + standalone engine
│   └── app_installer.c                    # PS5 Media launcher installer
├── third_party/                  # vendored: zlib, minizip-ng
├── tests/                        # POSIX/host test suite
│   ├── test_zip_extract.c
│   ├── make_fixtures.py          # regenerate test fixtures
│   ├── run-tests.sh              # one-shot runner
│   ├── compat/                   # tiny Win32/MSYS shims
│   └── fixtures/                 # generated test ZIPs
├── docs/screenshots/             # README screenshot images
├── THIRD_PARTY_NOTICES           # bundled-library credits
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
- **`err_extract_entry_too_large`** — default ZIP caps are 64 GiB per entry / 200:1 ratio. Confirm the large-file prompt (appears for archives > 60 GiB on disk), shrink the archive, or pass `large=1` directly to the API.

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

## License

The project is distributed under **GPLv3 or later**, matching the GPLv3+ projects used as implementation references. See [`LICENSE`](./LICENSE).

Third-party projects retain their own licenses. Do not copy assets or source from the credited projects into another distribution without preserving the corresponding license notices.

If distributing binaries, comply with the LGPL terms for `libmicrohttpd` in addition to this project's GPL license. The vendored `zlib` and `minizip-ng` sources are distributed under the zlib license; retain the copyright notices in `third_party/zlib/LICENSE` and `third_party/minizip-ng/LICENSE` when redistributing binaries built with this feature.

## Disclaimer

Unofficial homebrew software. Runs only on jailbroken PS5 consoles. Use at your own risk — the authors are not responsible for damage, data loss, account action or warranty impact. Do not redistribute Sony-proprietary content. Under GPLv3+, modified redistributions must publish their sources.
