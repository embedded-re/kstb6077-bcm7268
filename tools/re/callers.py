#!/usr/bin/env python3
"""List call sites (bl/blx/b.w) of the given function addresses.

    callers.py bolt_thumb.dis 0x070109cc 0x07011430
"""
import re, sys

if len(sys.argv) < 3:
    sys.exit(__doc__)
lines = open(sys.argv[1]).read().splitlines()
for f in sys.argv[2:]:
    t = hex(int(f, 16))
    pat = re.compile(rf"\s(bl|blx|b\.w)(\.w)?\s+{t}\b")
    hits = [l.split(":")[0].strip() for l in lines if pat.search(l)]
    print(f"{t}: " + (" ".join("0x" + h for h in hits) or "no direct callers"))
