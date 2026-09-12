#!/usr/bin/env python3
"""Builds split-archive fixtures for the multi-volume extraction tests.

Output goes to tests/fixtures-split/ and covers the three naming conventions
the engine recognises, plus two broken sets used to check the error messages:

  plain.zip.001 .002 .003      byte split (7-Zip "split to volumes")
  parts.part1.zip ... .part3.zip   byte split (WinRAR zip volumes)
  disks.z01 .z02 .zip          zip split disks, offsets relative to each disk
  broken.zip.001               first volume only, the rest missing
  gap.zip.001 .gap.zip.003     volume 2 missing

The byte-split sets are produced by slicing one ordinary archive. The
"disks" set is built by hand: the central directory is rewritten so that every
entry records the disk it starts on and an offset relative to that disk, which
is what a real zip split disk archive looks like (APPNOTE 4.4.11).
"""

import io
import os
import shutil
import struct
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
# The test binary receives tests/fixtures as its fixture directory, and
# make_fixtures.py has already rebuilt that tree by the time this script runs,
# so the split sets are added there (only our own files are replaced).
OUT = os.path.join(HERE, "fixtures")

OWN_PREFIXES = ("plain.zip.", "parts.part", "disks.", "broken.zip.",
                "gap.zip.", "split_single.zip")

ENTRIES = [
    ("readme.txt", b"split archive fixture\n" * 20),
    ("sub/data.bin", bytes(range(256)) * 40),
    ("sub/deep/more.bin", b"ABCD" * 3000),
    ("tail.bin", bytes(reversed(range(256))) * 30),
]


def build_plain_zip():
    """One ordinary archive; ZIP_STORED keeps the layout predictable."""
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_STORED) as zf:
        for name, data in ENTRIES:
            info = zipfile.ZipInfo(name, date_time=(2026, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_STORED
            zf.writestr(info, data)
    return buf.getvalue()


def read_central_directory(blob):
    """Returns (cd_offset, cd_size, count, [(name, local_offset, cd_pos)])."""
    eocd = blob.rfind(b"PK\x05\x06")
    if eocd < 0:
        raise SystemExit("fixture build: no end-of-central-directory record")
    count, cd_size, cd_offset = struct.unpack_from("<HII", blob, eocd + 10)
    entries = []
    pos = cd_offset
    for _ in range(count):
        if blob[pos : pos + 4] != b"PK\x01\x02":
            raise SystemExit("fixture build: bad central directory signature")
        name_len, extra_len, comment_len = struct.unpack_from("<HHH", blob, pos + 28)
        local_offset = struct.unpack_from("<I", blob, pos + 42)[0]
        name = blob[pos + 46 : pos + 46 + name_len].decode("utf-8")
        entries.append((name, local_offset, pos))
        pos += 46 + name_len + extra_len + comment_len
    return cd_offset, cd_size, count, entries


def split_into_disks(blob, parts=3):
    """Rewrites offsets so the archive looks like a real zip split disk set.

    Entries are packed into `parts - 1` disks; the last disk carries whatever
    is left plus the central directory and the end record.
    """
    cd_offset, _cd_size, count, entries = read_central_directory(blob)
    entries.sort(key=lambda e: e[1])

    # Local header size = fixed part + name + extra, so an entry ends where the
    # next one begins (or at the central directory for the last one).
    limits = []
    for index, (_name, local_offset, _cd_pos) in enumerate(entries):
        end = entries[index + 1][1] if index + 1 < len(entries) else cd_offset
        limits.append((local_offset, end))

    target = max(1, cd_offset // (parts - 1))
    disk_of_entry = []
    disk_starts = [0]
    current = 0
    for local_offset, end in limits:
        if (end - disk_starts[current]) > target and current < parts - 2:
            current += 1
            disk_starts.append(local_offset)
        disk_of_entry.append(current)

    out = bytearray(blob)
    # Central directory entries: disk number + offset relative to that disk.
    for (name, local_offset, cd_pos), disk in zip(entries, disk_of_entry):
        struct.pack_into("<H", out, cd_pos + 34, disk)
        struct.pack_into("<I", out, cd_pos + 42, local_offset - disk_starts[disk])

    last_disk = current
    eocd = len(blob) - 22
    struct.pack_into("<H", out, eocd + 4, last_disk)      # this disk
    struct.pack_into("<H", out, eocd + 6, last_disk)      # disk with the cd
    struct.pack_into("<H", out, eocd + 8, count)          # entries on this disk
    struct.pack_into("<H", out, eocd + 10, count)         # entries in total
    struct.pack_into("<I", out, eocd + 16, cd_offset - disk_starts[last_disk])

    bounds = disk_starts[1:] + [len(blob)]
    return [bytes(out[a:b]) for a, b in zip(disk_starts, bounds)]


def write(path, data):
    with open(path, "wb") as handle:
        handle.write(data)
    print("  %s (%d bytes)" % (os.path.basename(path), len(data)))


def write_set(base, names, chunks):
    for name, chunk in zip(names, chunks):
        write(os.path.join(OUT, name % base), chunk)


def write_disk_set(base, chunks):
    """Split disks are named name.z01, name.z02, ..., name.zip: the last volume
    (the one holding the central directory) drops the numeric suffix."""
    for index, chunk in enumerate(chunks):
        if index == len(chunks) - 1:
            name = "%s.zip" % base
        else:
            name = "%s.z%02d" % (base, index + 1)
        write(os.path.join(OUT, name), chunk)


def main():
    if not os.path.isdir(OUT):
        os.makedirs(OUT)
    for name in os.listdir(OUT):
        if name.startswith(OWN_PREFIXES):
            os.remove(os.path.join(OUT, name))

    plain = build_plain_zip()
    third = (len(plain) + 2) // 3
    chunks = [plain[0:third], plain[third : 2 * third], plain[2 * third :]]
    chunks = [c for c in chunks if c]

    print("byte split (name.zip.NNN):")
    write_set("plain", ["%s.zip.001", "%s.zip.002", "%s.zip.003"], chunks)

    print("byte split (name.partN.zip):")
    write_set("parts", ["%s.part1.zip", "%s.part2.zip", "%s.part3.zip"], chunks)

    print("zip split disks (name.zNN + name.zip):")
    disks = split_into_disks(plain)
    write_disk_set("disks", disks)

    print("broken sets:")
    write(os.path.join(OUT, "broken.zip.001"), chunks[0])
    write(os.path.join(OUT, "gap.zip.001"), chunks[0])
    write(os.path.join(OUT, "gap.zip.003"), chunks[2] if len(chunks) > 2 else b"")

    # A plain archive next to the sets, so the tests can prove the normal
    # single file path still works now that volume detection runs first.
    write(os.path.join(OUT, "split_single.zip"), plain)


if __name__ == "__main__":
    main()
