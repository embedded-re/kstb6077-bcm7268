"""Bounded compute latency/queue accounting milestone; stop at first fault."""
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
OUT = Path('/root/gpu-runs') / (datetime.datetime.now().strftime('%Y%m%d-%H%M%S') + '-compute-latency-milestone')
BAD = re.compile(r'MMU error|Oops|BUG:|Unable to handle|Internal error|kmsg overrun')
OUT.mkdir()
shutil.copyfile(__file__, OUT / 'run.py')
Path('/tmp/kstb-compute-latency-current').write_text(str(OUT) + '\n')
for path in Path('/proc').glob('[0-9]*/cmdline'):
    try:
        names = {Path(x.decode()).name for x in path.read_bytes().split(b'\0') if x}
    except (OSError, UnicodeError):
        continue
    assert not names.intersection({'deqp-gles31', 'deqp-gles3', 'deqp-gles2', 'deqp-egl', 'kstb-gpu-run', 'kstb-neural-test', 'kstb-compute-sync-test', 'kstb-compute-latency-test'}), (path, names)
env = os.environ.copy()
line = subprocess.check_output(['kstb-mesa', 'env', BUILD], text=True).strip()
assert line.startswith('export ')
env.update(dict(x.split('=', 1) for x in line[7:].split()))
for k in ('MESA_GLES_VERSION_OVERRIDE', 'MESA_GLSL_VERSION_OVERRIDE', 'V3D_DEBUG', 'KSTB_RESET_PROBE', 'LD_PRELOAD'):
    env.pop(k, None)
env.update(V3D_COMPUTE33='1', MESA_SHADER_CACHE_DISABLE='true')
meta = dict(build=BUILD, uname=subprocess.check_output(['uname', '-a'], text=True).strip(),
            selected=os.readlink('/opt/mesa/current'), environment={k: v for k, v in env.items() if k.startswith(('MESA_', 'V3D_', 'LD_LIBRARY_', 'LIBGL_'))}, sha256={})
for path in (Path('/opt/gpu-test/kstb-neural-test'), Path('/opt/gpu-test/kstb-compute-latency-test'), Path('/opt/gpu-test/kstb-compute-sync-test'), Path('/opt/gpu-test/kstb-reset-probe'), Path('/opt/mesa') / BUILD / 'lib/dri/v3d_dri.so'):
    meta['sha256'][str(path)] = hashlib.sha256(path.read_bytes()).hexdigest()
(OUT / 'meta.json').write_text(json.dumps(meta, indent=2) + '\n')
stop = threading.Event()
reader = runpy.run_path('/opt/gpu-test/kstb-gpu-run')['kmsg_reader']
thread = threading.Thread(target=reader, args=(str(OUT / 'kernel.txt'), stop))
thread.start()
time.sleep(.4)
Path('/dev/kmsg').write_text('kstb neural milestone start\n')
results = []
expected_resets = 0
power = Path('/sys/bus/platform/devices/f1200000.gpu/power/control')
original_power = power.read_text().strip()
for name, command in {
    'interrupts-before.txt': ['cat', '/proc/interrupts'],
    'clocks.txt': ['cat', '/sys/kernel/debug/clk/clk_summary'],
    'power-before.txt': ['sh', '-c', 'cat /sys/bus/platform/devices/f1200000.gpu/power/control /sys/bus/platform/devices/f1200000.gpu/power/runtime_status /sys/bus/platform/devices/f1200000.gpu/power/runtime_active_time /sys/bus/platform/devices/f1200000.gpu/power/runtime_suspended_time'],
}.items():
    with (OUT / name).open('w') as f:
        subprocess.run(command, stdout=f, stderr=subprocess.STDOUT, check=True)

def kernel_ok():
    text = (OUT / 'kernel.txt').read_text()
    assert not BAD.search(text), 'kernel fault'
    assert text.count('Resetting GPU') <= expected_resets, 'unexpected GPU reset'

def test(label, args, reset=False, timeout=120):
    global expected_resets
    expected_resets += int(reset)
    childenv = env.copy()
    if reset:
        childenv['KSTB_RESET_PROBE'] = '1'
    kernel_ok()
    Path('/dev/kmsg').write_text('kstb neural milestone ' + label + '\n')
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
    tool = '/opt/gpu-test/kstb-compute-latency-test'
    test('empty-smoke', [tool, '1', '0', '3', '1', 'empty'])
    test('store-smoke', [tool, '1', '1', '3', '1', 'store'])
    test('empty-1001', [tool, '1', '0', '1001', '1', 'empty'])
    test('store-1001', [tool, '1', '0', '1001', '1', 'store'])
    for jobs in (1, 2, 4, 16, 64):
        test(f'queued-{jobs}-jobs', [tool, str(jobs * 16), '0', '31', '1', 'store'])
        if jobs > 1:
            test(f'separate-{jobs}-dispatches', [tool, '1', '0', '31', str(jobs), 'store'])
    for loops in (0, 16, 64, 256, 1024, 4096):
        test(f'work-loops-{loops}', [tool, '16', str(loops), '31', '1', 'store'])
    # Device says runtime PM unsupported; still make the requested reversible
    # auto/on comparison, restoring the original control on every exit.
    power.write_text('on')
    test('power-on-empty', [tool, '1', '0', '101', '1', 'empty'])
    test('power-on-store', [tool, '16', '0', '101', '1', 'store'])
    power.write_text(original_power)
    test('power-restored-store', [tool, '16', '0', '101', '1', 'store'])
    test('sync-200', ['/opt/gpu-test/kstb-compute-sync-test', '200', '4097'], timeout=240)
    for batch in (1, 65, 257):
        test(f'post-probe-neural-b{batch}', ['/opt/gpu-test/kstb-neural-test', str(batch), '128', '11'])
    state = 'complete'
finally:
    power.write_text(original_power)
    for name, command in {
        'interrupts-after.txt': ['cat', '/proc/interrupts'],
        'power-after.txt': ['sh', '-c', 'cat /sys/bus/platform/devices/f1200000.gpu/power/control /sys/bus/platform/devices/f1200000.gpu/power/runtime_status /sys/bus/platform/devices/f1200000.gpu/power/runtime_active_time /sys/bus/platform/devices/f1200000.gpu/power/runtime_suspended_time'],
    }.items():
        with (OUT / name).open('w') as f:
            subprocess.run(command, stdout=f, stderr=subprocess.STDOUT, check=True)
    subprocess.run('killall watchdog; sleep 1; printf V > /dev/watchdog', shell=True, check=True)
    time.sleep(.5)
    stop.set()
    thread.join()
    (OUT / 'state.json').write_text(json.dumps(dict(state=state, tests=len(results), expected_resets=expected_resets), indent=2)+'\n')
    print(str(OUT), state, flush=True)
