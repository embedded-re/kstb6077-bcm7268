#!/usr/bin/env python3
"""Disassemble and assemble V3D 3.3 QPU instructions (64-bit words).

    qpu.py dis WORD...              disassemble hex words
    qpu.py dis -f FILE OFF COUNT    disassemble COUNT words at file offset OFF
    qpu.py so VA COUNT              disassemble COUNT words at virtual address VA
                                    in stock/release/hal/egl/libGLES_nexus.so

Field layout as in Mesa's src/broadcom/qpu/qpu_pack.c, plus the load
immediate type libGLES has (v3d_qpu_instr_get_type: mul opcode 0 and signal
bits 11xxx). Checked against the
stock libGLES_nexus.so: its signal table (0x19841c), magic write-address
names (0x1b2d50) and small-immediate table (0x19835c) match Mesa's V3D 3.3
tables entry for entry. The ALU opcode ranges are Mesa's; they are inferred,
not taken from libGLES.
"""
import struct, sys

# signal index -> set of signals (libGLES table 0x19841c = Mesa v33_sig_map)
SIG = {0: (), 1: ('thrsw',), 2: ('ldunif',), 3: ('thrsw', 'ldunif'),
       4: ('ldtmu',), 5: ('thrsw', 'ldtmu'), 6: ('ldtmu', 'ldunif'),
       7: ('thrsw', 'ldtmu', 'ldunif'), 8: ('ldvary',), 9: ('thrsw', 'ldvary'),
       10: ('ldvary', 'ldunif'), 11: ('thrsw', 'ldvary', 'ldunif'),
       12: ('ldvary', 'ldtmu'), 13: ('thrsw', 'ldvary', 'ldtmu'),
       14: ('small_imm', 'ldvary'), 15: ('small_imm',), 16: ('ldtlb',),
       17: ('ldtlbu',), 22: ('ucb',), 23: ('rotate',), 24: ('ldvpm',),
       25: ('thrsw', 'ldvpm'), 26: ('ldvpm', 'ldunif'),
       27: ('thrsw', 'ldvpm', 'ldunif'), 28: ('ldvpm', 'ldtmu'),
       29: ('thrsw', 'ldvpm', 'ldtmu'), 30: ('small_imm', 'ldvpm'),
       31: ('small_imm', 'ldtmu')}
SIG_INDEX = {frozenset(v): k for k, v in SIG.items()}

# magic write addresses (libGLES name table 0x1b2d50)
MAGIC = ['r0', 'r1', 'r2', 'r3', 'r4', 'r5quad', 'nop', 'tlb', 'tlbu', 'tmu',
         'tmul', 'tmud', 'tmua', 'tmuau', 'vpm', 'vpmu', 'sync', 'syncu', None,
         'recip', 'rsqrt', 'exp', 'log', 'sin', 'rsqrt2']
MUX = ['r0', 'r1', 'r2', 'r3', 'r4', 'r5', 'a', 'b']

# small immediates (libGLES table 0x19835c)
SMALL_IMM = list(range(16)) + list(range(-16, 0)) + \
    ['2^%d' % e for e in range(-8, 8)]

ANY = 0xff
def M(*bits):
    v = 0
    for b in bits: v |= 1 << b
    return v
def MR(lo, hi): return M(*range(lo, hi + 1))

# (first, last, mux_b mask, mux_a mask, name) as in Mesa (V3D 3.3 subset)
ADD_OPS = [
    (0, 47, ANY, ANY, 'fadd'),          # faddnf if mux order swapped
    (53, 55, ANY, ANY, 'vfpack'),
    (56, 56, ANY, ANY, 'add'),
    (57, 59, ANY, ANY, 'vfpack'),
    (60, 60, ANY, ANY, 'sub'),
    (61, 63, ANY, ANY, 'vfpack'),
    (64, 111, ANY, ANY, 'fsub'),
    (120, 120, ANY, ANY, 'min'), (121, 121, ANY, ANY, 'max'),
    (122, 122, ANY, ANY, 'umin'), (123, 123, ANY, ANY, 'umax'),
    (124, 124, ANY, ANY, 'shl'), (125, 125, ANY, ANY, 'shr'),
    (126, 126, ANY, ANY, 'asr'), (127, 127, ANY, ANY, 'ror'),
    (128, 175, ANY, ANY, 'fmin'),       # fmax if mux order swapped
    (176, 180, ANY, ANY, 'vfmin'),
    (181, 181, ANY, ANY, 'and'), (182, 182, ANY, ANY, 'or'),
    (183, 183, ANY, ANY, 'xor'), (184, 184, ANY, ANY, 'vadd'),
    (185, 185, ANY, ANY, 'vsub'),
    (186, 186, M(0), ANY, 'not'), (186, 186, M(1), ANY, 'neg'),
    (186, 186, M(2), ANY, 'flapush'), (186, 186, M(3), ANY, 'flbpush'),
    (186, 186, M(4), ANY, 'flpop'), (186, 186, M(5), ANY, 'recip'),
    (186, 186, M(6), ANY, 'setmsf'), (186, 186, M(7), ANY, 'setrevf'),
    (187, 187, M(0), M(0), 'nop'), (187, 187, M(0), M(1), 'tidx'),
    (187, 187, M(0), M(2), 'eidx'), (187, 187, M(0), M(3), 'lr'),
    (187, 187, M(0), M(4), 'vfla'), (187, 187, M(0), M(5), 'vflna'),
    (187, 187, M(0), M(6), 'vflb'), (187, 187, M(0), M(7), 'vflnb'),
    (187, 187, M(1), MR(0, 2), 'fxcd'), (187, 187, M(1), M(3), 'xcd'),
    (187, 187, M(1), MR(4, 6), 'fycd'), (187, 187, M(1), M(7), 'ycd'),
    (187, 187, M(2), M(0), 'msf'), (187, 187, M(2), M(1), 'revf'),
    (187, 187, M(2), M(2), 'vdwwt'), (187, 187, M(2), M(5), 'tmuwt'),
    (187, 187, M(2), M(6), 'vpmwt'),
    (187, 187, M(3), ANY, 'vpmsetup'),
    (192, 239, ANY, ANY, 'fcmp'),
    (240, 244, ANY, ANY, 'vfmax'),
    (245, 245, MR(0, 2), ANY, 'fround'), (245, 245, M(3), ANY, 'ftoin'),
    (245, 245, MR(4, 6), ANY, 'ftrunc'), (245, 245, M(7), ANY, 'ftoiz'),
    (246, 246, MR(0, 2), ANY, 'ffloor'), (246, 246, M(3), ANY, 'ftouz'),
    (246, 246, MR(4, 6), ANY, 'fceil'), (246, 246, M(7), ANY, 'ftoc'),
    (247, 247, MR(0, 2), ANY, 'fdx'), (247, 247, MR(4, 6), ANY, 'fdy'),
    (248, 248, ANY, ANY, 'stvpm'),
    (252, 252, MR(0, 2), ANY, 'itof'), (252, 252, M(3), ANY, 'clz'),
    (252, 252, MR(4, 6), ANY, 'utof'),
]
MUL_OPS = [
    (1, 1, ANY, ANY, 'add'), (2, 2, ANY, ANY, 'sub'),
    (3, 3, ANY, ANY, 'umul24'), (4, 8, ANY, ANY, 'vfmul'),
    (9, 9, ANY, ANY, 'smul24'), (10, 10, ANY, ANY, 'multop'),
    (14, 14, ANY, ANY, 'fmov'), (15, 15, MR(0, 3), ANY, 'fmov'),
    (15, 15, M(4), M(0), 'nop'), (15, 15, M(7), ANY, 'mov'),
    (16, 63, ANY, ANY, 'fmul'),
]
# ops that take no inputs / one input (for printing)
NO_IN = {'nop', 'tidx', 'eidx', 'lr', 'vfla', 'vflna', 'vflb', 'vflnb',
         'fxcd', 'xcd', 'fycd', 'ycd', 'msf', 'revf', 'vdwwt', 'tmuwt',
         'vpmwt'}
ONE_IN = {'not', 'neg', 'flapush', 'flbpush', 'flpop', 'recip', 'setmsf',
          'setrevf', 'vpmsetup', 'fround', 'ftoin', 'ftrunc', 'ftoiz',
          'ffloor', 'ftouz', 'fceil', 'ftoc', 'fdx', 'fdy', 'itof', 'clz',
          'utof', 'fmov', 'mov', 'stvpm'}

BCOND = ['always', 'a0', 'na0', 'alla', 'anyna', 'anya', 'allna', '?7']
MSFIGN = ['', 'pixel', 'quad', '?3']
BDEST = ['abs', 'rel', 'link_reg', 'regfile']


def f(w, hi, lo): return (w >> lo) & ((1 << (hi - lo + 1)) - 1)


def find(table, op, mb, ma):
    for first, last, mbm, mam, name in table:
        if first <= op <= last and (mbm >> mb) & 1 and (mam >> ma) & 1:
            return name
    return None


def src(mux, raddr_a, raddr_b, small):
    if mux == 6: return 'rf%d' % raddr_a
    if mux == 7:
        return ('#%s' % SMALL_IMM[raddr_b] if raddr_b < 48 else '#?%d' % raddr_b) \
            if small else 'rf%d' % raddr_b
    return MUX[mux]


def dst(waddr, magic):
    if magic:
        n = MAGIC[waddr] if waddr < len(MAGIC) else None
        return n or 'magic%d' % waddr
    return 'rf%d' % waddr


def dis(w):
    op_mul = f(w, 63, 58)
    sig = f(w, 57, 53)
    if op_mul == 0 and sig & 0x18 == 0x18:
        # load immediate (libGLES v3d_qpu_instr_get_type: type 2); the low
        # signal bits select the mode ("32", "el_unsigned", "el_signed")
        mode = ['', '.el_unsigned', '.el_signed'][sig & 7] if sig & 7 < 3 else '.mode%d' % (sig & 7)
        t = 'ldi%s %s, %s, 0x%08x' % (mode, dst(f(w, 37, 32), f(w, 44, 44)),
                                     dst(f(w, 43, 38), f(w, 45, 45)), w & 0xffffffff)
        cond = f(w, 52, 46)
        return t + (' ; cond=0x%02x' % cond if cond else '')
    if op_mul == 0 and sig & 0x18 != 0x10:
        # libGLES types 3-5 (semaphore / barrier kinds, not decoded)
        return 'other(type %d) %016x' % (3 if sig & 0x1c == 0 else 4 if sig == 9 else 5, w)
    if op_mul == 0:
        # branch
        cond = f(w, 34, 32)
        msfign = f(w, 22, 21)
        bdu = f(w, 17, 15)
        ub = f(w, 14, 14)
        bdi = f(w, 13, 12)
        raddr_a = f(w, 11, 6)
        off = (f(w, 31, 24) << 24) | (f(w, 55, 35) << 3)
        if off & (1 << 31): off -= 1 << 32
        t = 'b.%s' % BCOND[cond]
        if msfign: t += '.' + MSFIGN[msfign]
        t += ' %s %+d' % (BDEST[bdi], off)
        if bdi == 3: t += ' rf%d' % raddr_a
        if ub: t += ' ; ub %s' % BDEST[bdu]
        return t
    cond = f(w, 52, 46)
    mm = f(w, 45, 45)
    ma = f(w, 44, 44)
    waddr_m = f(w, 43, 38)
    waddr_a = f(w, 37, 32)
    op_add = f(w, 31, 24)
    mul_b, mul_a = f(w, 23, 21), f(w, 20, 18)
    add_b, add_a = f(w, 17, 15), f(w, 14, 12)
    raddr_a, raddr_b = f(w, 11, 6), f(w, 5, 0)
    sigs = SIG.get(sig)
    small = sigs is not None and 'small_imm' in sigs
    an = find(ADD_OPS, op_add, add_b, add_a) or 'add?%d' % op_add
    mn = find(MUL_OPS, op_mul, mul_b, mul_a) or 'mul?%d' % op_mul

    def alu(name, op, waddr, magic, a, b):
        if name == 'nop': return 'nop'
        s = '%s' % name
        if name in ('fadd', 'fmin', 'fsub', 'fcmp', 'vfpack', 'vfmin', 'vfmax',
                    'fmul', 'vfmul', 'fround', 'fmov') and op not in (14, 15):
            s += '.%d' % op           # packed op number carries pack/unpack modes
        s += ' ' + dst(waddr, magic)
        if name in NO_IN: return s
        s += ', ' + src(a, raddr_a, raddr_b, small)
        if name in ONE_IN: return s
        return s + ', ' + src(b, raddr_a, raddr_b, small)

    t = alu(an, op_add, waddr_a, ma, add_a, add_b) + ' ; ' + \
        alu(mn, op_mul, waddr_m, mm, mul_a, mul_b)
    if sigs is None: t += ' ; sig?%d' % sig
    elif sigs: t += ' ; ' + ', '.join(sigs)
    if cond: t += ' ; cond=0x%02x' % cond
    return t


def enc(op_add=187, add_a=0, add_b=0, waddr_a=6, ma=1,
        op_mul=15, mul_a=0, mul_b=4, waddr_m=6, mm=1,
        sig=(), cond=0, raddr_a=0, raddr_b=0):
    """Pack an ALU instruction. Defaults give 'nop ; nop'."""
    s = SIG_INDEX[frozenset(sig)]
    return (op_mul << 58) | (s << 53) | (cond << 46) | (mm << 45) | (ma << 44) | \
        (waddr_m << 38) | (waddr_a << 32) | (op_add << 24) | (mul_b << 21) | \
        (mul_a << 18) | (add_b << 15) | (add_a << 12) | (raddr_a << 6) | raddr_b


def enc_ldi(imm, waddr_a=6, ma=1, waddr_m=6, mm=1, mode=0, cond=0):
    """Load immediate (32-bit mode 0), as in libGLES's no-colour clear shader."""
    return ((24 + mode) << 53) | (cond << 46) | (mm << 45) | (ma << 44) | \
        (waddr_m << 38) | (waddr_a << 32) | (imm & 0xffffffff)


def so_words(va, n):
    from elftools.elf.elffile import ELFFile
    p = 'stock/release/hal/egl/libGLES_nexus.so'
    e = ELFFile(open(p, 'rb')); d = open(p, 'rb').read()
    for s in e.iter_segments():
        if s['p_type'] == 'PT_LOAD' and s['p_vaddr'] <= va < s['p_vaddr'] + s['p_filesz']:
            o = va - s['p_vaddr'] + s['p_offset']
            return [struct.unpack_from('<Q', d, o + 8 * i)[0] for i in range(n)]
    raise SystemExit('address not in a load segment')


def main(a):
    if len(a) >= 2 and a[0] == 'dis' and a[1] != '-f':
        ws = [int(x, 16) for x in a[1:]]; base = 0
    elif len(a) == 5 and a[0] == 'dis':
        d = open(a[2], 'rb').read(); o = int(a[3], 0)
        ws = [struct.unpack_from('<Q', d, o + 8 * i)[0] for i in range(int(a[4], 0))]
        base = o
    elif len(a) == 3 and a[0] == 'so':
        base = int(a[1], 16); ws = so_words(base, int(a[2], 0))
    else:
        print(__doc__); return 1
    for i, w in enumerate(ws):
        print('%6x: %016x  %s' % (base + 8 * i, w, dis(w)))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
