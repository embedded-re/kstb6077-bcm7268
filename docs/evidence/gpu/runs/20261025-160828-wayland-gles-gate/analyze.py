"""Verify native Wayland rendering, the scanout comparison and final gate."""
from pathlib import Path
import json,re,hashlib
root=Path(__file__).resolve().parent
assert json.loads((root/'state.json').read_text())==dict(state='complete',sessions=5)
rows=json.loads((root/'results.json').read_text())
frames=checks=0
for row in rows:
    path=root/row['label']
    assert row['exit']==0
    assert 'Total 0 objects, 0 bytes' in (path/'dmabuf-after.txt').read_text()
    if row['label'].startswith('native-'):
        text=(path/'client.txt').read_text()
        assert 'GL renderer: V3D 3.3' in text and 'FAIL' not in text
        assert row['fps']>=55 and row['minimum_reported_fps']>=45
        assert len(set(json.loads((path/'hdmi-crc.json').read_text())))>=2
        frames+=row['frames'];checks+=int(re.search(r'PASS:.*marker_checks (\d+)',text).group(1))
    if row['label']=='native-protocol':assert row['buffer_protocol']=='linux-dmabuf'
    if row['label']=='cpu-fallback':assert row['gpu_render_jobs_delta']==0
assert 'OK: 100 native fence export/import/server-wait chains, exact pixels' in (root/'native-fence.txt').read_text()
comparison=root.parent/'20261025-160642-wayland-pacing-ab'
ab=json.loads((comparison/'results.json').read_text())
assert not ab[0]['performance_passed'] and ab[1]['performance_passed']
for directory in (root,comparison,root.parent/'20261025-160343-wayland-gles-gate',root.parent/'20261025-155723-wayland-gles-visible'):
    assert not re.search(r'Resetting GPU|MMU error|Oops|BUG:|Unable to handle|Internal error|kmsg overrun',(directory/'kernel.txt').read_text())
meta=json.loads((root/'meta.json').read_text())
assert hashlib.sha256((root/'tv-tested').read_bytes()).hexdigest()==meta['sha256']['/usr/bin/tv']
final=(root/'final-board-state.txt').read_text()
assert 'Total 0 objects, 0 bytes' in final and 'allocated by = [fbcon]' in final
summary=dict(final_sessions=5,native_client_runs=3,native_frames=frames,client_marker_checks=checks,native_fence_chains=100,
             fps=[row['fps'] for row in rows if row['label'].startswith('native-')],
             direct_scanout_comparison=ab,resets=0,mmu_errors=0,dmabuf_objects_after_exit=0)
(root/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary,indent=2))
