#!/usr/bin/env python3
"""Generate the ZIP fixtures used by test_zip_extract."""

import os
import shutil
import stat
import struct
import sys
import zipfile
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "fixtures")


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


def main():
    fresh()
    for fn in (basic, stored, unicode_names, zip64, traversal,
               traversal_backslash, absolute, drive_letter, duplicate,
               file_dir_clash, symlink_entry, fifo_entry, encrypted, bad_crc,
               truncated, not_a_zip, bomb, medium_bomb, many_files,
               conflict_source):
        fn()
    print("fixtures written to %s" % OUT)
    return 0


if __name__ == "__main__":
    sys.exit(main())
