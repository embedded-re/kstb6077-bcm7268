"""Bounded neural-compute measurement/reliability milestone; stop at first fault."""
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import runpy
import shutil
import signal
import subprocess
import threading
import time

BUILD = '23.3.6-5a3fd8dd'
OUT = Path('/root/gpu-runs') / (datetime.datetime.now().strftime('%Y%m%d-%H%M%S') + '-gemm-spill-fixed')
BAD = re.compile(r'MMU error|Oops|BUG:|Unable to handle|Internal error|kmsg overrun')
OUT.mkdir()
shutil.copyfile(__file__, OUT / 'run.py')
Path('/root/gpu-kernel-ab/gemm/run-path').write_text(str(OUT) + '\n')
for path in Path('/proc').glob('[0-9]*/cmdline'):
    try:
        names = {Path(x.decode()).name for x in path.read_bytes().split(b'\0') if x}
    except (OSError, UnicodeError):
        continue
    assert not names.intersection({'deqp-gles31', 'deqp-gles3', 'deqp-gles2', 'deqp-egl', 'kstb-gpu-run', 'kstb-neural-test', 'kstb-compute-sync-test', 'kstb-gemm-test'}), (path, names)
env = os.environ.copy()
line = subprocess.check_output(['kstb-mesa', 'env', BUILD], text=True).strip()
assert line.startswith('export ')
env.update(dict(x.split('=', 1) for x in line[7:].split()))
for k in ('MESA_GLES_VERSION_OVERRIDE', 'MESA_GLSL_VERSION_OVERRIDE', 'V3D_DEBUG', 'KSTB_RESET_PROBE', 'LD_PRELOAD'):
    env.pop(k, None)
env.update(V3D_COMPUTE33='1', MESA_SHADER_CACHE_DISABLE='true')
meta = dict(build=BUILD, uname=subprocess.check_output(['uname', '-a'], text=True).strip(),
            selected=os.readlink('/opt/mesa/current'), environment={k: v for k, v in env.items() if k.startswith(('MESA_', 'V3D_', 'LD_LIBRARY_', 'LIBGL_'))}, sha256={})
for path in (Path('/opt/gpu-test/kstb-neural-test'), Path('/opt/gpu-test/kstb-gemm-test'), Path('/opt/gpu-test/kstb-compute-sync-test'), Path('/opt/gpu-test/kstb-reset-probe'), Path('/opt/mesa') / BUILD / 'lib/dri/v3d_dri.so'):
    meta['sha256'][str(path)] = hashlib.sha256(path.read_bytes()).hexdigest()
for scratch in ('/tmp/kstb-gemm-fixed1/v3d_dri.so','/tmp/kstb-gemm-fixed4/v3d_dri.so','/tmp/kstb-gemm-fixed/v3d_dri.so'):
    meta['sha256'][scratch] = hashlib.sha256(Path(scratch).read_bytes()).hexdigest()
assert '-kstb-hrtimer-ab ' in meta['uname'], meta['uname']
(OUT / 'v3d-ident.txt').write_text(Path('/sys/kernel/debug/dri/0/v3d_ident').read_text())
(OUT / 'cpuinfo.txt').write_text(Path('/proc/cpuinfo').read_text())
(OUT / 'meta.json').write_text(json.dumps(meta, indent=2) + '\n')
stop = threading.Event()
reader = runpy.run_path('/opt/gpu-test/kstb-gpu-run')['kmsg_reader']
thread = threading.Thread(target=reader, args=(str(OUT / 'kernel.txt'), stop))
thread.start()
time.sleep(.4)
Path('/dev/kmsg').write_text('kstb gemm isolation start\n')
results = []
expected_resets = 0

def kernel_ok():
    text = (OUT / 'kernel.txt').read_text()
    assert not BAD.search(text), 'kernel fault'
    assert text.count('Resetting GPU') <= expected_resets, 'unexpected GPU reset'

def test(label, args, reset=False, timeout=300, extra_env=None):
    global expected_resets
    expected_resets += int(reset)
    childenv = env.copy()
    if extra_env: childenv.update(extra_env)
    if reset:
        childenv['KSTB_RESET_PROBE'] = '1'
    kernel_ok()
    Path('/dev/kmsg').write_text('kstb gemm isolation ' + label + '\n')
    start = time.monotonic()
    with (OUT / (label + '.txt')).open('w') as log:
        proc = subprocess.Popen(args, env=childenv, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
        try:
            while proc.poll() is None:
                kernel_ok()
                assert time.monotonic() - start < timeout, 'test timeout'
                time.sleep(.1)
        finally:
            if proc.poll() is None:
                os.killpg(proc.pid, signal.SIGTERM)
                try:
                    proc.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    os.killpg(proc.pid, signal.SIGKILL)
                    proc.wait()
    time.sleep(.3)
    kernel_ok()
    assert proc.returncode == 0, (label, proc.returncode)
    text = (OUT / (label + '.txt')).read_text()
    assert 'PASS' in text and 'FAIL' not in text, label
    assert (OUT / 'kernel.txt').read_text().count('Resetting GPU') == expected_resets, 'missing expected reset'
    if reset:
        assert 'compute fence pending at uptime' in text, 'no pending compute during reset'
    results.append(dict(test=label, args=args, exit=proc.returncode, seconds=time.monotonic()-start, expected_resets=expected_resets))
    (OUT / 'results.json').write_text(json.dumps(results, indent=2)+'\n')
    print(label, 'PASS', flush=True)

subprocess.run(['watchdog', '-T', '30', '-t', '5', '/dev/watchdog'], check=True)
state = 'stopped'
try:
    tool='/opt/gpu-test/kstb-gemm-test'
    for force in (1,4):
        for mode,n in [('naive',32),('naive',65),('shared1',8),('shared1',32),('shared1',64)]:
            label=f'force{force}-{mode}-n{n}'
            test(label,[tool,mode,str(n),'5'],extra_env={'V3D_DEBUG':'shaderdb','LIBGL_DRIVERS_PATH':f'/tmp/kstb-gemm-fixed{force}'})
            assert re.search(r'[1-9][0-9]*:[0-9]+ spills:fills',(OUT/(label+'.txt')).read_text()), 'forced spill missing'
    fixed={'V3D_DEBUG':'shaderdb','LIBGL_DRIVERS_PATH':'/tmp/kstb-gemm-fixed'}
    test('tiled2-n32',[tool,'tiled2','32','5'],extra_env=fixed)
    test('tiled2-n64',[tool,'tiled2','64','5'],extra_env=fixed)
    test('sync-100',['/opt/gpu-test/kstb-compute-sync-test','100','4097'],timeout=120,extra_env=fixed)
    test('neural-post',['/opt/gpu-test/kstb-neural-test','257','128','11'],extra_env=fixed)
    state = 'complete'
finally:
    subprocess.run('killall watchdog; sleep 1; printf V > /dev/watchdog', shell=True, check=True)
    time.sleep(.5)
    stop.set()
    thread.join()
    (OUT / 'state.json').write_text(json.dumps(dict(state=state, tests=len(results), expected_resets=expected_resets), indent=2)+'\n')
    print(str(OUT), state, flush=True)
