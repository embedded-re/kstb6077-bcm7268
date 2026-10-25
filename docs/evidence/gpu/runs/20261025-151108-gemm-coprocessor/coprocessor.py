"""Sixteen independent GEMMs: CPU-only or twelve CPU plus four GPU jobs."""
from pathlib import Path
import json
import os
import subprocess
import sys
import time

out = Path(sys.argv[1]); out.mkdir()
kind = sys.argv[2]
assert kind in ('cpu-only', 'hybrid')
configs = [('cpu', 'neon4p', 16)] if kind == 'cpu-only' else [('cpu', 'neon4p', 12), ('gpu', 'tiled2', 4)]
children = []
try:
    for name, mode, jobs in configs:
        fifo = out / (name + '.fifo'); os.mkfifo(fifo)
        ready = out / (name + '.ready')
        env = os.environ.copy()
        env.update(KSTB_GEMM_GATE=str(fifo), KSTB_GEMM_READY=str(ready))
        if name == 'gpu': env['KSTB_GEMM_TRANSFER'] = '1'
        log = (out / (name + '.txt')).open('w')
        proc = subprocess.Popen(['/opt/gpu-test/kstb-gemm-test', mode, '1024', str(jobs + 1)], env=env, stdout=log, stderr=subprocess.STDOUT)
        children.append((name, proc, log, fifo, ready))
    setup = time.monotonic()
    while not all(x[4].exists() for x in children):
        assert all(x[1].poll() is None for x in children), 'child exited before ready'
        assert time.monotonic() - setup < 120, 'setup timeout'
        time.sleep(.01)
    start = time.monotonic()
    for _, _, _, fifo, _ in children:
        with fifo.open('wb', buffering=0) as gate: gate.write(b'G')
    for name, proc, _, _, _ in children:
        assert proc.wait(timeout=120) == 0, (name, proc.returncode)
    elapsed = time.monotonic() - start
    for name, _, log, _, _ in children:
        log.close()
        text = (out / (name + '.txt')).read_text()
        assert 'PASS: full matrix exact, guards intact' in text and 'FAIL' not in text
    result = dict(kind=kind, N=1024, timed_jobs=16, elapsed_seconds=elapsed,
                  aggregate_GFLOPS=16 * 2 * 1024**3 / elapsed / 1e9,
                  includes='all timed rounds, GPU transfers, result validation, process cleanup; warmup/reference excluded',
                  configs=configs)
    (out / 'summary.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result), flush=True)
    print('PASS: sixteen GEMMs exact, guards intact', flush=True)
finally:
    for _, proc, log, _, _ in children:
        if proc.poll() is None: proc.terminate()
        log.close()
