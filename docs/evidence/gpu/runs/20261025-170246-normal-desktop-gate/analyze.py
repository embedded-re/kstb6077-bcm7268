"""Verify native package migration, restored rollback and the normal desktop."""
from pathlib import Path
import json,re
from PIL import Image
r=Path(__file__).resolve().parent
assert json.loads((r/'state.json').read_text())==dict(state='complete',sessions=3)
assert 'app exit and closing all terminals leave the desktop alive' in (r/'persistent.txt').read_text()
assert 'format=AR24' in (r/'drm-state.txt').read_text()
assert 'Total 0 objects, 0 bytes' in (r/'dmabuf-after.txt').read_text()
assert 'allocated by = [fbcon]' in (r/'final-state.txt').read_text()
for f in r.glob('maps-*.txt'):
 text=f.read_text();assert '/usr/lib/dri/' in text and '/opt/mesa/' not in text and '/usr/local/lib/' not in text
for f in r.glob('dri-environment-*.json'):
 env=json.loads(f.read_text());assert not any(s.startswith(('LD_LIBRARY_PATH=','LIBGL_DRIVERS_PATH=','GBM_BACKENDS_PATH=')) for s in env)
text=(r/'client.txt').read_text();assert 'GL renderer: V3D 3.3' in text and 'FAIL' not in text
fps=float(re.search(r'PASS:.* fps ([\d.]+)',text).group(1));assert fps>=55
frames=int(re.search(r'PASS: frames (\d+)',text).group(1));checks=int(re.search(r'PASS:.*marker_checks (\d+)',text).group(1))
im=Image.open(r/'fullscreen.png').convert('RGBA');assert im.size==(1920,1080)
for row,y in enumerate((87,71,55,39)):
 for i in range(256):assert im.getpixel((32+i,y))==tuple(i if row in (0,c+1) else 0 for c in range(3))+(255,)
m=r.parent/'20261025-165747-system-mesa-migration'
assert json.loads((m/'state.json').read_text())==dict(state='complete')
assert json.loads((m/'rollback-verified.json').read_text())==dict(local_hashes_match=True,world_matches=True)
assert '0 pixel checks failed' in (m/'msaa.txt').read_text()
assert 'OK: 100 native fence' in (m/'fence.txt').read_text()
assert 'GL_VERSION=OpenGL ES 3.0 Mesa 23.3.6' in (m/'final-caps3.txt').read_text()
final=(r/'final-board-state.txt').read_text()
for name in ('mesa','mesa-dev','mesa-egl','mesa-gbm','mesa-gl','mesa-gles'):
 assert name+'-23.3.6-r33' in final
 assert name+'=23.3.6-r33' in final
assert '/usr/lib/dri/v3d_dri.so is owned by mesa-23.3.6-r33' in final
for name in ('20261025-164959-native-system-mesa-preview','20261025-165747-system-mesa-migration','20261025-170151-normal-desktop-gate',r.name):
 assert not re.search(r'Resetting GPU|MMU error|Oops|BUG:|Unable to handle|Internal error|kmsg overrun',(r.parent/name/'kernel.txt').read_text())
summary=dict(system_mesa='23.3.6-r33',prefix='/usr',persistent_desktop=True,desktop_sessions=2,cpu_fallback=True,native_frames=frames,client_checked_pixels=checks,fps=fps,screenshot_ramp_pixels=1024,native_fence_chains=100,rollback_restored_and_reinstalled=True,gpu_faults=0,dmabuf_objects_after_exit=0)
(r/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary,indent=2))
