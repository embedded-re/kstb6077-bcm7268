#!/usr/bin/env python3
"""Look inside an unstripped ARM ELF object (e.g. stock/release/modules/nexus.ko).

    ko.py FILE data SYMBOL...       words of a data symbol, with relocation targets
                                    (strings shown, pointers to other symbols named)
    ko.py FILE dis FUNCTION...      disassembly (llvm-objdump), literal-pool loads
                                    annotated with the loaded value
    ko.py FILE consts LO HI         every 32-bit word in .text between LO and HI
                                    (e.g. register addresses), with the function it's in
    ko.py FILE pwr NAME             BCHP_PWR resource NAME: type, id, and what it depends on

Broadcom register addresses appear as bus addresses: 0x2xxxxxxx = CPU 0xfxxxxxxx.
Needs pyelftools and llvm-objdump.
"""
import bisect, re, struct, subprocess, sys
from elftools.elf.elffile import ELFFile

if len(sys.argv) < 4:
    sys.exit(__doc__)
path, cmd, args = sys.argv[1], sys.argv[2], sys.argv[3:]
elf = ELFFile(open(path, "rb"))
symtab = elf.get_section_by_name(".symtab")
syms = {s.name: s for s in symtab.iter_symbols() if s.name}
text = elf.get_section_by_name(".text").data()
_rels = {}


def rels(secname):
    if secname not in _rels:
        r, m = elf.get_section_by_name(".rel" + secname), {}
        if r:
            for e in r.iter_relocations():
                m[e["r_offset"]] = symtab.get_symbol(e["r_info_sym"])
        _rels[secname] = m
    return _rels[secname]


def target(t, addend):
    """Name of a relocation target; the string itself if it points into .rodata.str*."""
    if not isinstance(t["st_shndx"], int):
        return t.name
    sec = elf.get_section(t["st_shndx"])
    if t.name:
        return t.name + (f"+{addend:#x}" if addend else "")
    if sec.name.startswith(".rodata.str"):
        d, o = sec.data(), t["st_value"] + addend
        return repr(d[o:d.find(b"\0", o)].decode("latin1"))
    return f"{sec.name}+{addend:#x}"


def data(name):
    s = syms[name]
    sec = elf.get_section(s["st_shndx"])
    d, a, n, rm = sec.data(), s["st_value"], s["st_size"] or 16, rels(sec.name)
    print(f"== {name} ({sec.name} {a:#x}, {n} bytes)")
    for o in range(a, a + n, 4):
        w = struct.unpack_from("<I", d, o)[0]
        t = rm.get(o)
        print(f"  +{o - a:03x}: {w:08x}" + (f"  -> {target(t, w)}" if t is not None else ""))


def dis(name):
    out = subprocess.run(["llvm-objdump", "-d", "-r", f"--disassemble-symbols={name}", path],
                         capture_output=True, text=True).stdout
    for line in out.splitlines():
        m = re.search(r"@ 0x([0-9a-f]+)", line)
        if m and "ldr" in line:
            line += f"   ; ={struct.unpack_from('<I', text, int(m.group(1), 16))[0]:#x}"
        print(line)


def consts(lo, hi):
    funcs = sorted((s["st_value"], s.name) for s in syms.values()
                   if s["st_info"]["type"] == "STT_FUNC" and s["st_shndx"] == 1)
    starts = [a for a, _ in funcs]
    hits = {}
    for o in range(0, len(text) - 3, 4):
        w = struct.unpack_from("<I", text, o)[0]
        if lo <= w < hi:
            i = bisect.bisect_right(starts, o) - 1
            hits.setdefault(w, set()).add(funcs[i][1] if i >= 0 else "?")
    for w in sorted(hits):
        print(f"{w:#010x}  {', '.join(sorted(hits[w])[:4])}")


def pwr(name):
    for kind in ("Resource", "Depend"):
        n = f"BCHP_PWR_P_{kind}_{name}"
        if n in syms:
            data(n)


if cmd == "data":
    for n in args:
        data(n)
elif cmd == "dis":
    for n in args:
        dis(n)
elif cmd == "consts":
    consts(int(args[0], 16), int(args[1], 16))
elif cmd == "pwr":
    for n in args:
        pwr(n)
else:
    sys.exit(__doc__)
