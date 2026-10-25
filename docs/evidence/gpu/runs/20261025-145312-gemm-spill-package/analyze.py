"""Verify archived gates and summarize measured rounds, not planned tests."""
from pathlib import Path
import collections
import json
import re

root = Path(__file__).resolve().parent
scratch = root.parent / '20261025-145135-gemm-spill-fixed'
summary = {}
for directory in (scratch, root):
    assert json.loads((directory / 'state.json').read_text())['state'] == 'complete'
    kernel = (directory / 'kernel.txt').read_text()
    assert not re.search(r'Resetting GPU|MMU error|Oops|BUG:|Unable to handle|Internal error|kmsg overrun', kernel)
    gemm = neural = 0
    for path in directory.glob('*.txt'):
        text = path.read_text()
        rounds = re.findall(r'^round \d+ (?:cold|warm) mode .*$', text, re.M)
        if rounds:
            assert all('errors 0 ' in line and line.endswith(' PASS') for line in rounds)
            assert 'PASS: full matrix exact, guards intact' in text
            gemm += len(rounds)
        neural_rounds = re.findall(r'^round \d+ \((?:cold|warm)\):.*$', text, re.M)
        assert all(line.endswith('PASS') for line in neural_rounds)
        neural += len(neural_rounds)
    assert '100/100 chains exact' in (directory / 'sync-100.txt').read_text()
    summary[directory.name] = dict(gemm_rounds=gemm, neural_rounds=neural, fence_chains=100, resets=0)
assert summary[scratch.name]['gemm_rounds'] == 60
assert summary[root.name]['gemm_rounds'] == 55
assert summary[scratch.name]['neural_rounds'] == 11
assert summary[root.name]['neural_rounds'] == 22
cts = {}
for label, module, count in [('compute', 'gles31', 195), ('graphics-spill', 'gles3', 200)]:
    rows = (root / ('cts-' + label) / ('results-' + module + '.txt')).read_text().splitlines()
    counts = dict(collections.Counter(line.split()[1] for line in rows))
    assert len(rows) == count and counts == {'Pass': count}
    cts[label] = counts
summary['cts'] = cts
summary['totals'] = dict(gemm_rounds=115, neural_rounds=33, fence_chains=200, cts_pass=395, resets=0)
(root / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
print(json.dumps(summary, indent=2))
