#!/usr/bin/env python3
"""Performance baseline for the extraction engines.

    python tests/bench_driver.py            # 82 MiB fixture, fast
    python tests/bench_driver.py --big      # 320 MiB fixture, accurate
    python tests/bench_driver.py --runs 5

Times our facades (src/zip_extract.c, src/sevenz_extract.c) against the two
external references that matter: the vendored SDK's own SzArEx path, and the
official 7-Zip binary -- the latter being what upstream v1.8 gets by shelling
out to a helper, so it doubles as the "how fast could we be" ceiling.

Why Python drives the measurement: in this sandbox a single `date` costs
~350 ms, so the usual `t0=$(date)` / `t1=$(date)` pair adds ~700 ms of pure
overhead to a measurement whose real value is ~600 ms, and `time`'s user/sys
accounting does not see into the native child at all. Python spawns each
child once and reads a monotonic clock around it, which leaves a small,
constant "spawn tax" that is measured and subtracted (see the report).

The candidate binaries print their own in-process timing, which excludes the
spawn tax entirely -- that is the most trustworthy figure for our side.
"""

import argparse
import os
import re
import subprocess
import sys
import time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(REPO, ".build", "bench")
SZ_BUILD = os.path.join(REPO, ".build", "sevenz-test")
HT_BUILD = os.path.join(REPO, ".build", "host-test")
SEVENZ_BIN = os.path.join(REPO, ".build", "7zdl", "extra", "x64", "7za.exe")
BENCH_BIN = os.path.join(BUILD, "bench_extract.exe")
SDK_BIN = os.path.join(SZ_BUILD, "sevenz_e2e.exe")

# Object lists mirror what tests/run-sevenz-tests.sh and tests/run-tests.sh
# build; those scripts must have run once before this can link.
VENDOR_7Z = ["7zAlloc", "7zArcIn", "7zBuf", "7zBuf2", "7zCrc", "7zCrcOpt",
             "7zDec", "7zFile", "7zStream", "Aes", "AesOpt", "Alloc", "Bcj2",
             "Bra", "Bra86", "BraIA64", "CpuArch", "Delta", "DllSecur",
             "Lzma2Dec", "LzmaDec", "Lzma2DecMt", "MtDec", "Threads", "Ppmd7",
             "Ppmd7Dec", "Sha256", "Sha256Opt", "SwapBytes"]
ZLIB = ["adler32", "crc32", "deflate", "inffast", "inflate", "inftrees",
        "trees", "zutil"]
MINIZIP = ["mz_crypt", "mz_os", "mz_os_posix", "mz_strm", "mz_strm_mem",
           "mz_strm_os_posix", "mz_strm_zlib", "mz_zip"]
EXTRA_LIBS = ["-lole32", "-loleaut32", "-luuid", "-ladvapi32", "-luser32",
              "-lshell32"]


def objs(base, names):
    return [os.path.join(base, n + ".o") for n in names]


def build_bench():
    """Link tests/bench_extract.c against the prebuilt engine objects."""
    if os.path.exists(BENCH_BIN):
        return True

    needed = (objs(SZ_BUILD, ["sevenz_extract", "sevenz_chain",
                              "sevenz_mt", "sevenz_volstream", "zipx_common",
                              "zipx_volume"])
              + objs(HT_BUILD, ["zip_extract", "zipx_volstream"])
              + objs(HT_BUILD, ZLIB) + objs(HT_BUILD, MINIZIP)
              + objs(SZ_BUILD, VENDOR_7Z))
    missing = [p for p in needed if not os.path.exists(p)]
    if missing:
        print("missing engine objects, run these first:")
        print("  /usr/bin/bash tests/run-sevenz-tests.sh")
        print("  /usr/bin/bash tests/run-tests.sh --rebuild")
        print("first missing: %s" % missing[0])
        return False

    cmd = ["gcc", "-O2", "-w",
           "-I" + os.path.join(REPO, "third_party", "7z"),
           "-I" + os.path.join(REPO, "third_party", "minizip-ng", "include"),
           "-I" + os.path.join(REPO, "third_party", "zlib", "include"),
           "-I" + os.path.join(REPO, "src"),
           "-I" + os.path.join(REPO, "tests", "compat"),
           "-include", os.path.join(REPO, "tests", "posix_compat.h"),
           "-o", BENCH_BIN, os.path.join(REPO, "tests", "bench_extract.c")]
    cmd += needed + EXTRA_LIBS
    print("== compiling bench harness ==")
    if subprocess.run(cmd, cwd=REPO).returncode != 0:
        return False
    return True


def make_fixture(big, fmt):
    """Build the archive if absent: repeated source text plus a real PE file.

    The blend matters -- compression throughput depends heavily on match
    length, so a pure-text corpus would flatter every decoder equally and a
    pure-random one would measure nothing but copying.
    """
    stem = "big4" if big else "big"
    archive = os.path.join(BUILD, "%s.%s" % (stem, fmt))
    if os.path.exists(archive):
        return archive

    os.makedirs(BUILD, exist_ok=True)
    tp = os.path.join(REPO, "third_party")
    parts = []
    for sub in ("zlib/src", "7z", "minizip-ng/src"):
        d = os.path.join(tp, sub)
        if os.path.isdir(d):
            for name in sorted(os.listdir(d)):
                if name.endswith((".c", ".h")):
                    parts.append(os.path.join(d, name))
    if not parts:
        print("no source available to build a fixture from")
        return None

    repeat = 240 if big else 60
    src = os.path.join(BUILD, "payload_src.bin")
    with open(src, "wb") as out:
        for _ in range(repeat):
            for path in parts:
                with open(path, "rb") as fh:
                    out.write(fh.read())

    payload = src
    pe = r"C:\Windows\System32\ntoskrnl.exe"
    if os.path.exists(pe):
        binary = os.path.join(BUILD, "payload_bin.bin")
        with open(binary, "wb") as out:
            for _ in range(30 if big else 3):
                with open(pe, "rb") as fh:
                    out.write(fh.read())
        payload = os.path.join(BUILD, "payload_mix.bin")
        with open(payload, "wb") as out:
            for _ in range(4 if big else 1):
                for path in (src, binary):
                    with open(path, "rb") as fh:
                        out.write(fh.read())

    print("== creating fixture (first run only) ==")
    if fmt == "7z":
        add = ["a", "-t7z", "-m0=lzma2", "-mx=5", "-ms=on"]
    else:
        add = ["a", "-tzip", "-mx=5", "-mm=Deflate"]
    subprocess.run([SEVENZ_BIN] + add + [archive, payload], cwd=REPO,
                   stdout=subprocess.DEVNULL)
    return archive


def sample(argv, runs, workdir):
    """Run argv `runs` times; return (best wall ms, best in-process ms|None)."""
    best = None
    internal = None
    for _ in range(runs):
        os.makedirs(workdir, exist_ok=True)
        started = time.perf_counter()
        proc = subprocess.run(argv, stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT, cwd=REPO, timeout=1800)
        elapsed = (time.perf_counter() - started) * 1000.0
        if proc.returncode != 0:
            sys.stdout.write(proc.stdout.decode("utf-8", "replace")[:300])
            return None, None
        if best is None or elapsed < best:
            best = elapsed
        match = re.search(rb"wall\s*:\s*([0-9.]+)\s*s", proc.stdout)
        if match:
            value = float(match.group(1)) * 1000.0
            if internal is None or value < internal:
                internal = value
    return best, internal


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--big", action="store_true",
                    help="4x fixture (~320 MiB) for accurate ratios")
    ap.add_argument("--runs", type=int, default=3)
    ap.add_argument("--format", choices=["7z", "zip"], default="7z",
                    help="which engine to benchmark (default 7z)")
    args = ap.parse_args()

    # For ZIP there is no useful SDK reference: the vendored SDK is 7z-only,
    # so the comparison is just ours versus the official binary.
    for path in (SEVENZ_BIN, SDK_BIN if args.format == "7z" else SEVENZ_BIN):
        if not os.path.exists(path):
            print("missing: %s" % path)
            return 1
    if not build_bench():
        return 1

    archive = make_fixture(args.big, args.format)
    if not archive:
        return 1
    raw = os.path.getsize(archive)

    print()
    print("archive : %s (%.0f MiB packed), best of %d runs"
          % (os.path.basename(archive), raw / 1048576.0, args.runs))
    print()

    rows = []
    wall, inner = sample([BENCH_BIN, archive, ".build/bench/W7"], args.runs,
                         os.path.join(BUILD, "W7"))
    if wall:
        rows.append(("ours / " + args.format, wall, inner))

    if args.format == "7z":
        wall, _ = sample([SDK_BIN, archive, ".build/bench/W8"], args.runs,
                         os.path.join(BUILD, "W8"))
        if wall:
            rows.append(("sdk SzArEx", wall, None))
        variants = (("7za 1 thread", ["-mmt=off"]),
                    ("7za 8 threads", ["-mmt=8"]),
                    ("7za all cores", []))
    else:
        variants = (("7za 1 thread", ["-mmt=off"]),)

    for label, extra in variants:
        wall, _ = sample([SEVENZ_BIN, "x", "-y", "-aoa"] + extra
                         + ["-o.build/bench/W9", archive], args.runs,
                         os.path.join(BUILD, "W9"))
        if wall:
            rows.append((label, wall, None))

    print("  %-18s %10s %12s" % ("configuration", "external", "internal"))
    for label, wall, inner in rows:
        print("  %-18s %9.0f ms %12s"
              % (label, wall,
                 "%9.0f ms" % inner if inner else "         -"))

    tax = None
    ours = [r for r in rows if r[0].startswith("ours")]
    if ours and ours[0][2]:
        tax = ours[0][1] - ours[0][2]
        ref = ours[0][2]
        print()
        print("  spawn tax (ours external - internal): %.0f ms" % tax)
        print()
        print("  %-18s %12s %9s" % ("configuration", "net", "vs ours"))
        for label, wall, inner in rows:
            net = inner if inner else max(wall - tax, 1.0)
            print("  %-18s %9.0f ms %8.2fx" % (label, net, ref / net))
        print()
        print("  net = external minus spawn tax; >1x means faster than ours")
    return 0


if __name__ == "__main__":
    sys.exit(main())
