#!/usr/bin/env python3
"""Find code that references strings containing the given text.

    xref.py bolt_ram.bin bolt_thumb.dis 'SPLASH: start audio' 'pcm' ...
"""
import os, re, sys
sys.path.insert(0, os.path.dirname(__file__))
from common import Image

if len(sys.argv) < 4:
    sys.exit(__doc__)
img = Image(sys.argv[1], sys.argv[2])
for sub in sys.argv[3:]:
    hits = set()
    for m in re.finditer(rb"[\x20-\x7e\t\n\r\x1b]{3,}", img.d):
        k = m.group().find(sub.encode())
        if k >= 0:
            st = m.start() + k
            while st > m.start() and img.d[st - 1] != 0:
                st -= 1
            hits.add(img.base + st)
    for sa in sorted(hits):
        print(f"== {sa:#010x} {img.string(sa, 200)!r}")
        for a in img.addrs:
            if img.pool_target(img.ins[a]) == sa:
                f = img.func_start(a)
                print(f"   ref {a:#010x}  {img.ins[a]}   (function {f and hex(f)})")
