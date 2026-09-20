#!/usr/bin/env python3
"""Cross-format extraction benchmark: same payload, three containers.

    python tests/bench_formats.py                 # one 329 MiB file, 3 runs
    python tests/bench_formats.py --source .build/bench/payload_bin.bin \\
        --stem bin --runs 5                       # 39 MiB of machine code
    python tests/bench_formats.py --source .build/bench/manyfiles_src --stem mf

bench_driver.py answers "how do we compare with 7-Zip for one format"; this
answers "which container should a user expect to unpack fastest", which is a
different question and needs the three archives to hold the same bytes.

Method notes that took a while to get right, so they are pinned here:

  * Fresh output directory per run. Publishing on top of the previous run's
    tree took the same 7z archive from 0.77 s to 1.28 s.
  * Forward slashes in every path. The engines derive the destination parent
    with a '/' scan, so "C:\\...\\W0" is rejected as an invalid destination.
  * Best of N, because a single run varies widely on this host: extraction
    creates hundreds of MiB that the on-access scanner inspects and the page
    cache has to write back, and neither is under our control.
  * Corpus matters as much as container. Deflate decodes faster than LZMA2 on
    ordinary data, but on highly repetitive text LZMA2 finds long matches
    where deflate only finds 32 KiB ones, and the order flips. Run the same
    archive set on more than one source before concluding anything.

Packed sizes are printed next to the times on purpose: a container that packs
5x smaller also reads 5x less from the card, which is why the ranking on the
PS5 -- where storage is the slow part -- can differ from the ranking here.
"""

import argparse
import os
import re
import subprocess
import sys
import time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(REPO, ".build", "bench")
BENCH_BIN = os.path.join(BUILD, "bench_extract.exe")
SEVENZ_BIN = os.path.join(REPO, ".build", "7zdl", "extra", "x64", "7za.exe")
RAR_BIN = next((p for p in (
    os.environ.get("WFM_RAR"),
    r"C:\Program Files\WinRAR\rar.exe",
    r"C:\Program Files (x86)\WinRAR\rar.exe",
    "/usr/bin/rar", "/usr/local/bin/rar") if p and os.path.exists(p)), None)
DEFAULT_SOURCE = os.path.join(BUILD, "payload4.bin")

# Nominal level 5 in both packers, which is the GUI default of each:
# 7-Zip -mx=5, WinRAR -m3 ("Normal"). They are not equivalent amounts of
# work -- LZMA2 at level 5 is a far stronger compressor than deflate at 5 --
# but they are what a user who never opens the advanced panel ends up with.
FORMATS = ("zip", "7z", "rar")


def pack(stem, payload):
    """Create stem.{zip,7z,rar} from payload if they are not there yet."""
    made = []
    for fmt in FORMATS:
        archive = os.path.join(BUILD, "%s.%s" % (stem, fmt))
        if os.path.exists(archive):
            continue
        if fmt == "rar":
            if not RAR_BIN:
                print("skip %s: WinRAR (rar.exe) not found" % fmt)
                continue
            cmd = [RAR_BIN, "a", "-m3", "-idq"]
            if os.path.isfile(payload):
                cmd.append("-ep1")
            cmd += [archive, payload]
        else:
            cmd = [SEVENZ_BIN, "a", "-t" + fmt] + (
                ["-m0=lzma2", "-mx=5", "-ms=on"] if fmt == "7z"
                else ["-mx=5", "-mm=Deflate"]) + [archive, payload]
        print("== packing %s ==" % os.path.basename(archive))
        if subprocess.run(cmd, cwd=REPO, stdout=subprocess.DEVNULL).returncode:
            print("packing failed")
            return None
        made.append(archive)
    return [os.path.join(BUILD, "%s.%s" % (stem, f)) for f in FORMATS]


def time_archive(archive, runs):
    """Best in-process wall time over `runs` fresh-directory extractions."""
    tag = os.path.splitext(os.path.basename(archive))[0]
    times = []
    unpacked = entries = 0
    for i in range(runs):
        # A path that does not exist yet: the engines publish with a rename,
        # and a rename into a tree that already has the file costs extra.
        out = os.path.join(BUILD, "F-%s-%d" % (tag, i)).replace("\\", "/")
        started = time.perf_counter()
        proc = subprocess.run([BENCH_BIN, archive, out], cwd=REPO,
                              stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT, timeout=1800)
        external = time.perf_counter() - started
        if proc.returncode != 0:
            sys.stdout.write(proc.stdout.decode("utf-8", "replace")[:400])
            return None
        text = proc.stdout.decode("utf-8", "replace")
        match = re.search(r"wall\s*:\s*([0-9.]+)", text)
        if not match:
            return None
        times.append(float(match.group(1)))
        unpacked = float(re.search(r"unpacked\s*:\s*([0-9.]+)", text).group(1))
        entries = int(re.search(r"entries\s*:\s*(\d+)", text).group(1))
    return {"times": sorted(times), "unpacked": unpacked, "entries": entries}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--runs", type=int, default=3)
    ap.add_argument("--source", default=DEFAULT_SOURCE,
                    help="file or directory to pack (default: %s)" % DEFAULT_SOURCE)
    ap.add_argument("--stem", help="archive base name (default: source basename)")
    args = ap.parse_args()

    if not os.path.exists(BENCH_BIN):
        print("missing %s -- run: python tests/bench_driver.py" % BENCH_BIN)
        return 1

    payload = args.source
    if not os.path.isabs(payload):
        payload = os.path.join(REPO, payload)
    if not os.path.exists(payload):
        print("missing payload: %s" % payload)
        return 1
    stem = args.stem or os.path.splitext(os.path.basename(payload))[0]

    archives = pack(stem, payload)
    if not archives:
        return 1

    rows = []
    for archive in archives:
        if not os.path.exists(archive):
            continue
        result = time_archive(archive, args.runs)
        if result is None:
            print("failed: %s" % archive)
            continue
        rows.append((os.path.splitext(archive)[1][1:], archive, result))

    if not rows:
        return 1

    print()
    print("best of %d runs, fresh output directory each time" % args.runs)
    print("payload: %d entries, %.1f MiB unpacked"
          % (rows[0][2]["entries"], rows[0][2]["unpacked"]))
    print()
    print("  %-5s %10s %9s %12s %12s" %
          ("fmt", "packed", "best", "median", "throughput"))
    best = min(r[2]["times"][0] for r in rows)
    for fmt, archive, result in sorted(rows, key=lambda r: r[2]["times"][0]):
        packed = os.path.getsize(archive) / 1048576.0
        times = result["times"]
        median = times[len(times) // 2]
        rate = result["unpacked"] / times[0]
        print("  %-5s %8.1f M %7.0f ms %9.0f ms %8.0f MiB/s"
              % (fmt, packed, times[0] * 1000.0, median * 1000.0, rate))
    print()
    for fmt, archive, result in sorted(rows, key=lambda r: r[2]["times"][0]):
        print("  %-5s vs fastest: %.2fx" % (fmt, result["times"][0] / best))
    return 0


if __name__ == "__main__":
    sys.exit(main())
