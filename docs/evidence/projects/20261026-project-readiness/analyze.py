#!/usr/bin/env python3
"""Validate recorded preparation outcomes and archive integrity offline."""
import hashlib
import json
from pathlib import Path
p=Path(__file__).resolve().parent
b=json.loads((p/'benchmark.json').read_text())
assert b['result']=='PASS' and b['top5'][0]['index']==258
assert b['providers']==['CPUExecutionProvider']
assert [r['threads'] for r in b['benchmarks']]==[1,2,4]
for row in b['benchmarks']:
    assert len(row['samples_ms'])==20 and 0<row['median_ms']<=row['p95_ms']
s=json.loads((p/'soak.json').read_text())
assert s['result']=='PASS' and s['soak']['seconds']>=300
assert s['soak']['memory_pattern_mib']==256 and s['soak']['memory_check']=='PASS'
assert s['soak']['repeatability']=='PASS' and s['soak']['inferences']>100
assert s['soak']['max_temp_c']<80 and s['soak']['min_available_kib']>256*1024
state=json.loads((p/'soak-state.json').read_text())
assert state['state']=='complete' and state['fault'] is None and state['returncode']==0
post=json.loads((p/'post-state.json').read_text())
assert post['driver_sha256']=='ab8e3f40c4d368bc46661342bde624de3051a249aeed1b02f220e39b6e8bcb8a'
assert '#30' in post['kernel']
for name in ('mesa','mesa-dev','mesa-egl','mesa-gbm','mesa-gl','mesa-gles'):
    assert name+'=23.3.6-r34' in post['world'].splitlines()
assert post['root_free_bytes']>900*1024*1024
assert post['kernel_resets_since_boot']==0 and post['test_processes']==[]
assert post['project_uid']==1000
assert post['workspace_restore_check']=='PASS' and post['classifier_processes']==[]
assert json.loads((p/'workspace-backup.json').read_text())['workspace_restore_check']=='PASS'
assert 'GL_VERSION=OpenGL ES 3.0' in (p/'gpu-default-caps.txt').read_text()
pipe=json.loads((p/'pipeline-benchmark.json').read_text())
assert pipe['result']=='PASS' and pipe['expected_class']==258 and len(pipe['samples_ms'])==20
cleanup=json.loads((p/'cleanup-result.json').read_text())
assert cleanup['reclaimed_bytes']==530481152
before=(p/'packages-before-projects.txt').read_text()
after=(p/'packages-after-projects.txt').read_text()
for name in ('py3-numpy','py3-pillow','py3-onnxruntime','py3-pip'):
    assert name+'-' in after
boot=json.loads((p/'boot-images.json').read_text())
assert boot['KRN']['bytes']==64*1024*1024 and boot['DTB']['bytes']==4*1024*1024
for line in (p/'SHA256SUMS').read_text().splitlines():
    digest,name=line.split('  ',1)
    assert hashlib.sha256((p/name).read_bytes()).hexdigest()==digest,name
print('PASS: model/thread benchmark, 300s CPU/RAM gate, pipeline, recovery metadata, Mesa pins, workspace backup, hashes')
