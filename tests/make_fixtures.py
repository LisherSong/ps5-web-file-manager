#!/usr/bin/env python3
"""Generate the ZIP fixtures used by test_zip_extract."""

import os
import shutil
import stat
import struct
import sys
import time as _time
import zipfile
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "fixtures")

# Force every ZIP entry's date_time to a fixed value (1980-01-01 00:00:00)
# so generated archives are byte-stable across runs. Without this fix,
# Python 3.13's zipfile.writestr() passes time.localtime(time.time())[:6]
# into ZipInfo(...) when the caller supplies a string arcname, which makes
# every fixture differ each run and the git diff stat balloons on every
# "regenerate fixtures" pass. We swap the zipfile module's `time` symbol
# for a fake that always returns the same struct_time; the fake also stubs
# `time.time` since zipfile.writestr chains localtime(time.time()).
_FIXED_DT = (1980, 1, 1, 0, 0, 0)
zipfile.time = type("_FakeTimeMod", (), {
    "localtime": staticmethod(lambda *_a, **_k: _time.struct_time(_FIXED_DT + (0, 1, 0))),
    "time":      staticmethod(lambda *_a, **_k: 0.0),
})()


def fresh():
    if os.path.isdir(OUT):
        shutil.rmtree(OUT)
    os.makedirs(OUT)


def path(name):
    return os.path.join(OUT, name)


def basic():
    with zipfile.ZipFile(path("basic.zip"), "w") as zf:
        zf.writestr("root.txt", "root content")
        zf.writestr("dir/nested.txt", "nested content")
        zf.writestr("dir/deep/deeper.txt", "deeper content")
        zi = zipfile.ZipInfo("empty_dir/")
        zi.external_attr = (stat.S_IFDIR | 0o755) << 16
        zi.create_system = 3
        zf.writestr(zi, b"")


def stored():
    with zipfile.ZipFile(path("stored.zip"), "w", zipfile.ZIP_STORED) as zf:
        zf.writestr("stored.txt", "stored content" * 100)


def unicode_names():
    with zipfile.ZipFile(path("unicode.zip"), "w") as zf:
        zf.writestr("中文目录/文件.txt", "unicode content")
        zf.writestr("emoji-\U0001f600.txt", "emoji content")


def zip64():
    """A genuine ZIP64 archive. Python's zipfile only emits zip64 fields when
    sizes exceed 4 GiB (impractical for a fixture), so the layout is written by
    hand: 32-bit size fields are set to 0xFFFFFFFF and the real values live in
    the zip64 extra fields and the zip64 end-of-central-directory record."""
    name = "big.bin"
    data = bytes(range(256)) * 16  # 4096 bytes, deterministic
    name_b = name.encode("utf-8")
    crc = zlib.crc32(data) & 0xFFFFFFFF
    size = len(data)

    def extra_local():
        return struct.pack("<HHQQ", 0x0001, 16, size, size)

    def extra_central(offset):
        return struct.pack("<HHQQQ", 0x0001, 24, size, size, offset)

    out = bytearray()

    # Local file header (stored, sizes deferred to zip64 extra field).
    local_offset = 0
    el = extra_local()
    out += struct.pack("<IHHHHHIIIHH", 0x04034B50, 45, 0, 0, 0, 0, crc,
                       0xFFFFFFFF, 0xFFFFFFFF, len(name_b), len(el))
    out += name_b + el + data

    # Central directory header.
    cd_offset = len(out)
    ec = extra_central(local_offset)
    out += struct.pack("<IHHHHHHIIIHHHHHII", 0x02014B50, 45, 45, 0, 0, 0, 0,
                       crc, 0xFFFFFFFF, 0xFFFFFFFF, len(name_b), len(ec), 0,
                       0, 0, 0, 0xFFFFFFFF)
    out += name_b + ec
    cd_size = len(out) - cd_offset

    # ZIP64 end of central directory record.
    zip64_eocd_offset = len(out)
    out += struct.pack("<IQHHIIQQQQ", 0x06064B50, 44, 45, 45, 0, 0, 1, 1,
                       cd_size, cd_offset)

    # ZIP64 end of central directory locator.
    out += struct.pack("<IIQI", 0x07064B50, 0, zip64_eocd_offset, 1)

    # End of central directory record (offsets deferred to zip64).
    out += struct.pack("<IHHHHIIH", 0x06054B50, 0, 0, 1, 1, 0xFFFFFFFF,
                       0xFFFFFFFF, 0)

    with open(path("zip64.zip"), "wb") as fh:
        fh.write(bytes(out))


def traversal():
    with zipfile.ZipFile(path("traversal.zip"), "w") as zf:
        zf.writestr("ok.txt", "ok")
        zf.writestr("../evil.txt", "evil")


def traversal_backslash():
    with zipfile.ZipFile(path("traversal_bs.zip"), "w") as zf:
        zf.writestr("ok.txt", "ok")
        zf.writestr("..\\evil.txt", "evil")


def absolute():
    with zipfile.ZipFile(path("absolute.zip"), "w") as zf:
        zf.writestr("/tmp/evil.txt", "evil")


def drive_letter():
    with zipfile.ZipFile(path("drive.zip"), "w") as zf:
        zf.writestr("C:/evil.txt", "evil")


def duplicate():
    with zipfile.ZipFile(path("duplicate.zip"), "w") as zf:
        zf.writestr("a.txt", "first")
        zf.writestr("a.txt", "second")


def file_dir_clash():
    with zipfile.ZipFile(path("clash.zip"), "w") as zf:
        zf.writestr("a", "file")
        zf.writestr("a/b.txt", "child")


def symlink_entry():
    with zipfile.ZipFile(path("symlink.zip"), "w") as zf:
        zf.writestr("ok.txt", "ok")
        zi = zipfile.ZipInfo("link")
        zi.create_system = 3
        zi.external_attr = (stat.S_IFLNK | 0o777) << 16
        zf.writestr(zi, "/etc/passwd")


def fifo_entry():
    with zipfile.ZipFile(path("fifo.zip"), "w") as zf:
        zi = zipfile.ZipInfo("pipe")
        zi.create_system = 3
        zi.external_attr = (stat.S_IFIFO | 0o644) << 16
        zf.writestr(zi, b"")


def encrypted():
    with zipfile.ZipFile(path("encrypted.zip"), "w") as zf:
        zf.writestr("secret.txt", "secret")
    data = bytearray(open(path("encrypted.zip"), "rb").read())
    idx = data.find(b"PK\x03\x04")
    if idx < 0:
        raise SystemExit("local header not found")
    data[idx + 6] |= 0x01  # general purpose bit 0 = encrypted
    cd = data.find(b"PK\x01\x02")
    data[cd + 8] |= 0x01
    open(path("encrypted.zip"), "wb").write(bytes(data))


def bad_crc():
    with zipfile.ZipFile(path("bad_crc.zip"), "w") as zf:
        zf.writestr("data.bin", "x" * 4096)
    data = bytearray(open(path("bad_crc.zip"), "rb").read())
    cd = data.find(b"PK\x01\x02")
    if cd < 0:
        raise SystemExit("central directory not found")
    data[cd + 16:cd + 20] = b"\xde\xad\xbe\xef"
    open(path("bad_crc.zip"), "wb").write(bytes(data))


def truncated():
    with zipfile.ZipFile(path("full.zip"), "w") as zf:
        zf.writestr("data.txt", "y" * 4096)
    data = open(path("full.zip"), "rb").read()
    open(path("truncated.zip"), "wb").write(data[:-40])


def not_a_zip():
    open(path("notazip.zip"), "wb").write(b"this is definitely not a zip file")


def bomb():
    with zipfile.ZipFile(path("bomb.zip"), "w", zipfile.ZIP_DEFLATED) as zf:
        zf.writestr("bomb.bin", "A" * (4 * 1024 * 1024))


def medium_bomb():
    """1 MiB of 0..255 cycled, which deflate squeezes to ~4.4 KiB
    (ratio ~238). Sits between the default cap (200) and the large cap
    (1000) so the default profile rejects it and the large profile
    accepts it. Used by the large-profile host test."""
    with zipfile.ZipFile(path("medium_bomb.zip"), "w",
                         zipfile.ZIP_DEFLATED) as zf:
        zf.writestr("medium.bin", bytes(range(256)) * 4096)


def many_files():
    with zipfile.ZipFile(path("many.zip"), "w", zipfile.ZIP_DEFLATED) as zf:
        for i in range(500):
            zf.writestr("many/f%03d.txt" % i, "%d" % i)


def conflict_source():
    """A zip whose top level collides with an existing destination layout."""
    with zipfile.ZipFile(path("conflict.zip"), "w") as zf:
        zf.writestr("shared.txt", "from zip")
        zf.writestr("shareddir/inner.txt", "inner from zip")
        zf.writestr("shareddir/added.txt", "added from zip")


def rar_fixtures():
    """Generate RAR fixtures if a rar/7z writer is available.

    We intentionally do not depend on a rar binary being installed in the
    host test environment; when neither `rar` nor `7z` is present we leave
    1-byte placeholders so that tests/test_rar_extract.c can still hit its
    "not a real archive" branches. See docs/HANDOVER.md for the manual
    fixture procedure used in CI on a developer workstation that has WinRAR.
    """
    import shutil
    import subprocess
    import tempfile

    candidates = []
    for cmd in ("rar", "7z", "7za"):
        if shutil.which(cmd):
            candidates.append(cmd)

    staging_dir = tempfile.mkdtemp(prefix="wfm-rar-fixtures-")
    try:
        # Build a small directory we can compress into a RAR.
        staging_root = os.path.join(staging_dir, "stage")
        os.makedirs(staging_root)
        with open(os.path.join(staging_root, "root.txt"), "wb") as f:
            f.write(b"rar root content\n")
        nested = os.path.join(staging_root, "dir")
        os.makedirs(nested)
        with open(os.path.join(nested, "nested.txt"), "wb") as f:
            f.write(b"rar nested content\n")

        out = path("basic.rar")
        ok = False
        for cmd in candidates:
            args = [cmd, "a", "-r", "-ep1", out,
                    os.path.join(staging_root, "root.txt"),
                    os.path.join(staging_root, "dir")]
            # 7z uses -t7z / -rr differently; for the purposes of a smoke
            # fixture we only need any small valid RAR.
            try:
                rc = subprocess.call(args, stdout=subprocess.DEVNULL,
                                     stderr=subprocess.DEVNULL)
                if rc == 0 and os.path.exists(out) and os.path.getsize(out) > 16:
                    print("rar_fixtures: built %s via %s" % (out, cmd))
                    ok = True
                    break
            except Exception:
                pass
        if not ok:
            # Placeholder: the test suite only needs a file that dmc_unrar
            # will reject. 8 bytes is far too small to be a valid archive.
            with open(out, "wb") as f:
                f.write(b"placeholder")
    finally:
        shutil.rmtree(staging_dir, ignore_errors=True)


def bigdict():
    """dict-8g.rar -- a RAR5 block whose header asks for an 8 GiB dictionary.

    This one cannot be produced by any compressor, so it is synthesised here:

      * arcread.cpp:871 reads a RAR 5.0 dictionary as
        `0x20000 << ((CompInfo>>10) & 0x0f)` -- FOUR bits, so the format's own
        ceiling is 128 KiB << 15 = exactly 4 GiB, the same as our default
        Cmd->WinSizeLimit (options.cpp:13).  No `-ma5` archive can ever ask for
        more, which is why -m0 store archives never reach CheckWinLimit().
      * Only a RAR7 header (UnpVer==1, five bits, up to UNPACK_MAX_DICT = 64 GiB)
        can -- and Rar.exe 7.23 refuses to create one (`-ma4`, `-ma6`, `-ma7` all
        exit 7; only `-ma5` works).

    So we emit a minimal, valid RAR5 archive by hand: signature, main header,
    one store-method file header (FHFL_CRC32 set), the raw payload, end block.
    CompInfo says UnpVer=1 with 16 dictionary bits (= 8 GiB) plus
    FCI_RAR5_COMPAT, and arcread.cpp:878 then forces the algorithm back to
    VER_PACK5 -- the payload really is stored, so nothing has to decode it.

    Method 0 also means Unpack::Init() is never reached, i.e. the archive
    exercises exactly the gate under test (CheckWinLimit -> uiDictLimit ->
    UCM_LARGEDICT) and never allocates anything multi-gigabyte.

    Sanity check with rarlab's own tools before trusting a change here:
        UnRAR.exe lt dict-8g.rar          -> "-md=8g"
        UnRAR.exe t -mdx12g dict-8g.rar   -> all OK
    Without -mdx UnRAR refuses it exactly as we do ("8 GB dictionary exceeds the
    4 GB limit and needs more than 8 GB of memory").
    """
    name = b"hello.txt"
    data = b"".join(b"line %04d dictionary probe payload\n" % i for i in range(200))
    comp_info = 1 | (16 << 10) | 0x00100000  # UnpVer=1, method=0, 8 GiB, RAR5 compat

    def vint(v):
        out = bytearray()
        while True:
            c = v & 0x7F
            v >>= 7
            out.append(c | 0x80 if v else c)
            if not v:
                return bytes(out)

    def block(htype, flags, payload, data_size=None):
        # The HFL_DATA size lives in the block header prologue, right after the
        # flags -- it is not part of the per-type payload (arcread.cpp:710).
        hd = vint(htype) + vint(flags)
        if data_size is not None:
            hd += vint(data_size)
        hd += payload
        size = vint(len(hd))
        # rawread.cpp:185 GetCRC50() == zlib.crc32 over (size field + header data)
        crc = zlib.crc32(size + hd) & 0xFFFFFFFF
        return struct.pack("<I", crc) + size + hd

    main_hdr = block(1, 0x04, vint(0))                      # HEAD_MAIN, ArcFlags=0
    file_hdr = block(2, 0x02,                               # HEAD_FILE, HFL_DATA
                     vint(0x0004) +                         # FileFlags: FHFL_CRC32
                     vint(len(data)) +                      # UnpSize
                     vint(0) +                              # FileAttr
                     struct.pack("<I", zlib.crc32(data) & 0xFFFFFFFF) +
                     vint(comp_info) +
                     vint(0) +                              # HostOS: Windows
                     vint(len(name)) + name,
                     data_size=len(data))
    end_hdr = block(5, 0x00, vint(0))                       # HEAD_ENDARC

    with open(path("dict-8g.rar"), "wb") as f:
        f.write(b"Rar!\x1a\x07\x01\x00" + main_hdr + file_hdr + data + end_hdr)


def main():
    fresh()
    for fn in (basic, stored, unicode_names, zip64, traversal,
               traversal_backslash, absolute, drive_letter, duplicate,
               file_dir_clash, symlink_entry, fifo_entry, encrypted, bad_crc,
               truncated, not_a_zip, bomb, medium_bomb, many_files,
               conflict_source, rar_fixtures, bigdict):
        fn()
    print("fixtures written to %s" % OUT)
    return 0


if __name__ == "__main__":
    sys.exit(main())
