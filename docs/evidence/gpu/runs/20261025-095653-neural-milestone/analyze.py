from pathlib import Path
import json,re,statistics
p=Path('docs/evidence/gpu/runs/20261025-095653-neural-milestone')
pattern=re.compile(r'round (\d+) \((cold|warm)\): CPU ([\d.]+) ms; dispatch1 host ([\d.]+) ms; dispatch2 host ([\d.]+) ms; finish ([\d.]+) ms; GPU dispatch \+ synchronization ([\d.]+) ms; GPU class checksum (0x[0-9a-f]+); (\d+) classification checks; PASS')
rows=[]
for path in sorted(p.glob('*.txt')):
 if path.name in ('kernel.txt','sync-200.txt','reset-sync-50.txt'):continue
 text=path.read_text(); found=pattern.findall(text)
 assert found,path
 assert 'FAIL' not in text
 assert len(re.findall(r'max absolute error 0, 0 errors; guards checked',text))==2*len(found)
 expected=next(x['args'][-1] for x in json.loads((p/'results.json').read_text()) if x['test']==path.stem)
 assert len(found)==int(expected),(path,len(found),expected)
 warm=found[1:]
 rows.append(dict(test=path.stem,rounds=len(found),batch=int(found[0][8]),cold_gpu_ms=float(found[0][6]),warm_cpu_median_ms=statistics.median(float(x[2]) for x in warm),warm_gpu_median_ms=statistics.median(float(x[6]) for x in warm),warm_gpu_min_ms=min(float(x[6]) for x in warm),warm_gpu_max_ms=max(float(x[6]) for x in warm),warm_dispatch1_host_median_ms=statistics.median(float(x[3]) for x in warm),warm_dispatch2_host_median_ms=statistics.median(float(x[4]) for x in warm),warm_finish_median_ms=statistics.median(float(x[5]) for x in warm)))
kernel=(p/'kernel.txt').read_text()
assert kernel.count('Resetting GPU')==1
assert not re.search('MMU error|Oops|BUG:|Unable to handle|Internal error|kmsg overrun',kernel)
assert json.loads((p/'state.json').read_text())['state']=='complete'
# Identical generated inputs must give identical classifications across workgroup sizes.
for batch in (1,65,257):
 checks=[]
 for local in (16,32,64,128):
  checks.append([x[7] for x in pattern.findall((p/f'neural-b{batch}-l{local}.txt').read_text())])
 assert all(x==checks[0] for x in checks)
summary=dict(neural_rounds=sum(x['rounds'] for x in rows),synchronization_chains=250,deliberate_gpu_resets=1,unexpected_gpu_resets=0,mmu_errors=0,oopses=0,all_layers_exact=True,guards_intact=True,measurements=rows)
(p/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
for row in rows:
 print(row['test'],row['rounds'],'CPU',row['warm_cpu_median_ms'],'GPU',row['warm_gpu_median_ms'],'finish',row['warm_finish_median_ms'])
print('TOTAL',summary['neural_rounds'])
