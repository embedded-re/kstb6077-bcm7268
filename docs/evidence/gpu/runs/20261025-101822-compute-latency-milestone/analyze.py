"""Recompute queue accounting from the archived raw logs; no board access."""
from pathlib import Path
import json
import re
import statistics

ROOT = Path(__file__).resolve().parent
NEURAL = ROOT.parent / '20261025-102029-neural-queue-accounting'
FAULT = re.compile(r'MMU error|Resetting GPU|Oops|BUG:|Unable to handle|Internal error|kmsg overrun')
for root in (ROOT, NEURAL):
    assert json.loads((root / 'state.json').read_text())['state'] == 'complete'
    assert not FAULT.search((root / 'kernel.txt').read_text())
    assert all(x['exit'] == 0 for x in json.loads((root / 'results.json').read_text()))

def median(rows, key):
    return statistics.median(x[key] for x in rows)

pattern = re.compile(r'round (\d+) (cold|warm) host_submit_ms ([\d.]+) host_finish_ms ([\d.]+) wall_ms ([\d.]+) render_ms ([\d.]+) clean_ms ([\d.]+) render_jobs (\d+) clean_jobs (\d+) bin_jobs (\d+)')
latency = []
for stage in json.loads((ROOT / 'results.json').read_text()):
    if Path(stage['args'][0]).name != 'kstb-compute-latency-test':
        continue
    text = (ROOT / (stage['test'] + '.txt')).read_text()
    assert 'PASS:' in text and 'FAIL' not in text
    args = stage['args'][1:]
    groups, loops, rounds, dispatches = map(int, args[:4])
    found = pattern.findall(text)
    assert len(found) == rounds
    expected_jobs = ((groups + 15) // 16) * dispatches
    rows = []
    for row in found:
        assert tuple(map(int, row[7:10])) == (expected_jobs, expected_jobs, 0)
        rows.append(dict(zip(('round', 'submit_ms', 'finish_ms', 'wall_ms', 'render_ms', 'clean_ms'),
                             [int(row[0]), *map(float, row[2:7])])))
    warm = rows[1:]
    latency.append(dict(test=stage['test'], groups=groups, loops=loops, rounds=rounds,
                        dispatches=dispatches, render_jobs_per_round=expected_jobs,
                        warm_median={key: median(warm, key) for key in rows[0] if key != 'round'}))

neural = []
round_pattern = re.compile(r'round (\d+) \((cold|warm)\): CPU ([\d.]+) ms; dispatch1 host ([\d.]+) ms; dispatch2 host ([\d.]+) ms; finish ([\d.]+) ms; GPU dispatch \+ synchronization ([\d.]+) ms; GPU class checksum (0x[0-9a-f]+); (\d+) classification checks; PASS')
for stage in json.loads((NEURAL / 'results.json').read_text()):
    text = (NEURAL / (stage['test'] + '.txt')).read_text()
    batch, local, rounds = map(int, stage['args'][1:])
    found = round_pattern.findall(text)
    assert len(found) == rounds
    assert len(re.findall('max absolute error 0, 0 errors; guards checked', text)) == 2 * rounds
    counters = {}
    for line in text.splitlines():
        m = re.fullmatch(r'stats round (-?\d+) (\S+):\s+(\d+) (?:ns|jobs)', line)
        if m:
            counters.setdefault(int(m[1]), {})[m[2]] = int(m[3])
    expected_jobs = sum((((batch * out + local - 1) // local) + 15) // 16 for out in (16, 8))
    rows = []
    for round in range(rounds):
        before, after = counters[round - 1], counters[round]
        assert after['v3d-jobs-render'] - before['v3d-jobs-render'] == expected_jobs
        assert after['v3d-jobs-cache_clean'] - before['v3d-jobs-cache_clean'] == expected_jobs
        assert after['v3d-jobs-bin'] == before['v3d-jobs-bin'] == 0
        assert after['v3d-jobs-csd'] == before['v3d-jobs-csd'] == 0
        rows.append(dict(wall_ms=float(found[round][6]), cpu_ms=float(found[round][2]),
                         render_ms=(after['drm-engine-render'] - before['drm-engine-render']) / 1e6,
                         clean_ms=(after['drm-engine-cache_clean'] - before['drm-engine-cache_clean']) / 1e6))
    neural.append(dict(test=stage['test'], batch=batch, local_size=local, rounds=rounds,
                       render_jobs_per_round=expected_jobs,
                       warm_median={key: median(rows[1:], key) for key in rows[0]}))

# Count IRQs independently; only the render-only compute path was active.
def irqs(file):
    rows = {}
    for line in file.read_text().splitlines():
        fields = line.split()
        if fields and fields[-1] in ('v3d_core0', 'v3d_hub'):
            rows[fields[-1]] = list(map(int, fields[1:5]))
    return rows
before, after = irqs(ROOT / 'interrupts-before.txt'), irqs(ROOT / 'interrupts-after.txt')
deltas = {key: [a - b for a, b in zip(after[key], before[key])] for key in before}
latency_jobs = sum(x['rounds'] * x['render_jobs_per_round'] for x in latency)
# Main runner ends with 200 producer/consumer chains (17 jobs/dispatch),
# then 11 neural rounds for batches 1,65,257 at local size 128 (2/2/5 jobs).
expected_irqs = latency_jobs + 200 * 2 * 17 + 11 * (2 + 2 + 5)
assert sum(deltas['v3d_core0']) == expected_irqs, (deltas, expected_irqs)
assert not any(deltas['v3d_hub'])
assert (ROOT / 'power-before.txt').read_text() == (ROOT / 'power-after.txt').read_text()
# Linear fit of kernel render-queue elapsed time to dependent uint LCG loops.
sweep = [x for x in latency if x['test'].startswith('work-loops-')]
x = [r['loops'] for r in sweep]
y = [r['warm_median']['render_ms'] for r in sweep]
xmean, ymean = statistics.mean(x), statistics.mean(y)
slope = sum((a-xmean)*(b-ymean) for a,b in zip(x,y)) / sum((a-xmean)**2 for a in x)
intercept = ymean - slope * xmean
summary = dict(latency_rounds=sum(x['rounds'] for x in latency), latency_render_jobs=latency_jobs,
               synchronization_chains=200, neural_rounds_without_stats=33,
               neural_rounds_with_stats=sum(x['rounds'] for x in neural),
               unexpected_gpu_resets=0, mmu_errors=0, oopses=0,
               irq_deltas=deltas, irq_expected=expected_irqs,
               work_sweep_render_fit=dict(intercept_ms=intercept, ms_per_loop=slope,
                                          invocations_per_loop=2048), latency=latency, neural=neural)
(ROOT / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
for group in (latency, neural):
    for row in group:
        print(row['test'], 'jobs', row['render_jobs_per_round'], row['warm_median'])
print('TOTAL', {k: v for k, v in summary.items() if k not in ('latency', 'neural')})
