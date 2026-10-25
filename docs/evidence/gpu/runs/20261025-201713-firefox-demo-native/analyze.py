"""Offline assertions on the native browser gate's recorded evidence."""
import json,re
from pathlib import Path
P=Path(__file__).resolve().parent
read=lambda n:json.loads((P/n).read_text())
st=read('state.json');assert st['state']=='complete' and st['fault'] is None
assert not re.search(r'Resetting GPU|MMU error|Oops|BUG:|Unable to handle|Internal error|kmsg overrun',(P/'kernel.txt').read_text())
g=read('graphics.json')['graphics'];assert g['windowLayerManagerType']=='WebRender';assert g['numAcceleratedWindows']==g['numTotalWindows']==1
assert g['adapterDescription']=='V3D 3.3' and 'V3D 3.3' in g['webgl1Renderer']
a=read('launch.json');assert not any(x.startswith(('LIBGL_DRIVERS_PATH=','LD_LIBRARY_PATH=','MESA_','V3D_')) for x in a)
assert any('/usr/lib/dri/v3d_dri.so' in p.read_text() for p in P.glob('maps-*.txt'))
assert (P/'driver.sha256').read_text().strip()=='ab8e3f40c4d368bc46661342bde624de3051a249aeed1b02f220e39b6e8bcb8a'
d=read('demo-state.json');assert d['status']=='running' and d['failures']==0 and d['checks']>0
samples=read('samples.json');assert len(samples)==3
for s in samples:
 assert s['after']['frames']>s['before']['frames'] and s['after']['failures']==0 and s['after']['checks']>0
assert read('internet.json')['title']=='Wikipedia'
print('PASS: native hardware WebRender/WebGL, demo, three reload/scroll cycles, zero GPU faults')
