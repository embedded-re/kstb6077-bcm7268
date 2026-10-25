"""Single-workload, bounded post-reboot FFT gate; no existing apps signalled."""
import datetime
import hashlib
import json
import os
from pathlib import Path
import runpy
import signal
import subprocess
import sys
import threading
import time

mode = sys.argv[1]
assert mode in ('smoke', 'full', 'boundary')
assert not subprocess.run(['pidof', 'watchdog'], capture_output=True).stdout.strip(), 'existing watchdog'
assert not subprocess.run(['pidof', 'sway', 'firefox', 'supertux2'], capture_output=True).stdout.strip(), 'desktop started: stop isolated gate'
root = Path('/root/gpu-runs') / (datetime.datetime.now().strftime('%Y%m%d-%H%M%S') + '-fft-' + mode)
root.mkdir()
(root / 'run.py').write_bytes(Path(__file__).read_bytes())
Path('/root/math-audit/fft-path').write_text(str(root))
state = dict(state='stopped', mode=mode, boot_id=Path('/proc/sys/kernel/random/boot_id').read_text().strip(),
             driver_sha256=hashlib.sha256(Path('/usr/lib/dri/v3d_dri.so').read_bytes()).hexdigest(),
             binary_sha256=hashlib.sha256(Path('/root/math-audit/kstb-fft-test').read_bytes()).hexdigest(),
             kernel=subprocess.check_output(['uname','-a'],text=True).strip(), completed=[], fault=None)
(root / 'meminfo-before.txt').write_text(Path('/proc/meminfo').read_text())
(root / 'state.json').write_text(json.dumps(state,indent=2))
stop = threading.Event()
thread = threading.Thread(target=runpy.run_path('/opt/gpu-test/kstb-gpu-run')['kmsg_reader'],args=(str(root/'kernel.txt'),stop))
thread.start()
time.sleep(.25)
guard = runpy.run_path('/opt/gpu-test/kstb_fault_guard.py')['KernelFaultGuard'](root/'kernel.txt').start()
watchdog = subprocess.Popen(['watchdog','-F','-T','30','-t','5','/dev/watchdog'], start_new_session=True)
env = dict(os.environ, V3D_COMPUTE33='1', MESA_SHADER_CACHE_DISABLE='true')
for key in ('LIBGL_DRIVERS_PATH','LD_LIBRARY_PATH','MESA_GLES_VERSION_OVERRIDE','KSTB_RESET_PROBE'):
    env.pop(key,None)
state['environment'] = {k:env[k] for k in ('V3D_COMPUTE33','MESA_SHADER_CACHE_DISABLE')}
cases = [(128,1)] if mode == 'smoke' else [(128,16),(256,1),(256,16),(512,16),(1024,1),(1024,16),(1024,64)]
if mode == 'boundary':
    cases = [(2048,1),(2048,16),(2048,64)]
p = None
try:
    for n,batch in cases:
        name = f'fft-{n}-{batch}'
        print('start',name,flush=True)
        with (root/(name+'.txt')).open('w') as log:
            p = subprocess.Popen(['/root/math-audit/kstb-fft-test',str(n),str(batch),'5'],env=env,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
            guard.attach(p)
            try:
                p.wait(timeout=90)
            finally:
                if p.poll() is None:
                    os.killpg(p.pid,signal.SIGKILL)
                    p.wait(timeout=5)
            time.sleep(.3)
            guard.check()
            assert p.returncode == 0, (name,p.returncode)
        state['completed'].append(name)
        print((root/(name+'.txt')).read_text().splitlines()[-2:],flush=True)
    # Known graphics/fence controls use the native system library prefix too.
    for name,cmd in [('fences',['/root/gpu-system-prep/kstb-native-fence-test']),
                     ('graphics',['/root/gpu-system-prep/kstb-msaa-depth-stencil-test'])]:
        with (root/(name+'.txt')).open('w') as log:
            p = subprocess.Popen(cmd,env=env,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
            guard.attach(p)
            try:
                p.wait(timeout=30)
            finally:
                if p.poll() is None:
                    os.killpg(p.pid,signal.SIGKILL)
                    p.wait(timeout=5)
            time.sleep(.3)
            guard.check()
            assert p.returncode == 0, (name,p.returncode)
        state['completed'].append(name)
    state['state'] = 'complete'
finally:
    if p is not None and p.poll() is None:
        os.killpg(p.pid,signal.SIGKILL)
        p.wait(timeout=5)
    time.sleep(.3)
    state['fault'] = guard.fault
    if guard.fault is not None:
        state['state'] = 'fault'
    guard.close()
    stop.set()
    thread.join()
    watchdog.terminate()
    watchdog.wait(timeout=5)
    with open('/dev/watchdog','wb',buffering=0) as wd:
        wd.write(b'V')
    (root/'meminfo-after.txt').write_text(Path('/proc/meminfo').read_text())
    (root/'state.json').write_text(json.dumps(state,indent=2))
    print(root,state['state'],flush=True)
