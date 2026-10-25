"""One-shot continuation of the existing B1 validation, on the board only."""
import collections
import datetime
import fcntl
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

BUILD = '23.3.6-3112d69b'
PREVIOUS = Path('/root/gpu-runs/20261024-224600-b1e71-gles31-chain')
OUT = Path('/root/gpu-runs') / (datetime.datetime.now().strftime('%Y%m%d-%H%M%S') + '-3112-neural')
FAULT = re.compile(r'MMU error|Resetting GPU|Oops|BUG:|Unable to handle|Internal error|kmsg overrun')
BAD = re.compile(r' (Fail|Crash|ResourceError|InternalError|Missing|Lost|Timeout|DeviceLost) ')
ALLOWED = {'Pass', 'NotSupported', 'QualityWarning', 'CompatibilityWarning'}
results = []


def save(state, **extra):
    value = dict(state=state, predecessor=str(PREVIOUS), build=BUILD, results=results, **extra)
    temp = OUT / 'state.tmp'
    temp.write_text(json.dumps(value, indent=2) + '\n')
    temp.replace(OUT / 'state.json')
    print(state, extra, flush=True)


def faults(path):
    return path.exists() and FAULT.search(path.read_text())


def no_other_workload():
    names = {'deqp-gles2', 'deqp-gles3', 'deqp-gles31', 'deqp-egl',
             'kstb-gpu-run', 'kstb-neural-test', 'kstb-gather-test', 'kstb-gather-shadow-test'}
    for path in Path('/proc').glob('[0-9]*/cmdline'):
        try:
            args = [Path(s.decode()).name for s in path.read_bytes().split(b'\0') if s]
        except (OSError, UnicodeError):
            continue
        assert not names.intersection(args), ('another workload', str(path), args)


def finish_process(proc):
    os.killpg(proc.pid, signal.SIGINT)
    try:
        proc.wait(timeout=15)
    except subprocess.TimeoutExpired:
        os.killpg(proc.pid, signal.SIGKILL)
        proc.wait()
        subprocess.run('killall watchdog; sleep 1; printf V > /dev/watchdog', shell=True, check=True)


def case_statuses(path):
    rows = [line.split()[:2] for line in path.read_text().splitlines()]
    assert rows and all(len(r) == 2 for r in rows), ('bad result file', str(path))
    assert len(rows) == len(dict(rows)), ('duplicate cases', str(path))
    return dict(rows)


def neural():
    no_other_workload()
    out = OUT / 'neural'
    out.mkdir()
    env = os.environ.copy()
    line = subprocess.check_output(['kstb-mesa', 'env', BUILD], text=True).strip()
    assert line.startswith('export ')
    env.update(dict(kv.split('=', 1) for kv in line[7:].split()))
    for k in ('MESA_GLES_VERSION_OVERRIDE', 'MESA_GLSL_VERSION_OVERRIDE', 'V3D_DEBUG'):
        env.pop(k, None)
    env.update(V3D_COMPUTE33='1', MESA_SHADER_CACHE_DISABLE='true')
    with (out / 'meta.txt').open('w') as f:
        subprocess.run(['uname', '-a'], stdout=f, check=True)
        f.write(json.dumps({k: v for k, v in env.items() if k.startswith(('MESA_', 'V3D_', 'LIBGL_', 'LD_LIBRARY_'))}) + '\n')
        for path in (Path('/opt/gpu-test/kstb-neural-test'), Path('/opt/mesa') / BUILD / 'lib/dri/v3d_dri.so'):
            f.write(hashlib.sha256(path.read_bytes()).hexdigest() + '  ' + str(path) + '\n')
    reader = runpy.run_path('/opt/gpu-test/kstb-gpu-run')['kmsg_reader']
    stop = threading.Event()
    thread = threading.Thread(target=reader, args=(str(out / 'kernel.txt'), stop))
    thread.start()
    time.sleep(.5)
    Path('/dev/kmsg').write_text('kstb neural validation start\n')
    subprocess.run('watchdog -T 30 -t 5 /dev/watchdog', shell=True, check=True)
    try:
        for batch in (1, 65, 257):
            with (out / f'batch-{batch}.txt').open('w') as f:
                proc = subprocess.Popen(['/opt/gpu-test/kstb-neural-test', str(batch)],
                                        env=env, stdout=f, stderr=subprocess.STDOUT, start_new_session=True)
                deadline = time.monotonic() + 60
                while proc.poll() is None:
                    time.sleep(.2)
                    if faults(out / 'kernel.txt') or time.monotonic() > deadline:
                        finish_process(proc)
                        raise RuntimeError('neural GPU fault or timeout')
            assert proc.returncode == 0, ('neural', batch, proc.returncode)
            assert 'PASS' in (out / f'batch-{batch}.txt').read_text()
            results.append(dict(test='neural', batch=batch, exit=0, run=str(out)))
            save('neural')
    finally:
        subprocess.run('killall watchdog; sleep 1; printf V > /dev/watchdog', shell=True, check=True)
        time.sleep(1)
        stop.set()
        thread.join()
    assert (out / 'kernel.txt').stat().st_size > 0
    assert not faults(out / 'kernel.txt'), 'neural kernel fault'


def graphics(module, count):
    no_other_workload()
    out = OUT / module
    env = os.environ.copy()
    for k in ('MESA_GLES_VERSION_OVERRIDE', 'MESA_GLSL_VERSION_OVERRIDE', 'V3D_DEBUG', 'V3D_COMPUTE33'):
        env.pop(k, None)
    env['MESA_SHADER_CACHE_DISABLE'] = 'true'
    args = ['/opt/gpu-test/kstb-gpu-run', '--mesa', BUILD, '--dir', str(out), module, '--chunk', '100']
    with (OUT / f'{module}.txt').open('w') as f:
        proc = subprocess.Popen(args, env=env, stdout=f, stderr=subprocess.STDOUT, start_new_session=True)
        deadline = time.monotonic() + 6 * 3600
        while proc.poll() is None:
            time.sleep(2)
            path = out / f'results-{module}.txt'
            if (path.exists() and BAD.search(path.read_text())) or faults(out / 'kernel.txt') or time.monotonic() > deadline:
                finish_process(proc)
                raise RuntimeError(f'{module} failed, faulted or timed out')
    assert proc.returncode == 0, (module, proc.returncode)
    assert re.search(r'^end ', (out / 'meta.txt').read_text(), re.M), 'incomplete run'
    rows = case_statuses(out / f'results-{module}.txt')
    assert len(rows) == count and set(rows.values()) <= ALLOWED, (module, collections.Counter(rows.values()))
    baseline = case_statuses(Path('/tmp/kstb-b1e71-baseline') / f'results-{module}.txt')
    assert rows.keys() == baseline.keys(), 'case list changed'
    changes = [dict(case=c, before=baseline[c], after=s) for c, s in rows.items() if s != baseline[c]]
    (out / 'baseline-differences.json').write_text(json.dumps(changes, indent=2) + '\n')
    assert not faults(out / 'kernel.txt'), 'kernel fault'
    results.append(dict(test=module, exit=0, run=str(out), counts=dict(collections.Counter(rows.values())),
                        baseline_status_changes=len(changes)))
    save(module)
    # Stop even on an apparently beneficial status change for manual review.
    assert not changes, ('review changed results', module, len(changes))


if __name__ == '__main__':
    OUT.mkdir()
    shutil.copyfile(__file__, OUT / 'run.py')
    save('neural')
    try:
        neural()
        save('complete')
    except Exception as error:
        save('stopped', error=repr(error))
        raise
