"""Verify that specific identifiers made it into the ELF's embedded assets.

`gen-asset-module.py` gzips every JS/CSS/HTML asset (compresslevel=9, mtime=0),
so plain `strings` finds none of their text -- a zero hit means "compressed",
not "missing". This script walks the file for gzip streams, decompresses each
one and reports where each key landed.

Usage:
  python .build/check-elf-gzip.py <elf> [key ...]

With no keys it defaults to the current round: the v1.9.3M fork marker and the
footer tooltip that explains it. Pass your own after the ELF path when reusing
this for another change.

Scope note: this covers **assets only**. C-side string literals (e.g.
`extract_dict_too_large`, `versionsTooltip` in a header) are not compressed and
belong to a plain `strings` check.
"""
import sys
import zlib

if len(sys.argv) < 2:
    sys.exit(__doc__)

f = sys.argv[1]
data = open(f, "rb").read()

keys = [k.encode() for k in sys.argv[2:]] or [
    b"v1.9.3M",                          # assets/main.js footer fallback
    b"versionTooltip",                   # main.js + both lang files
    "本版为 LisherSong 改版".encode(),      # assets/lang-zh.js
    b"Modified build by LisherSong",     # assets/lang-en.js
]

seen = {}
streams = 0
i = data.find(b"\x1f\x8b\x08")
while i >= 0:
    # Try to decompress starting here, capped at 2 MiB.
    end_cap = min(len(data), i + 2 * 1024 * 1024)
    try:
        dec = zlib.decompressobj(zlib.MAX_WBITS | 16)
        chunk = dec.decompress(data[i:end_cap], 2 * 1024 * 1024)
        if dec.eof and chunk:
            streams += 1
            for k in keys:
                if k in chunk and k not in seen:
                    idx = chunk.find(k)
                    ctx = chunk[max(0, idx - 24):idx + len(k) + 48]
                    seen[k] = ctx.decode("utf-8", errors="replace")
    except Exception:
        pass
    i = data.find(b"\x1f\x8b\x08", i + 1)

print(f"{f}: {streams} gzip streams scanned, keys found: {len(seen)} / {len(keys)}")
for k, v in seen.items():
    print(f"  OK   {k.decode()}")
    print(f"       context: {v[:160]}")
missing = [k for k in keys if k not in seen]
for m in missing:
    print(f"  MISS {m.decode()}")
sys.exit(1 if missing else 0)
