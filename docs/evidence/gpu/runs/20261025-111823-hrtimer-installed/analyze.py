"""Validate the installed-kernel gate and regenerate its timing summary."""
from pathlib import Path
from collections import Counter
import json,re,statistics
root=Path(__file__).resolve().parent
state=json.loads((root/'state.json').read_text());assert state=={'state':'complete','tests':10,'expected_resets':0}
assert all(x['exit']==0 for x in json.loads((root/'results.json').read_text()))
kernel=(root/'kernel.txt').read_text()
assert not re.search('MMU error|Resetting GPU|Oops|BUG:|Unable to handle|Internal error|kmsg overrun',kernel)
assert 'CONFIG_HIGH_RES_TIMERS=y' in (root/'kernel-config-selected.txt').read_text()
assert '#30 ' in json.loads((root/'meta.json').read_text())['uname']
rows=[]
pattern=re.compile(r'round (\d+) \((cold|warm)\): CPU ([\d.]+) ms;.*?GPU dispatch \+ synchronization ([\d.]+) ms;.*? (\d+) classification checks; PASS')
for batch in (1,65,257,1024,4096):
 text=(root/f'neural-b{batch}-l128.txt').read_text();matches=pattern.findall(text)
 assert len(matches)==11 and all(int(row[0])==i and int(row[4])==batch for i,row in enumerate(matches))
 assert len(re.findall('max absolute error 0, 0 errors; guards checked',text))==22 and 'FAIL' not in text
 rows.append(dict(batch=batch,local_size=128,rounds=11,warm_cpu_median_ms=statistics.median(float(row[2]) for row in matches[1:]),warm_gpu_median_ms=statistics.median(float(row[3]) for row in matches[1:]),warm_gpu_min_ms=min(float(row[3]) for row in matches[1:]),warm_gpu_max_ms=max(float(row[3]) for row in matches[1:])))
cts={}
for path in sorted(root.glob('cts-*/results-*.txt')):
 counts=Counter(line.split()[1] for line in path.read_text().splitlines());assert set(counts)<={'Pass','NotSupported'}
 cts[path.parent.name]=dict(counts)
assert sum(sum(x.values()) for x in cts.values())==290
assert '100/100 chains exact' in (root/'sync-100.txt').read_text()
summary=dict(kernel=json.loads((root/'meta.json').read_text())['uname'],neural_inferences=55,fence_chains=100,timing_rounds=101,conformance=cts,gpu_resets=0,mmu_errors=0,oopses=0,neural=rows)
(root/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
print(json.dumps(summary,indent=2))
