#!/usr/bin/env python3
"""Decode Broadcom RDC (register DMA) lists from a hex dump.

Input: dump lines "addr w0 w1 w2 w3 ..." (as in docs/bolt/raw/display/).
Opcode table and word counts from nexus.ko BRDC_DBG_GetListEntry_isr and the
BRDC_AddrRul_* builders. Register addresses in the lists are CPU addresses
(0xfxxxxxxx).

    rdc.py DUMP [START_ADDR] [--names]   --names: label registers with the
                                         nexus.ko functions that use them
"""
import sys, re, subprocess, os
OPS = {  # op: (name, fmt)
 0x00: ("NOP", ""), 0x01: ("IMM_TO_REG", "RI"), 0x02: ("VAR_TO_REG", "R"),
 0x03: ("REG_TO_VAR", "R"), 0x04: ("IMM_TO_VAR", "I"), 0x05: ("IMMS_TO_REG", "Rn"),
 0x06: ("IMMS_TO_REGS", "Rn"), 0x07: ("REGS_TO_REGS", "RR"), 0x08: ("REG_TO_REGS", "RR"),
 0x0b: ("AND", ""), 0x0c: ("AND_IMM", "I"), 0x0d: ("OR", ""), 0x0e: ("OR_IMM", "I"),
 0x0f: ("XOR", ""), 0x10: ("XOR_IMM", "I"), 0x11: ("NOT", ""), 0x12: ("SHIFT", ""),
 0x13: ("SUM", ""), 0x14: ("SUM_IMM", "I"), 0x15: ("OP15", "R"), 0x16: ("OP16", ""),
 0x17: ("OP17", ""),
}
def load(path):
    mem = {}
    for l in open(path):
        p = l.split()
        if not p or not re.fullmatch(r"[0-9a-f]{8}", p[0]): continue
        a = int(p[0], 16)
        for i, w in enumerate(p[1:5]):
            if re.fullmatch(r"[0-9a-f]{8}", w): mem[a + 4*i] = int(w, 16)
    return mem
def names():
    K = os.path.join(os.path.dirname(__file__), "../../stock/release/modules/nexus.ko")
    out = subprocess.run([os.path.join(os.path.dirname(__file__), "ko.py"), K, "consts",
                          "0x20600000", "0x20700000"], capture_output=True, text=True).stdout
    n = {}
    for l in out.splitlines():
        p = l.split(None, 1)
        if len(p) == 2: n[int(p[0], 16) + 0xd0000000] = p[1][:70]
    return n
def entry_len(mem, pc):
    """Words in the entry at pc, or 0 if it doesn't look like a valid entry."""
    w = mem.get(pc)
    if w is None or (w >> 24) not in OPS: return 0
    name, fmt = OPS[w >> 24]
    isreg = lambda x: x is not None and 0xf0000000 <= x < 0xf2000000
    if name in ("AND","OR","XOR","SUM","NOT","SHIFT","NOP") and w & 0x00fc0000 and name != "SHIFT": return 0
    if fmt == "RI": return 3 if isreg(mem.get(pc+4)) and mem.get(pc+8) is not None else 0
    if fmt == "R": return 2 if isreg(mem.get(pc+4)) else 0
    if fmt == "I": return 2 if mem.get(pc+4) is not None else 0
    if fmt == "RR": return 3 if isreg(mem.get(pc+4)) and isreg(mem.get(pc+8)) else 0
    if fmt == "Rn":
        n = (w & 0xfff) + 1
        return 2 + n if isreg(mem.get(pc+4)) and n <= 1024 else 0
    return 1
def good_run(mem, pc, k=3):
    """k valid entries in a row starting at pc, at least one naming a register."""
    regs = 0
    for _ in range(k):
        n = entry_len(mem, pc)
        if not n: return False
        if n > 1 and OPS[mem[pc] >> 24][1] and "R" in OPS[mem[pc] >> 24][1]: regs += 1
        pc += 4 * n
    return regs > 0
def main():
    a = [x for x in sys.argv[1:] if not x.startswith("--")]
    mem = load(a[0]); nm = names() if "--names" in sys.argv else {}
    pc = int(a[1], 16) if len(a) > 1 else min(mem)
    end = max(mem) + 4
    lab = lambda r: ("   ; " + nm[r]) if r in nm else ""
    while pc < end:
        w = mem.get(pc)
        if w is None: break
        op = w >> 24
        if not entry_len(mem, pc):
            q = pc + 4
            while q < end and not good_run(mem, q): q += 4
            print("%08x  ... %d words not a list ..." % (pc, (q - pc) // 4)); pc = q; continue
        name, fmt = OPS[op]
        v0, v6, v12 = w & 63, (w >> 6) & 63, (w >> 12) & 63
        if name in ("AND","OR","XOR","SUM"): desc = "v%d = v%d %s v%d" % (v0, v12, name, v6)
        elif name in ("AND_IMM","OR_IMM","XOR_IMM","SUM_IMM"): desc = "v%d = v%d %s" % (v0, v12, name[:-4])
        elif name == "NOT": desc = "v%d = ~v%d" % (v0, v12)
        elif name == "SHIFT": desc = "v%d = v%d shift %d (w=%08x)" % (v0, v12, (w >> 18) & 31, w)
        elif name == "VAR_TO_REG": desc = "reg = v%d" % v12
        elif name in ("REG_TO_VAR", "IMM_TO_VAR"): desc = "v%d =" % v0
        else: desc = ""
        q = pc + 4; args = []
        if fmt == "RI": args = ["%08x" % mem[q], "<- %08x" % mem[q+4]]; r = mem[q]; q += 8
        elif fmt in ("R", "I"): args = ["%08x" % mem[q]]; r = mem[q] if fmt == "R" else None; q += 4
        elif fmt == "RR": n = (w & 0xfff) + 1; args = ["%08x -> %08x  n=%d" % (mem[q], mem[q+4], n)]; r = mem[q+4]; q += 8
        elif fmt == "Rn":
            n = (w & 0xfff) + 1; r = mem[q]
            vals = [mem[q + 4 + 4*i] for i in range(n)]; q += 4 + 4*n
            print("%08x  %-13s %08x  n=%d%s" % (pc, name, r, n, lab(r)))
            for i, v in enumerate(vals):
                rr = r + 4*i if name == "IMMS_TO_REGS" else r
                print("            %08x <- %08x%s" % (rr, v, lab(rr)))
            pc = q; continue
        else: r = None
        print("%08x  %-13s %s %s%s" % (pc, name, desc, " ".join(args), lab(r) if r else ""))
        pc = q
main()
