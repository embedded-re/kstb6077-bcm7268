#!/usr/bin/env python3
"""Print the Thumb disassembly of a range with literal-pool loads annotated.

    ann.py bolt_ram.bin bolt_thumb.dis 0x070109cc 0x07010a80
"""
import os, sys
sys.path.insert(0, os.path.dirname(__file__))
from common import Image

if len(sys.argv) != 5:
    sys.exit(__doc__)
img = Image(sys.argv[1], sys.argv[2])
lo, hi = int(sys.argv[3], 16), int(sys.argv[4], 16)
for a in img.addrs:
    if lo <= a < hi:
        t = img.ins[a]
        v = img.pool_target(t)
        if v is not None:
            s = img.string(v)
            t += f"   ; ={v:#x}" + (f" {s!r}" if s else "")
        print(f"{a:08x}  {t}")
