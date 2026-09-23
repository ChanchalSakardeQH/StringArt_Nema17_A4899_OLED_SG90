#!/usr/bin/env python3
"""
Three consistency checks on the served page. All three catch the kind of
mistake that fails silently on the machine rather than loudly at build time:
the page loads, looks right, and then one control quietly does nothing.

1. Every script block must parse. (Needs node; skipped if it is not present.)
2. Every element id the JavaScript looks up must exist in the HTML. A typo
   does not throw until that line runs, which may be only when a job is
   already halfway through.
3. Every j.<key> read off /status must be a key the firmware actually emits.
   A missing one reads as undefined and renders as "undefined" or NaN
   somewhere in the interface.
"""

import os
import re
import shutil
import subprocess
import sys
import tempfile
import pathlib

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent / "StringArt_Nema17_GUI"
PAGE_H = ROOT / "web_page.h"
SKETCH = ROOT / "StringArt_Nema17_GUI.ino"

raw = PAGE_H.read_text(encoding="utf-8", errors="replace")
m = re.search(r'R"STRINGARTPAGE\((.*)\)STRINGARTPAGE"', raw, re.S)
if not m:
    print("could not find the page inside web_page.h")
    sys.exit(1)
page = m.group(1)

blocks = re.findall(r"<script[^>]*>(.*?)</script>", page, re.S)
js = "\n".join(blocks)
fail = 0

# ---- 1. script blocks parse --------------------------------------------
if shutil.which("node"):
    bad = []
    with tempfile.TemporaryDirectory() as tmp:
        for i, b in enumerate(blocks):
            f = os.path.join(tmp, "b%d.js" % i)
            with open(f, "w") as fh:
                fh.write(b)
            r = subprocess.run(["node", "--check", f],
                               capture_output=True, text=True)
            if r.returncode != 0:
                bad.append((i, r.stderr.strip().split("\n")[0]))
    if bad:
        fail = 1
        print("script blocks that do not parse:")
        for i, err in bad:
            print("   block %d: %s" % (i, err))
    else:
        print("ok -- %d script blocks parse" % len(blocks))
else:
    print("skipped -- node not installed, %d script blocks unchecked" % len(blocks))

# ---- 2. element ids -----------------------------------------------------
ids_in_html = set(re.findall(r'id="([^"]+)"', page))
looked_up = set(re.findall(r'getElementById\("([^"]+)"\)', js))
looked_up |= set(re.findall(r'\$\("#([\w-]+)"\)', js))

missing = sorted(looked_up - ids_in_html)
if missing:
    fail = 1
    print("\nids used by JS but absent from the HTML:")
    for x in missing:
        print("   #" + x)
else:
    print("ok -- %d ids looked up, all present" % len(looked_up))

# ---- 3. /status keys ----------------------------------------------------
emitted = set(re.findall(r'json \+= "\\"(\w+)\\":', SKETCH.read_text()))
used = set(re.findall(r"\bj\.(\w+)\b", js))
used |= set(re.findall(r"\bst\.(\w+)\b", js))
used |= set(re.findall(r"lastStatus\.(\w+)", js))

# Properties of objects that are not the status object.
IGNORE = {"length", "value", "textContent", "hidden", "checked", "disabled",
          "innerHTML", "style", "id", "className", "forEach", "map", "push"}
unknown = sorted(used - emitted - IGNORE)
if unknown:
    fail = 1
    print("\nstatus keys read by JS but never emitted by the firmware:")
    for k in unknown:
        print("   j." + k)
else:
    print("ok -- %d status keys read, all emitted" % len(used - IGNORE))

sys.exit(fail)
