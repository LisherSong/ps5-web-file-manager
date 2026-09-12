#!/usr/bin/env python3
"""Generate real .7z fixtures for the host test suite.

The fixtures are produced by an actual 7-Zip binary so that the archives the
engine has to read are genuine, not hand-rolled.  The tool is looked up in this
order:

  1. ``--tool <path>``
  2. ``$SEVENZ_TOOL``
  3. the full ``7za.exe`` from the "7-Zip Extra" package under ``.build/7zdl/``
  4. the reduced ``7zr.exe`` under ``.build/7zdl/``
  5. ``7z`` / ``7zr`` / ``7za`` on PATH

Both binaries come from https://github.com/ip7z/7zip/releases (7-Zip is public
domain).  The reduced ``7zr.exe`` has **no PPMd encoder**, so that fixture is
skipped (with a warning) unless a full build is available.

Output goes to ``tests/fixtures-7z/``.  Only files this script owns are removed
on a re-run (identified by an ``OWN_`` prefix list), never the whole directory.

    python tests/make_sevenz_fixtures.py [--big]
"""

from __future__ import annotations

import argparse
import os
import random
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
OUT = os.path.join(HERE, "fixtures-7z")
SRC = os.path.join(OUT, "_src")

# Everything this script may delete on a re-run.
OWN_PREFIXES = (
    "store", "lzma", "lzma2", "ppmd", "bcj", "delta", "aes", "vol", "utf8",
    "big", "empty", "_src",
)

PASSWORD = "Secret123"


# --------------------------------------------------------------------------
# tool discovery / process helpers
# --------------------------------------------------------------------------

def find_tool(explicit: str | None) -> str:
    if explicit:
        if os.path.exists(explicit):
            return explicit
        raise SystemExit(f"7z tool not found: {explicit}")

    env = os.environ.get("SEVENZ_TOOL")
    if env and os.path.exists(env):
        return env

    # Prefer the full 7za.exe (has PPMd); fall back to the reduced 7zr.exe.
    for rel in ("7zdl/extra/x64/7za.exe", "7zdl/extra/7za.exe", "7zdl/7zr.exe"):
        local = os.path.join(ROOT, ".build", rel.replace("/", os.sep))
        if os.path.exists(local):
            return local

    for name in ("7z", "7zr", "7za"):
        found = shutil.which(name)
        if found:
            return found

    raise SystemExit(
        "no 7z tool found; pass --tool, set SEVENZ_TOOL, or drop 7zr.exe into "
        ".build/7zdl/"
    )


def run(tool: str, args: list[str], optional: bool = False) -> bool:
    """Run the 7z tool.  Returns True on success; raises unless ``optional``."""
    proc = subprocess.run(
        [tool] + args, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT
    )
    if proc.returncode == 0:
        return True
    out = proc.stdout.decode("utf-8", "replace")
    # 7-Zip writes its diagnostics in the OEM code page; try the common ones.
    for enc in ("gbk", "cp936", "utf-8"):
        try:
            out = proc.stdout.decode(enc)
            break
        except UnicodeDecodeError:
            continue
    if optional:
        print(f"  !! skipped: {' '.join(args[:4])}\n     {out.strip()[-200:]}")
        return False
    raise SystemExit(
        f"7z failed ({proc.returncode}): {tool} {' '.join(args)}\n{out}"
    )


def win(path: str) -> str:
    """7zr.exe is a native Windows binary: hand it a Windows path."""
    return path.replace("/", "\\")


# --------------------------------------------------------------------------
# source tree
# --------------------------------------------------------------------------

def x86ish(size: int) -> bytes:
    """Bytes that look like x86 code so BCJ/BCJ2 filters are actually useful."""
    out = bytearray()
    rnd = random.Random(1234)
    while len(out) < size:
        op = rnd.choice((0xE8, 0xE9, 0xE8, 0xE9, 0x0F, 0x8B, 0xC3, 0x90))
        out.append(op)
        if op in (0xE8, 0xE9):
            out += rnd.randrange(0, 1 << 24).to_bytes(4, "little")
        elif op == 0x0F:
            out.append(0x8B)
        else:
            out += bytes(rnd.randrange(256) for _ in range(rnd.randrange(1, 6)))
    return bytes(out[:size])


def build_source(sizes: dict[str, int]) -> None:
    os.makedirs(os.path.join(SRC, "sub"), exist_ok=True)
    rnd = random.Random(7)

    with open(os.path.join(SRC, "readme.txt"), "wb") as fh:
        fh.write(b"sevenz fixture\n" * 20)

    with open(os.path.join(SRC, "binary.bin"), "wb") as fh:
        fh.write(bytes(rnd.randrange(256) for _ in range(sizes["binary"])))

    # Highly compressible: exercises the LZMA/LZMA2/PPMd long-range paths.
    with open(os.path.join(SRC, "zeros.bin"), "wb") as fh:
        fh.write(b"\0" * sizes["zeros"])

    with open(os.path.join(SRC, "sub", "nested.txt"), "wb") as fh:
        fh.write(b"nested entry\n" * 100)

    with open(os.path.join(SRC, "sub", "code.bin"), "wb") as fh:
        fh.write(x86ish(sizes["code"]))

    # Non-ASCII names must survive the UTF-16 name table round trip.
    with open(os.path.join(SRC, "sub", "\u4e2d\u6587-\u30c6\u30b9\u30c8.txt"), "wb") as fh:
        fh.write("unicode name\n".encode("utf-8") * 30)


def write_big_file(path: str, size: int) -> None:
    """A large, semi-compressible file: forces multi-chunk streaming decode."""
    rnd = random.Random(99)
    block = bytes(rnd.randrange(256) for _ in range(64 * 1024))
    with open(path, "wb") as fh:
        written = 0
        while written < size:
            n = min(len(block), size - written)
            fh.write(block[:n])
            written += n


# --------------------------------------------------------------------------
# archives
# --------------------------------------------------------------------------

# name -> (extra 7z arguments, takes password, required)
VARIANTS: list[tuple[str, list[str], bool, bool]] = [
    ("store",   ["-mx0"], False, True),
    ("lzma2",   ["-m0=lzma2", "-mx5"], False, True),
    ("lzma",    ["-m0=lzma"], False, True),
    # The reduced 7zr.exe has no PPMd encoder, so this one is best-effort.
    ("ppmd",    ["-m0=ppmd"], False, False),
    ("bcj",     ["-m0=bcj", "-m1=lzma2"], False, True),
    ("delta",   ["-m0=delta:4", "-m1=lzma2"], False, True),
    ("bcj2",    ["-m0=bcj2", "-m1=lzma2", "-m2=lzma2", "-m3=lzma2", "-m4=lzma2"], False, True),
    ("aes",     ["-m0=lzma2", "-mx5"], True, True),
    ("utf8",    ["-m0=lzma2", "-mx5"], False, True),
]


def build_archives(tool: str, big: bool) -> None:
    for name, extra, secret, required in VARIANTS:
        arch = os.path.join(OUT, f"{name}.7z")
        if os.path.exists(arch):
            os.remove(arch)
        args = ["a", "-t7z", win(arch), win(SRC), "-y"] + extra
        if secret:
            args.append(f"-p{PASSWORD}")
        if name == "aes":
            # Header stays readable: only the streams are encrypted.
            args.append("-mhe=off")
        run(tool, args, optional=not required)

    # Encrypted header (-mhe=on): the archive cannot even be listed without
    # the password, so the engine must ask for it up front.
    run(tool, ["a", "-t7z", win(os.path.join(OUT, "aeshe.7z")), win(SRC),
               "-y", "-m0=lzma2", "-mx5", f"-p{PASSWORD}", "-mhe=on"])

    # Multi-volume (-v): 100 KiB parts force the fixture across several files.
    vols = os.path.join(OUT, "vol.7z")
    for stale in sorted(os.listdir(OUT)):
        if stale.startswith("vol.7z"):
            os.remove(os.path.join(OUT, stale))
    run(tool, ["a", "-t7z", win(vols), win(SRC), "-y", "-m0=lzma2", "-mx5",
               "-v100k"])

    if big:
        bigsrc = os.path.join(OUT, "_big")
        os.makedirs(bigsrc, exist_ok=True)
        write_big_file(os.path.join(bigsrc, "big.bin"), 96 * 1024 * 1024)
        run(tool, ["a", "-t7z", win(os.path.join(OUT, "big.7z")), win(bigsrc),
                   "-y", "-m0=lzma2", "-mx1"])
        shutil.rmtree(bigsrc, ignore_errors=True)


# --------------------------------------------------------------------------

def clean() -> None:
    os.makedirs(OUT, exist_ok=True)
    for entry in os.listdir(OUT):
        if entry.startswith(OWN_PREFIXES):
            path = os.path.join(OUT, entry)
            if os.path.isdir(path):
                shutil.rmtree(path, ignore_errors=True)
            else:
                os.remove(path)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--tool", help="path to 7zr.exe / 7z.exe / 7zr")
    ap.add_argument("--big", action="store_true",
                    help="also build a ~96 MiB single-file archive")
    opts = ap.parse_args()

    tool = find_tool(opts.tool)
    print(f"7z tool: {tool}")

    clean()
    build_source({"binary": 300_000, "zeros": 2_000_000, "code": 400_000})
    build_archives(tool, opts.big)

    print(f"fixtures written to {OUT}")
    for name in sorted(os.listdir(OUT)):
        path = os.path.join(OUT, name)
        if os.path.isfile(path):
            print(f"  {name} ({os.path.getsize(path)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
