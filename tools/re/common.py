"""Shared helpers for analysing a BOLT RAM dump (see docs/bolt/bolt.md §11)."""
import bisect, re, struct

BASE = 0x07008000          # load address of the dump made with tools/kstb-dump 0x07008000 ...


class Image:
    def __init__(self, bin_path, dis_path, base=BASE):
        self.d = open(bin_path, "rb").read()
        self.base = base
        self.ins = {}       # addr -> instruction text (Thumb listing from objdump -M force-thumb)
        for line in open(dis_path):
            m = re.match(r"\s*([0-9a-f]+):\s+[0-9a-f]{4}(?: [0-9a-f]{4})?\s+(.*)", line)
            if m:
                self.ins[int(m.group(1), 16)] = m.group(2).rstrip()
        self.addrs = sorted(self.ins)

    def word(self, a):
        return struct.unpack_from("<I", self.d, a - self.base)[0]

    def string(self, a, maxlen=120):
        o = a - self.base
        if not 0 <= o < len(self.d):
            return None
        e = self.d.find(b"\0", o)
        t = self.d[o:e]
        ok = 0 < len(t) < maxlen and all(32 <= c < 127 or c in (9, 10, 13, 27) for c in t)
        return t.decode("latin1") if ok else None

    def pool_target(self, text):
        """For 'ldr rX, [pc, #n] @ (0xADDR)' return the 32-bit value stored at ADDR."""
        m = re.search(r"\[pc, #-?\d+\]\s+@ \(0x([0-9a-f]+)\)", text)
        return self.word(int(m.group(1), 16)) if m else None

    def func_start(self, a, limit=0x4000):
        i = bisect.bisect_left(self.addrs, a)
        while i > 0:
            i -= 1
            t = self.ins[self.addrs[i]]
            if re.match(r"(push(\.w)?|stmdb(\.w)?\s+sp!,)\s*\{.*lr\}", t):
                return self.addrs[i]
            if a - self.addrs[i] > limit:
                break
        return None
