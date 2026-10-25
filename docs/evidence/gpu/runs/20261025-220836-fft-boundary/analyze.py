#!/usr/bin/env python3
"""Validate all three completed FFT gates without executing hardware work."""
from pathlib import Path
import hashlib
import json
import re
p=Path(__file__).resolve().parent
runs=[p.parent/'20261025-220730-fft-smoke',p.parent/'20261025-220755-fft-full',p]
case_count=dispatches=transforms=0
worst_forward=worst_inverse=0.0
for root in runs:
    state=json.loads((root/'state.json').read_text())
    assert state['state']=='complete' and state['fault'] is None
    assert state['driver_sha256']=='ab8e3f40c4d368bc46661342bde624de3051a249aeed1b02f220e39b6e8bcb8a'
    assert state['boot_id']=='3c548c6c-e2ce-4629-b9de-fc863743c1cb'
    assert not re.search(r'Resetting GPU|MMU error|Oops|BUG:|Unable to handle|Internal error|kmsg overrun',(root/'kernel.txt').read_text())
    assert '100 native fence' in (root/'fences.txt').read_text()
    assert '0 pixel checks failed' in (root/'graphics.txt').read_text()
    for name in state['completed']:
        if not name.startswith('fft-'):
            continue
        case_count+=1
        n,batch=map(int,name.removeprefix('fft-').split('-'))
        text=(root/(name+'.txt')).read_text()
        assert 'git-8b05308' in text and 'renderer=V3D 3.3' in text
        rows=re.findall(r'^round=.*$',text,re.M)
        assert len(rows)==6 and all('guards=PASS' in row for row in rows)
        for row in rows:
            fields=dict(re.findall(r'(\w+)=([^ ]+)',row))
            f=float(fields['forward_relative_l2']);b=float(fields['inverse_max_abs'])
            assert 0<=f<3e-5 and 0<=b<3e-5
            worst_forward=max(worst_forward,f);worst_inverse=max(worst_inverse,b)
        result=re.search(r'RESULT=PASS .*dispatches=(\d+)',text)
        assert result and int(result[1])==12
        assert 'MEDIAN cpu1_ms=' in text
        dispatches+=12;transforms+=12*batch
    for entry in (root/'SHA256SUMS').read_text().splitlines():
        digest,name=entry.split('  ',1)
        assert hashlib.sha256((root/name).read_bytes()).hexdigest()==digest,name
post=json.loads((p/'post-check.json').read_text())
assert post['kernel_resets_since_boot']==0
for name in ('watchdog','sway','firefox','supertux2','kstb-fft-test'):
    assert post[name+'_pids']==[]
elf=(p/'elf-dynamic.txt').read_text()
assert 'RPATH' not in elf and 'RUNPATH' not in elf
assert 'shared vec2 s[N]' in (p/'kstb-fft-test.c').read_text()
assert (case_count,dispatches,transforms)==(11,132,2544)
print(f'PASS {case_count} FFT cases, {dispatches} dispatches, {transforms} transforms; forward L2 {worst_forward:.9g}, inverse abs {worst_inverse:.9g}; fences/graphics/kernel/cleanup/manifests')
