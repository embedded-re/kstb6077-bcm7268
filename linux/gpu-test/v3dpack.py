#!/usr/bin/python3
# v3dpack - pack/unpack V3D 3.3 control-list packets and structs by their field
# names in Mesa's src/broadcom/cle/v3d_packet_v33.xml (put next to this file).
#   v3dpack.py NAME              list the fields
#   v3dpack.py NAME -u HEX       decode bytes (packets with their opcode byte)
import os, re, struct, sys, xml.etree.ElementTree as ET

XML = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'v3d_packet_v33.xml')

def norm(s):
    return re.sub(r'[^a-z0-9]+', '_', s.lower()).strip('_')

def ok_ver(e):
    lo = int(e.get('min_ver', '0')); hi = int(e.get('max_ver', '99'))
    return lo <= 33 <= hi

def bitpos(s):
    m = re.fullmatch(r'(\d+)b', s)
    return int(m.group(1)) * 8 if m else int(s)

defs = {}
for e in ET.parse(XML).getroot():
    if e.tag in ('packet', 'struct') and ok_ver(e):
        fields = []
        for f in e.findall('field'):
            fields.append((norm(f.get('name')), bitpos(f.get('start')), int(f.get('size')),
                           f.get('type'), f.get('default'), f.get('minus_one') == 'true'))
        code = int(e.get('code')) if e.get('code') else None
        nbits = max([s + n for _, s, n, *_ in fields] or [0])
        defs.setdefault(norm(e.get('name')), []).append((code, (nbits + 7) // 8, fields))
        if e.get('shortname'):
            defs.setdefault(norm(e.get('shortname')), []).append((code, (nbits + 7) // 8, fields))

def enc(t, v, size):
    if t == 'float':
        return struct.unpack('<I', struct.pack('<f', v))[0]
    if t == 'f187':
        return struct.unpack('<I', struct.pack('<f', v))[0] >> 16
    m = re.fullmatch(r'([su])(\d+)\.(\d+)', t or '')
    if m:
        return int(round(v * (1 << int(m.group(3))))) & ((1 << size) - 1)
    if t == 'bool':
        return 1 if v else 0
    return int(v) & ((1 << size) - 1)

def pack(name, **kw):
    code, nbytes, fields = defs[norm(name)][0]
    val = 0
    names = {f[0] for f in fields}
    for k in kw:
        if k not in names:
            raise KeyError('%s: no field %s (have %s)' % (name, k, sorted(names)))
    for fname, start, size, t, default, minus_one in fields:
        v = kw.get(fname, default)
        if v is None:
            continue
        v = float(v) if t in ('float', 'f187') or (t and '.' in t) else int(v, 0) if isinstance(v, str) else v
        if minus_one:
            v -= 1
        if t == 'address':
            # address fields hold the address's own top bits
            val |= (int(v) >> (32 - size)) << start
            continue
        val |= enc(t, v, size) << start
    b = val.to_bytes(nbytes, 'little')
    return (bytes([code]) if code is not None else b'') + b

def unpack(name, b):
    code, nbytes, fields = defs[norm(name)][0]
    if code is not None:
        b = b[1:]
    val = int.from_bytes(b[:nbytes], 'little')
    out = {}
    for fname, start, size, t, default, minus_one in fields:
        v = (val >> start) & ((1 << size) - 1)
        if t == 'address':
            v <<= 32 - size
        if minus_one:
            v += 1
        out[fname] = v
    return out

if __name__ == '__main__':
    n = sys.argv[1]
    if len(sys.argv) > 2 and sys.argv[2] == '-u':
        print(unpack(n, bytes.fromhex(sys.argv[3])))
    else:
        for code, nb, fields in defs[norm(n)]:
            print(code, nb, [(f[0], f[1], f[2], f[3]) for f in fields])
