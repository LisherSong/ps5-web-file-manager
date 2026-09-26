#!/usr/bin/env python3
"""Rebuild the offline preview under .build/preview/ from the real assets.

    python .build/preview_build.py

Copies assets/* and re-injects .build/preview_stub.html into a COPY of
index.html, so the page can be served from a plain static server
(.build/preview_check.mjs drives it with a browser). assets/ is never touched.
"""

from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "assets"
DST = ROOT / ".build" / "preview"
STUB = (ROOT / ".build" / "preview_stub.html").read_text(encoding="utf-8")
MARKER = '  <script src="/main.js"></script>'

DST.mkdir(parents=True, exist_ok=True)
for item in SRC.iterdir():
    if item.is_file():
        shutil.copy2(item, DST / item.name)

html = (DST / "index.html").read_text(encoding="utf-8")
if MARKER not in html:
    raise SystemExit("index.html has no <script src=\"/main.js\"> tag to hook")
(DST / "index.html").write_text(html.replace(MARKER, STUB + MARKER), encoding="utf-8")

print("preview ready:", DST)
print("serve it with:  python -m http.server 8899 --bind 127.0.0.1   (run inside .build/preview)")
