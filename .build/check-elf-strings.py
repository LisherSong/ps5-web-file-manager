import re, sys
f = sys.argv[1]
data = open(f, "rb").read()
runs = re.findall(rb"[\x20-\x7e]{6,}", data)
hits = set()
keys = [
    b"zipx_limits_profile",
    b"k_large_limits",
    b"ZIPX_LIMITS_LARGE",
    b"ZIPX_LIMITS_DEFAULT",
    b"large-file profile",
    b"large-file",
    b"LargeFile",
    b"large_file",
    b"extractLargeAsk",
    b"extractLargeActive",
    b"large",
]
for r in runs:
    for k in keys:
        if k in r and k.decode() not in hits:
            # Trim the context a bit
            i = r.find(k)
            ctx = r[max(0, i - 8):i + len(k) + 24].strip()
            if len(ctx) < 80:
                hits.add(ctx.decode(errors="replace"))
for h in sorted(hits):
    print(repr(h))
print("total:", len(hits))