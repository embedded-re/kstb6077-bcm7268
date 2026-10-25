"""Check the installed-launcher gate, without starting a compositor."""
from pathlib import Path
import hashlib
import json
import re
root = Path(__file__).resolve().parent
assert json.loads((root/'state.json').read_text()) == {'state':'complete','sessions':5}
meta = json.loads((root/'meta.json').read_text())
assert hashlib.sha256((root/'tv-tested').read_bytes()).hexdigest() == meta['sha256']['/usr/bin/tv']
assert not re.search(r'Resetting GPU|MMU error|Oops|BUG:|Unable to handle|Internal error|kmsg overrun', (root/'kernel.txt').read_text())
rows = json.loads((root/'results.json').read_text())
for row in rows:
    path = root/row['label']
    env = json.loads((path/'environment.json').read_text())
    gpu = row['selector']=='--gpu'
    assert env['WLR_RENDERER'] == ('gles2' if gpu else 'pixman')
    assert row['exit']==0 and row['console']=='tty1'
    assert row['gpu_render_jobs_delta']>0 if gpu else row['gpu_render_jobs_delta']==0
    assert len(set(json.loads((path/'hdmi-crc.json').read_text())))>=2
    state = (path/'drm-state.txt').read_text()
    assert 'format=RG16' in state and 'allocated by = sway' in state
final = (root/'final-board-state.txt').read_text()
assert 'Total 0 objects, 0 bytes' in final and 'allocated by = [fbcon]' in final
summary = dict(sessions=5,gpu_sessions=3,cpu_sessions=2,
               measured_gpu_render_jobs=sum(row['gpu_render_jobs_delta'] for row in rows),
               resets=0,mmu_errors=0,console_restored=True,dmabuf_objects_after_exit=0)
assert summary['measured_gpu_render_jobs']==360
(root/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
print(json.dumps(summary,indent=2))
