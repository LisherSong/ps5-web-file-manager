# Changelog

All notable changes to **PS5 Web File Manager** are documented in this file.
The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

> Release artifact for v1.9.3M — **published** (<https://github.com/LisherSong/ps5-web-file-manager/releases/tag/v1.9.3M>),
> after end-to-end validation on a real console:
> `web-file-mgr-v1.9.3M.elf` — size 903 448 bytes (~882 KiB)
> sha256 `8ca47d5aaca75085b32641300cce30fadb7df7749cb6b53d04f129bcecc286b7`
> ELF class 64, little-endian, e_machine `0x003e` (x86_64-sie-ps5)
>
> **The trailing `M` is the fork marker** (Modified build, maintained by
> LisherSong). Upstream owendswang ships plain `vX.Y.Z`, so a version string on
> its own says which of the two you are looking at. The marker rides on
> `VERSION_TAG` rather than on a display-only constant, so `/api/version`, the
> PS5 start-up notification, the stdout banner, the UI footer and the ELF file
> name all gain it in one move — nothing can be forgotten in one of the five.
> A second benefit is that a fork build can no longer collide with an upstream
> artifact of the same upstream version, a mix-up that has already happened
> twice. The footer additionally carries a tooltip spelling the marker out
> (`versionTooltip`, en + zh). This is the first release using the convention;
> the older entries below keep their original plain numbers.
>
> This is the first **published** binary to carry the encrypted-archive work and
> the dictionary-reporting fix (`[v1.9.3M]` below). It was validated end to end
> on a real console before release; the acceptance checklist that was run is
> `docs/DEVICE-TEST-v1.9.3M.md`. The previous release, v1.9.2, contains none of
> this work.
>
> Its size is **unchanged yet again** (903 448 B) although the content grew, for
> the sixth build in a row. Every round of this release has only moved
> `.rodata`, and by less than the 16 KiB section alignment absorbs:
>
> | build | `.rodata` | delta | what changed |
> |---|---|---|---|
> | `v1.9.3` (pre-marker) | 0x025F80 | — | — |
> | `v1.9.3M` + encrypted archives | 0x0260C0 | +0x140 | the `M` marker, its tooltip, re-gzipped assets |
> | `+` upload menu and i18n names | 0x026A40 | +0x980 | the menu, the hint, the new copy |
> | `+` hint move, status clamp, retry key | 0x026B40 | +0x100 | final copy and CSS |
> | `+` menu row highlight fix | 0x026CC0 | +0x180 | the scoped row highlight rules and their comment |
> | `+` always-on extract button | 0x026F00 | +0x240 | the un-hidden button, three disabled reasons, the tooltip fix |
>
> `.text` is byte-for-byte the same size across all six, which is the expected
> shape for a change that touches no C logic. **Never infer "nothing changed"
> from the file size** — compare sections with `readelf -SW`. Each of the six
> carries a different sha256 despite the identical size, so the digest, not the
> byte count, is what identifies a build.
>
> Putting the extract button on screen at all times is not free: it widens the
> resting toolbar by its own width, so the width at which the toolbar wraps onto
> a second row moves out from 1080px to 1190px in Chinese, and from 1230px to
> 1350px in English, where the labels are longer. The console is 1920px wide and
> 1280px still fits in Chinese, so the trade was accepted: the alternative was
> leaving the entry hidden until an archive happened to be selected, which is
> what made it undiscoverable in the first place. The measured threshold is now
> pinned by the headless harness rather than left to be rediscovered.
>
> Release artifact for v1.9.2:
> `web-file-mgr-v1.9.2.elf` — size 870 488 bytes (~850 KiB)
> sha256 `177e90fecf93a0251e83f67884fba4551051be248330b0d70fda8ea732f88e84`
> ELF class 64, little-endian, e_machine `0x003e` (x86_64-sie-ps5)
>
> Behaviourally identical to the published v1.9.1 binary — the only source
> delta is the version literal itself (`VERSION_TAG` in the Makefile, plus the
> UI footer fallback in `assets/main.js`). The build is reproducible: reverting
> those two literals and rebuilding reproduces the v1.9.1 ELF byte for byte, so
> nothing else differs. See [v1.9.2] below for why the version moved at all.
>
> Release artifact for v1.9.1 (superseded — the tag pointed four commits behind
> the tree that actually produced this binary):
> `web-file-mgr-v1.9.1.elf` — size 870 488 bytes (~850 KiB)
> sha256 `24392aff6ddcca4dc0ea969cce356bd693ac52efe8a117d61ee1c814aa43cd07`
> ELF class 64, little-endian, e_machine `0x003e` (x86_64-sie-ps5)
>
> Built on the 7z-complete tree: LZMA SDK decode subset + self-written codec
> chain, 7zAES, and ZIP/RAR/7z volume support. Build-system-only delta vs the
> first v1.9.1 artifact (1 017 864 B): `src/demangle_stub.c` keeps libc++abi's
> Itanium name demangler (105 KiB, only reachable from the uncaught-exception
> path) out of the link, and `-Wl,--icf=all` folds identical functions.
> −15.8% overall with no change to functionality or decompression throughput.
> See `docs/SIZE-OPTIMIZATION.md`.
>
> Release artifact for v1.9:
> `web-file-mgr.elf` — size 919 440 bytes (~897 KiB)
> sha256 `bb8f17e9addc6a9984f611503ca01b51f8984b773d353630da1f25bd1a28a997`
> ELF class 64, little-endian, e_machine `0x003e` (x86_64-sie-ps5)
>
> Source delta vs v1.8.3: RAR engine replaced (dmc_unrar 1.7.0 → rarlab
> UnRAR 7.20.1, `third_party/unrar/` → `third_party/unrar7/`), new
> `src/rar_extract.c` scan/extract implementation, Makefile + host-test
> C++ rules, 5 real RAR fixtures committed. See [v1.9] below.
>
> Release artifact for v1.8.3:
> `web-file-mgr.elf` — size 509 704 bytes (~497 KiB)
> sha256 `fdcf7b09b69e2160e77dfa084c0e890ba0696d4dd478b1d5ff499cdc9f527955`
> ELF class 64, little-endian, e_machine `0x003e` (x86_64-sie-ps5)
>
> Source delta vs v1.8.2: 5 files touched (4 user-facing + 1 build pipeline) —
> see [v1.8.3] below for details.
>
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

## [v1.9.3M] — 2026-09-24

**Encrypted archives now extract end to end — ZIP (both schemes), RAR, and 7z
with an encrypted header.**

Until now every encrypted archive was refused up front, even though the
password field, the prompt and `extract_password` copy had been in place
since v1.9. The gap was in the engines, not the UI:

- **ZIP**: the vendored minizip-ng had been trimmed past its crypto
  backend, so the `-DHAVE_WZAES` / `-DHAVE_PKCRYPT` branches inside the
  (unmodified) `mz_zip.c` had no implementation to call.
- **RAR**: rarlab UnRAR could decrypt, but `RARSetPassword` was never
  called.
- **7z**: `-mhe=on` put the file names and the folder table inside the
  encrypted header, so the archive could not even be listed.

All three are wired now. A missing or wrong password is reported as
`extract_password` (`ZIPX_ERR_PASSWORD` at the engine level), which is the
code the task overlay's existing password prompt already reacts to.

### Changed

- **The version string gains a fork marker: `v1.9.3` → `v1.9.3M`.** Upstream
  owendswang releases are plain `vX.Y.Z`, so the `M` (Modified) is what tells a
  user which of the two they are holding. It is part of `VERSION_TAG` in the
  Makefile, which means `/api/version`, the PS5 start-up notification, the
  stdout banner, the UI footer **and the ELF file name** all carry it at once.
  Because the file name changes too, a fork build can no longer shadow an
  upstream artifact of the same upstream version.
- The UI footer tooltip (`versionTooltip`, en + zh) spells the marker out, so
  `v1.9.3M` is not left unexplained for someone who has not read this file.
  `loadVersion()` only ever rewrites the footer's text, so the tooltip survives
  the `/api/version` round trip.
- **The upload button opens a menu instead of hiding half of itself behind a
  caret.** It used to be a main button plus a small arrow: users read the arrow
  as decoration and never found "upload a folder" at all. One click now lists
  **Upload Files / Upload Folder** (`#uploadMenu`, `role="menu"`), with Escape,
  an outside click and arrow keys handled, and focus moved into the list when it
  opens. The button keeps a caret so it is obvious that a list is coming.
- The footer status line is now clamped to a single line with an ellipsis. It
  is a fixed-height row that also carries the version and the new drag hint, and
  a long "uploading 3/12: some-name.zip" used to wrap to two lines and spill out
  of the 46 px footer.
- **The extract button is always on screen and greys out instead of being
  hidden.** It used to appear only once an extractable archive was selected, so
  the resting toolbar had no extract entry at all — the same discoverability
  problem the upload button was changed for. It is now always present, disabled
  and at 45% opacity whenever the selection cannot be extracted, and its tooltip
  names the reason: *"Select one archive to extract (ZIP / RAR / 7z)"*, the
  existing "please select the main volume" when only a `.partNN.rar` sub-volume
  is selected, and a new "only one archive can be extracted at a time" when
  several are selected — the old code answered *that* case with the main-volume
  message, which was simply the wrong sentence. Because `button:disabled` sets
  `pointer-events: none`, a disabled button cannot be hovered and its tooltip
  never appears at all, so `.extract-action:disabled` restores hit-testing
  without restoring clicks; the parent-directory button needed the same fix
  earlier.
- The extract button's label is the short `extract` ("解压" / "Extract"),
  matching the other toolbar verbs, instead of `extractToCurrent` ("解压到当前
  目录" / "Extract to current folder"). A button that is on screen permanently
  should not also be the widest one in the toolbar, and the target folder is
  still named in the tooltip and in the confirmation dialog. The measured cost
  of the always-on button: the width at which the toolbar wraps onto a second
  row moves from 1080px to 1190px in Chinese and from 1230px to 1350px in
  English, where the labels are longer. 1280px still fits in Chinese and the
  console is 1920px wide.

### Added

- **Encrypted ZIP** — traditional PKWARE ("ZipCrypto", what `zip -e`
  writes) and WinZip AES-128/192/256 (method `99` + the `0x9901` extra
  field, what `7z -mem=AES256` writes), for stored and deflated entries.
- **Encrypted RAR** — `-p` data encryption and `-hp` header encryption.
  `RARSetPassword` now runs right after `RAROpenArchiveEx` and before the
  first `RARReadHeaderEx`, which is the order unrar needs to decrypt a
  RAR5 header.
- `password=` on `/api/extract` now reaches a real decrypt path for both
  engines. An empty or absent value means "no password", so the raw form
  field can be passed straight through.
- **The UI retries a failed extraction with a password.** An
  `extract_password` failure used to end in an error box, which for ZIP and
  RAR meant the password could never be supplied at all — the prompt only
  existed for 7z. The failed task is now re-sent with whatever the user types,
  up to three times, and the remembered request keeps the original conflict
  policy and large-file opt-in. Cancelling or submitting an empty box falls
  back to the previous failure report. 7z keeps its up-front prompt so an
  encrypted header does not cost a wasted scan.
- `extractPasswordRetryAsk` (en + zh) is the retry prompt's wording, distinct
  from the up-front `extractPasswordAsk`.
- `third_party/minizip-ng/src/mz_crypt_wfm.c` — a local crypto provider for
  the trimmed minizip-ng: SHA-1, HMAC-SHA1 and AES-128/192/256, with the
  S-box and the GF(2^8) tables derived on first use so the binary gains no
  new `.rodata` lookup tables. PBKDF2 comes from the vendored `mz_crypt.c`;
  the CSPRNG reads `/dev/urandom` rather than `mz_os_rand()`, which keeps
  `rand`/`srand` out of the import table. Restored verbatim from upstream
  4.2.2: `mz_strm_wzaes.{c,h}`, `mz_strm_pkcrypt.{c,h}`.
- `tests/make-zip-enc-fixtures.bat` and three real fixtures under
  `tests/fixtures-real/` (`enc-zipcrypto.zip`, `enc-aes256.zip`,
  `enc-aes256-store.zip`, password `secret123`).
- **Encrypted 7z headers (`-mhe=on`)** — the last remaining format gap. With
  `-mhe=on` the header is itself a folder holding the file names, the folder
  table and every entry size, so the vendored SDK (whose C decoder has no
  7zAES coder at all) abandons the archive with `SZ_ERROR_UNSUPPORTED` before
  it can list a single entry. `src/sevenz_header.c` now reads the
  `k7zIdEncodedHeader` record, decodes its one folder through the project's
  own 7zAES path (`src/sevenz_chain.c`) and then gives the SDK a small virtual
  `ISeekInStream` in which that record has been replaced by the plaintext —
  the rewritten start header, the decrypted header at the offset the encoded
  one already occupied, and the real archive everywhere else, so every offset
  the archive stores still points where it did. Nothing on disk is written to.
  Archives whose header is only *compressed* (`-mhc=on`, the default) are
  detected from one byte and never touched, and a wrong password comes back as
  `ZIPX_ERR_PASSWORD` like any other encrypted archive.
- **A visible drag-and-drop hint** (`dropUploadHint`, en + zh) in the footer:
  *"Drag files or folders into this window to upload"*. Dropping already worked,
  but nothing but the drop overlay itself ever said so, and that only appears
  once a drag is under way. It is `remote-only`, like the upload button it
  describes, and starts hidden so the console browser never flashes it.
- `uploadFiles` (en + zh) for the menu's file entry, and
  `extractPasswordFirstAsk` — a first-failure prompt that says the archive is
  encrypted rather than blaming a password the user was never asked for.
- `extractSelectArchive` and `extractOneAtATime` (en + zh): the two reasons the
  always-on extract button can be greyed out with nothing useful selected, and
  with several archives selected. The third reason, `extractSelectMainVolume`,
  already existed.

### Fixed

- **Compiler-flag changes now invalidate objects.** `make` cannot see a
  flag change, so adding `-DHAVE_WZAES -DHAVE_PKCRYPT` left the existing
  `mz_zip.o` / `mz_crypt.o` untouched — and since nothing referenced the
  new streams any more, `--gc-sections` dropped the encryption code again
  while the link still reported success (the first build of this change was
  byte-for-byte the published release). The Makefile now records the
  third-party flag set in `ps5-obj/.third_party_cflags` /
  `linux-obj/.third_party_cflags` and rebuilds only when it really changes —
  the same trap the older `LzmaDec.o` rule was working around, generalised.
- `ZIPX_ERR_UNSUPPORTED` no longer covers encryption — it is now "multipart
  or unsupported compression method" only.

- **An oversized archive dictionary is now reported as such.** An archive whose
  dictionary exceeds what the build allows used to fail with
  `extract_entry_too_large` / "A file inside the archive is too large" naming the
  entry that happened to be in flight — for the 8 GiB-dictionary fixture the
  message blamed a 7 KB text file. The dictionary is a property of the archive,
  not of the entry, so it now has its own status (`ZIPX_ERR_LIMIT_DICT`), its own
  i18n code (`extract_dict_too_large`, en + zh) and the real numbers, which
  unrar hands over in the `UCM_LARGEDICT` callback: *"needs 8192 MiB (limit
  4096 MiB)"*.
- The **behaviour is unchanged on purpose**: we still refuse, matching what
  rarlab's own CLI does by default ("8 GB dictionary exceeds the 4 GB limit and
  needs more than 8 GB of memory; use -md8g or -mdx8g"). Answering `1` to
  `UCM_LARGEDICT` would only move the problem into `Unpack::Init()`, which
  allocates the whole dictionary in one block — on a 16 GB console that trades a
  clean error for an OOM-kill of the payload mid-extraction. Note also that
  **RAR 5.0 headers cannot express more than 4 GiB** (four dictionary bits,
  `arcread.cpp:871`), so only RAR7 headers can reach this path at all.
- `tests/test_rar_extract.c:test_dict_limit` + a new synthetic fixture
  `tests/fixtures/dict-8g.rar` (built by `tests/make_fixtures.py:bigdict()`,
  which writes a minimal valid RAR5 archive by hand: no compressor can produce
  such a header, and `Rar.exe 7.23` refuses to create RAR7 archives — `-ma4`,
  `-ma6`, `-ma7` all exit 7). Verified independently with rarlab's own tools:
  `UnRAR lt` reports `-md=8g` and `UnRAR t -mdx12g` extracts it cleanly.

- **`err_extract_unsupported` copy was two releases out of date.** It read
  *"only plain ZIP, single-volume RAR, and 7z are supported"* — the exact
  inverse of what the build does now, because encrypted archives and
  multi-volume RAR are both supported. The label now names what is actually
  accepted (`.zip` / `.rar` / `.7z`, including their multi-volume and
  encrypted forms); the backend's own sentence, which carries the real cause,
  is still appended after it. The same stale wording in both READMEs is
  corrected too. `.rodata` +0x40 (64 B), nothing else changed.

- **The password prompt never appeared for an archive in a non-ASCII folder
  — the user only ever saw the failure alert.** The retry that asks for a
  password was looked up by *path*, and paths are not stable across the wire:
  the server escapes every byte ≥ 0x80 as `\u00XX` when it serialises a name
  and `fs_path_value()` maps those back to raw bytes when it receives one, so
  the string the page holds for a directory and the string the task reports back
  differ for every name that is not pure ASCII. The lookup missed, the retry
  returned false, and the plain "extract failed" box was shown instead — the
  archive had to be extracted again by hand before the prompt would appear. The
  remembered request is now keyed by **task id**, which the server assigns and
  which survives the round trip untouched; the retry also re-sends the path the
  *server* reported rather than the one the page was holding. On-device symptom:
  upload `x.zip` into a Chinese-named folder, choose "upload and extract" → no
  prompt, then the raw error.

- **Entry names in error messages were unreadable mojibake** —
  `解压失败: 密码错误，或压缩包未使用所提供的密码加密: â®…ç§.psd (entry is
  encrypted and no password was given)`. The listing has always translated the
  byte-mapped names back through `decodeFsText()` for display, but
  `backendErrorText()` used the raw `error_arg` — the one place where the name
  matters most. Both `error_arg` and the backend's own sentence now go through
  the same translation, so a GBK name stored inside a ZIP reads as Chinese
  again.

- **An archive with a non-ASCII name inside a non-ASCII folder could not be
  extracted after upload.** `uploadAndExtractFile()` joined the byte-mapped
  directory with the real Unicode file name, producing a mixed path; the server
  only byte-repairs a string when *every* non-ASCII code point in it is ≤ 0xFF,
  so one CJK character made it skip the repair and look for a path that does not
  exist. `encodeFsText()` (the inverse of `decodeFsText()`) now byte-maps the
  name before the join, which is also applied to the path `New Text` hands to
  the editor. Pure-ASCII paths and pure-real-Unicode paths were unaffected,
  which is why this survived until someone hit the mixed case.

- **The upload menu's row highlight painted as a broken shape.** Selecting a
  row drew a 3px blue ring at `outline-offset: 2px` that cleared the panel's
  6px padding, overlapped the row above, and kept the row's own 6px corner
  radius instead of growing with the offset — so the highlight read as a
  detached outline with two arcs hanging off its sides rather than a selected
  row. Two rules were fighting. The ring came from the generic `button:focus`
  rule, which is sized for a 54px toolbar button and was never meant to reach a
  46px list row. Underneath it, the panel's own
  `.upload-menu-list button:hover:not(:disabled)` fill had **never applied at
  all**: it ties on specificity (0,3,1) with the generic
  `button:not(.row-action):hover:not(:disabled)` rule and that one comes later
  in the file, so it won the cascade — hover and focus therefore ended up two
  different colours (`#303945` against `#2b343e`), and a hovered row lit up
  while the keyboard-focused row stayed lit, which is what made two rows look
  selected at once. Both rules are now scoped to the panel id, rows highlight
  by fill alone, and the keyboard cue is a 2px **inset** ring drawn inside the
  row, where it cannot cross the panel edge at any row height.

### Tests

- `tests/test_zip_extract.c` runs each encrypted fixture four ways (no
  password → `PASSWORD`, empty → `PASSWORD`, wrong → `PASSWORD`, correct →
  `ZIPX_OK` with a byte-level content check), plus a case proving the
  limits still apply when a password has been handed over.
- `tests/test_rar_extract.c` exercises `enc-v6.rar` the same way, including
  that nothing is published on the failing paths.
- `tests/test_sevenz_extract.c` runs `aeshe.7z` three ways: no password →
  `ZIPX_ERR_PASSWORD`, wrong password → `ZIPX_ERR_PASSWORD`, correct password
  → success with the content compared byte for byte, and no staging tree left
  behind on any of them.
- `.build/ui_retry_test.mjs` loads the real `assets/main.js` into a stubbed DOM
  and checks the frontend retry flow: the request is remembered with its
  conflict policy and large-file flag, a failure retries with the typed
  password, cancel and empty input give up, and the retry count caps at three.
  It also carries the regression case for the missing prompt: a task whose
  `src`/`dst` come back byte-repaired (a non-ASCII folder) must still be retried,
  and a structural check that no retry entry is keyed by a path. **40 checks,
  0 failures.**
- `.build/ui_upload_menu_test.mjs` is the markup-side counterpart: it asserts
  that every `data-i18n` key in `index.html` exists in both language files, that
  the two language files carry the same keys, that the upload menu and the drag
  hint exist and start hidden, that the removed file/folder split is gone from
  both the markup and the stylesheet, that the click handlers are bound to the
  new ids, and that the classes the markup uses are actually styled. Four of its
  checks pin the row-highlight cascade: the hover and focus rules must be scoped
  to the panel, the row ring must be suppressed, the keyboard cue must be an
  inset shadow, and the unscoped forms must not come back — a rule that silently
  loses the cascade is exactly the kind of thing a static check can still catch.
  A further group covers the extract button, which is now always on screen: the
  markup must not hide it, main.js must never assign to `extractBtn.hidden`, the
  disabled rule must keep the tooltip hoverable, and there must be a message for
  each reason it can be unavailable. One check sweeps the other direction —
  every `t("...")` literal in `main.js` (117 keys) must exist in both language
  files — because a key reached only from script code is invisible to the markup
  sweep, which is how a message goes missing unnoticed.
  **40 checks, 0 failures.** Both scripts are hand-run — the project has no
  browser test runner — but each one exits non-zero on failure.
- `.build/preview_build.py` + `.build/preview_check.mjs` render the real page
  against a fixture API in headless Chromium and assert what a screenshot alone
  cannot: the menu is hidden at rest, opens on click, moves focus into the list,
  reaches the hidden `<input type="file">`, and closes on a choice, an outside
  click and Escape. It reads back the *computed* highlight for a focused, a
  hovered and a keyboard-focused row, which is the only way to settle a cascade
  question — a rule losing to a generic one and a ring leaking out of its
  container both look fine in the source. It is how the three layout/highlight
  mistakes of this round were caught (a hint that pushed the toolbar onto a
  second row, a status line that wrapped out of the footer, and the broken row
  highlight described under Fixed).
- Host totals: **140 ZIP + 37 RAR = 177 checks**, 0 failures.
- 7z totals: **27 cases, 0 failures** (`tests/run-sevenz-tests.sh`), and the
  `KNOWN_GAPS` list that held `aeshe` is now empty — the encrypted-header
  fixture passes through both the folder decoder and the extraction facade.

### Still open

- End-to-end validation of the built ELF on a real console.

## [v1.9.2] — 2026-09-05

**Version-string-only re-release: the tag now points at the tree that produced
the published binary.**

The `v1.9.1` tag sat four commits behind the tree its ELF was built from, so
cloning that tag could not rebuild the published artifact. v1.9.2 is cut from
the right commit. It is functionally identical to the v1.9.1 binary — the only
source delta is the version literal itself (`VERSION_TAG` in the Makefile, plus
the UI footer fallback in `assets/main.js`) — and the build is reproducible:
reverting those two literals reproduces the v1.9.1 ELF byte for byte.

Release artifact: `web-file-mgr-v1.9.2.elf` — 870 488 bytes (~850 KiB), sha256
`177e90fecf93a0251e83f67884fba4551051be248330b0d70fda8ea732f88e84`.

## [v1.9.1] — 2026-09-05

**7z extraction — a third engine — plus a size and throughput pass.**

Added:

- `src/sevenz_extract.{c,h}` — the 7z engine, built on the LZMA SDK 26.03
  decode subset plus the project's own pull-based codec chain
  (`src/sevenz_chain.c`). The SDK's own `SzArEx` path only understands folders
  of up to four coders, which cannot express BCJ2's five — hence the
  self-parsed folder table and the pull-based chain. Dispatch is by extension
  in `src/extract.c`; the three-phase model, the limit profiles and the
  conflict policy are shared with ZIP and RAR, so `.7z` files get the same
  **Extract** button as `.zip` and `.rar`.
- `src/sevenz_volstream.{c,h}` — `.7z.001` / `.z01` byte-split volume sets,
  stitched by name; open the first volume.
- 7zAES content decryption (AES-256-CBC). The frontend asks for the password
  *up front* here, so an unencrypted archive does not pay for a wasted scan.

Performance (decode-only, no functional change):

- LZMA SDK assembly decoder (`Asm/x86/LzmaDecOpt.asm` assembled with jwasm,
  with an automatic pure-C fallback) ≈ 1.26×.
- Single-coder pure-LZMA2 folders decode multi-threaded
  (`Lzma2DecMt` via `src/sevenz_mt.c`, 8 threads) ≈ 1.37×.
- The extract path drops its per-entry `fsync` — publish is rename-only and
  there is no resume feature to protect (≥ 14× measured on an 8000-file
  archive; see `docs/EXTRACTION-PERF.md`).

Build and size:

- `VERSION_TAG` v1.9.1. `src/demangle_stub.c` keeps libc++abi's Itanium name
  demangler (105 KiB, reachable only from the uncaught-exception path) out of
  the link, and `-Wl,--icf=all` folds identical functions: −15.8% overall with
  no functional or throughput change, 1 017 864 B → 870 488 B. Measured in
  `docs/SIZE-OPTIMIZATION.md`.

Known gap at the time: 7z `-mhe=on` encrypted headers — closed in v1.9.3M with
`src/sevenz_header.c`.

Tests: **163 checks** (ZIP 108 + RAR 27 + 7z 28), 0 failures, plus a successful
PS5 cross-compile.

## [v1.9] — 2026-09-05

**RAR engine replaced: rarlab UnRAR 7.20.1 (v6 / multi-volume / decryption-capable).**

The vendored dmc_unrar 1.7.0 only dispatched RAR5 compression version 5
(`switch(file->version)` case `0x5000`). Archives written by WinRAR 6.x /
7.x (algorithm string `v6`, version field `0x5001`) hit the default branch
and surfaced as "corrupt archive" — confirmed on a real `v6:8M` archive.
v1.9 swaps in the official rarlab UnRAR source (7.20.1) via its
C-compatible DLL API, compiled as a static library (`-DRARDLL`, PS5 uses
the toolchain's default `libc++`).

What this enables:

- **RAR5 "v6" compression** (WinRAR 6/7 archives) — the v1.9 trigger.
- **Multi-volume RAR** (`.partNN.rar`): unrar stitches volumes by name when
  all parts sit next to the opened volume. Select the first volume
  (`name.part1.rar`) and extract as usual.
- RAR4 and older RAR5 remain supported.
- The engine *can* decrypt encrypted archives (`RARSetPassword`), but the
  password channel (API + UI) is not wired yet — encrypted headers/entries
  still fail up front with `err_extract_unsupported`. Planned for a follow-up.

Engine changes:

- `src/rar_extract.c` rewritten to unrar's sequential DLL API
  (`RAROpenArchiveEx → RARReadHeaderEx → RARProcessFile`); scan and extract
  each re-open the archive. Multi-volume continuation segments
  (`RHDF_SPLITBEFORE`) are advanced but not re-counted/deduped.
- The three-phase scan → staging → publish/rollback machinery is unchanged.
- Bug fix: `normalize_name()` no longer clears the caller's directory flag
  (a real v6 archive with an explicit directory header after its files
  tripped the duplicate detector).

Build & test:

- Makefile: `.cpp` rules for the unrar RARDLL source set (49 files, mirrors
  `UnRARDll.vcxproj`); links through the C++ driver; `VERSION_TAG` v1.9.
- tests: 5 real RAR fixtures committed under `tests/fixtures-real/`
  (generated with `tests/make-rar-fixtures.bat` + WinRAR); new happy-path
  checks extract a real v6 archive, verify files on disk, auto-merge a
  3-volume split, and reject encrypted archives. Total: **70 ZIP + 24 RAR
  = 94 checks** (up from 70 + 14; the old 14 RAR checks never ran a real
  archive).

Credits: unrar (c) Alexander Roshal, freeware license — see
`third_party/unrar7/license.txt` and `THIRD_PARTY_NOTICES`.

## [v1.8.3] — 2026-09-05

**Hotfix: "Upload and extract" now accepts `.rar` files.**

The "upload and extract" entry was hard-coded to accept only `.zip`,
even though the server-side dispatch in `src/extract.c:79` already
correctly routes `.rar` to `rar_extract()`. v1.8.3 fixes the frontend
filter so users can select a single-volume plaintext `.rar` from the
file picker and have it uploaded + extracted in one click (the same
flow that already worked for `.zip`).

What this enables:

- Choose a single-volume `.rar` from "Upload and extract"
- Server extracts it via the existing `rar_extract()` engine
- Uploaded `.rar` is auto-deleted after a successful extract (same as
  `.zip` since v1.7)

What this does **not** enable (planned for v1.9.0):

- **Multi-volume RAR** (e.g. `name.part01.rar` + `name.part02.rar` …)
- **Encrypted RAR** (password-protected headers or entries)

Both still return `extract_unsupported` "single-volume RAR only" /
"encrypted RAR is not supported; please extract on a PC first" — see
the underlying engine limit in `third_party/unrar/dmc_unrar` (GPL-2.0,
1.7.0). v1.9 will swap the vendor to **opello/unrar 7.20.1** (UnRAR
License) which natively supports both.

Changed:

- `assets/index.html` — `<input id="uploadZip" accept>` now lists
  `.rar` + the two RAR MIME types next to the existing ZIP entries.
- `assets/main.js:2340` — `/\.zip$/i` → `/\.(zip|rar)$/i` (the upload
  pre-check), plus a local `isRar` flag so the next step branches.
- `assets/lang-en.js` — `extractUploadConfirm`: "uploaded ZIP" →
  "uploaded archive".
- `assets/lang-zh.js` — `extractUploadConfirm` & `extractLargeAsk`
  drop the "ZIP" wording so the copy reads sensibly for RAR uploads.
- `assets/main.js:38` — `APP_VERSION` `"v1.7"` → `"v1.8.3"` (footer
  version string had been hard-coded to v1.7 since the frontend was
  first imported; it no longer misleads about which build is running).

No backend changes — the server side was already correct. No test
changes — the existing RAR happy-path test in `tests/test_rar_extract.c`
passes against the same backend.

Build pipeline (also v1.8.3):

- `.build/build-elf.sh` step 6 "no source change → skip make" check now
  also watches `assets/*` and `gen-asset-module.py`, not just `src/*.c`
  and the `Makefile`. Without this, v1.8.3 (which touched no backend,
  only frontend assets feeding `gen/*.c`) was misclassified as "no
  change" and `make` was skipped — the result was that the v1.8.2 ELF
  was reported as v1.8.3 with the same sha256. With this fix, only
  frontend changes correctly trigger a rebuild. Users running the WSL
  build need to re-copy `.build/build-elf.sh` to `/home/song/build-elf.sh`
  (canonical source is on the Windows side).

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
[`third_party/unrar7/VENDORED.md`](./third_party/unrar7/VENDORED.md) — v1.8
shipped it at `third_party/unrar/VENDORED.md`; the directory was renamed in
v1.9 when the engine was replaced.

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
