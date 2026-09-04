"""Verify our large-file mode identifiers made it into the ELF.
gen-asset-module.py gzips JS assets, so plain `strings` won't find them.
This script finds all gzip streams in the ELF, decompresses them, and greps
for the new identifiers."""
import re, sys, zlib

f = sys.argv[1]
data = open(f, "rb").read()

# Gzip streams start with 0x1f 0x8b. Walk through the file looking for them.
keys = [
    b"extractLargeAsk",
    b"extractLargeActive",
    b"promptLargeMode",
    b"shouldPromptLargeMode",
    b"LARGE_FILE_THRESHOLD_BYTES",
    b"/api/extract",
    b"large",
]
seen = {}
i = 0
while i < len(data) - 10:
    if data[i] == 0x1f and data[i + 1] == 0x8b:
        # Try to decompress starting here, capped at 2 MiB.
        end_cap = min(len(data), i + 2 * 1024 * 1024)
        try:
            dec = zlib.decompressobj(zlib.MAX_WBITS | 16)
            chunk = dec.decompress(data[i:end_cap], 2 * 1024 * 1024)
            if not dec.eof:
                i += 1
                continue
        except Exception:
            i += 1
            continue
        # Check for any of our keys in the decompressed stream.
        for k in keys:
            if k in chunk and k not in seen:
                idx = chunk.find(k)
                ctx = chunk[max(0, idx - 24):idx + len(k) + 48]
                seen[k] = ctx.decode("utf-8", errors="replace")
        i += 1
    else:
        i += 1

print(f"decompressed streams scanned, keys found: {len(seen)} / {len(keys)}")
for k, v in seen.items():
    print(f"  ✓ {k.decode()}")
    print(f"    context: {v[:160]}")
missing = [k for k in keys if k not in seen]
if missing:
    print(f"  ✗ not found: {[m.decode() for m in missing]}")