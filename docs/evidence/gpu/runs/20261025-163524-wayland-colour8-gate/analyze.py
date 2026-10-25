"""Verify the colour-depth milestone against archived buffer bytes and logs."""
from pathlib import Path
import collections,hashlib,json,lzma,re
from PIL import Image
root=Path(__file__).resolve().parent
assert json.loads((root/'state.json').read_text())==dict(state='complete',sessions=5)
rows=json.loads((root/'results.json').read_text());frames=checks=0
for row in rows:
 p=root/row['label'];assert row['exit']==0
 assert 'Total 0 objects, 0 bytes' in (p/'dmabuf-after.txt').read_text()
 drm=(p/'drm-state.txt').read_text()
 assert 'allocated by = sway' in drm
 assert ('format=RG16' if row['label']=='cpu-fallback' else 'format=AR24') in drm
 if row['label'].startswith('native-'):
  text=(p/'client.txt').read_text();assert 'GL renderer: V3D 3.3' in text and 'FAIL' not in text
  assert row['fps']>=55 and row['minimum_reported_fps']>=45
  assert len(set(json.loads((p/'hdmi-crc.json').read_text())))>=2
  frames+=row['frames'];checks+=int(re.search(r'PASS:.*marker_checks (\d+)',text).group(1))
 if row['label']=='native-protocol':assert row['buffer_protocol']=='linux-dmabuf'
 if row['label']=='cpu-fallback':assert row['gpu_render_jobs_delta']==0
 if row['label']=='older-package':assert any('/opt/mesa/23.3.6-85a951cd/lib/' in p.read_text() for p in p.glob('maps-*.txt'))
assert 'OK: 100 native fence export/import/server-wait chains, exact pixels' in (root/'native-fence.txt').read_text()
ramp=root.parent/'20261025-163337-wayland-colour8-ramps'
raw=lzma.decompress((ramp/'scanout.bgra.xz').read_bytes())
assert len(raw)==1920*1080*4
assert collections.Counter(raw[3::4])=={255:1920*1080}
im=Image.open(ramp/'fullscreen.png').convert('RGBA');assert im.size==(1920,1080)
for row,y in enumerate((87,71,55,39)):
 for i in range(256):
  rgb=tuple(i if row in (0,c+1) else 0 for c in range(3))
  pos=y*7680+(32+i)*4
  assert raw[pos:pos+4]==bytes(rgb[::-1]+(255,))
  assert im.getpixel((32+i,y))==rgb+(255,)
for xy,rgb in [((8,8),(0,0,255)),((8,1072),(255,0,0)),((1912,1072),(0,255,0))]:assert im.getpixel(xy)==rgb+(255,)
for name in ('20261025-163144-wayland-colour8-probe','20261025-163337-wayland-colour8-ramps','20261025-163431-wayland-colour8-gate',root.name):
 p=root.parent/name
 assert not re.search(r'Resetting GPU|MMU error|Oops|BUG:|Unable to handle|Internal error|kmsg overrun',(p/'kernel.txt').read_text())
meta=json.loads((root/'meta.json').read_text())
for local,board in [('tv-tested','/usr/bin/tv'),('sway-cpu-tested.conf','/etc/kstb-tv/sway.conf'),('sway-gpu-tested.conf','/etc/kstb-tv/sway-gpu.conf')]:assert hashlib.sha256((root/local).read_bytes()).hexdigest()==meta['sha256'][board]
final=(root/'final-board-state.txt').read_text();assert 'Total 0 objects, 0 bytes' in final and 'allocated by = [fbcon]' in final
summary=dict(final_sessions=5,native_frames=frames,checked_client_pixels=checks,scanout_format='ARGB8888',scanout_ramp_pixels=1024,screenshot_ramp_pixels=1024,opaque_alpha_pixels=1920*1080,native_fence_chains=100,fps=[r['fps'] for r in rows if r['label'].startswith('native-')],gpu_faults=0,dmabuf_objects_after_exit=0)
(root/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary,indent=2))
