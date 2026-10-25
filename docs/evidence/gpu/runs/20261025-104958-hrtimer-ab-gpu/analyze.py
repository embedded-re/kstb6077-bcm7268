"""Validate and summarize the archived timer A/B/A experiment, without a board."""
from pathlib import Path
from collections import Counter
import json,re,statistics
ROOT=Path(__file__).resolve().parent
BASE=ROOT.parent/'20261025-101822-compute-latency-milestone'
BASE_NN=ROOT.parent/'20261025-102029-neural-queue-accounting'
GRAPHICS=ROOT.parent/'20261025-104138-kernel29-timer-ab-graphics'
ROLLBACK=ROOT.parent/'20261025-110017-hrtimer-rollback29'

def load(root,name):return json.loads((root/name).read_text())
for root,resets in ((ROOT,3),(ROLLBACK,0),(GRAPHICS,0),(BASE,0),(BASE_NN,0)):
 assert load(root,'state.json')['state']=='complete',root
 assert all(x['exit']==0 for x in load(root,'results.json'))
 kernel=(root/'kernel.txt').read_text()
 assert kernel.count('Resetting GPU')==resets,(root,resets)
 assert not re.search(r'MMU error|Oops|BUG:|Unable to handle|Internal error|kmsg overrun',kernel)
for key,baseline in (('/opt/gpu-test/kstb-neural-test',BASE_NN),('/opt/gpu-test/kstb-compute-latency-test',BASE),('/opt/mesa/23.3.6-5a3fd8dd/lib/dri/v3d_dri.so',BASE)):
 expected=load(baseline,'meta.json')['sha256'][key]
 assert load(ROOT,'meta.json')['sha256'][key]==expected
 assert load(ROLLBACK,'meta.json')['sha256'][key]==expected

latency_pattern=re.compile(r'round (\d+) (cold|warm) host_submit_ms ([\d.]+) host_finish_ms ([\d.]+) wall_ms ([\d.]+) render_ms ([\d.]+) clean_ms ([\d.]+) render_jobs (\d+) clean_jobs (\d+) bin_jobs (\d+)')
def latency(root):
 result={}
 for stage in load(root,'results.json'):
  if Path(stage['args'][0]).name!='kstb-compute-latency-test':continue
  text=(root/(stage['test']+'.txt')).read_text();assert 'PASS:' in text and 'FAIL' not in text
  groups,loops,rounds,dispatches=map(int,stage['args'][1:5]);jobs=((groups+15)//16)*dispatches
  rows=latency_pattern.findall(text);assert len(rows)==rounds
  assert all(tuple(map(int,row[7:10]))==(jobs,jobs,0) for row in rows)
  result[stage['test']]=dict(rounds=rounds,jobs_per_round=jobs,warm_median_ms={key:statistics.median(float(row[index]) for row in rows[1:]) for key,index in [('submit',2),('finish',3),('wall',4),('render',5),('cache_clean',6)]})
 return result

neural_pattern=re.compile(r'round (\d+) \((cold|warm)\): CPU ([\d.]+) ms; dispatch1 host ([\d.]+) ms; dispatch2 host ([\d.]+) ms; finish ([\d.]+) ms; GPU dispatch \+ synchronization ([\d.]+) ms; GPU class checksum (0x[0-9a-f]+); (\d+) classification checks; PASS')
def neural(root):
 result={}
 for stage in load(root,'results.json'):
  if Path(stage['args'][0]).name!='kstb-neural-test':continue
  text=(root/(stage['test']+'.txt')).read_text();assert 'FAIL' not in text
  rows=neural_pattern.findall(text)
  batch,local,rounds=map(int,stage['args'][1:]);assert len(rows)==rounds
  assert len(re.findall(r'max absolute error 0, 0 errors; guards checked',text))==2*rounds
  counters={}
  for n,key,value in re.findall(r'stats round (-?\d+) (\S+):\s+(\d+) (?:ns|jobs)',text):counters.setdefault(int(n),{})[key]=int(value)
  jobs=sum((((batch*out+local-1)//local)+15)//16 for out in (16,8))
  queue=[]
  for n in range(rounds):
   a,b=counters[n-1],counters[n]
   for name in ('render','cache_clean'):assert b['v3d-jobs-'+name]-a['v3d-jobs-'+name]==jobs
   for name in ('bin','csd'):assert b['v3d-jobs-'+name]==a['v3d-jobs-'+name]==0
   queue.append({name:(b['drm-engine-'+name]-a['drm-engine-'+name])/1e6 for name in ('render','cache_clean')})
  result[stage['test']]=dict(batch=batch,local_size=local,rounds=rounds,jobs_per_round=jobs,warm_median_ms={**{key:statistics.median(float(row[index]) for row in rows[1:]) for key,index in [('cpu',2),('dispatch1',3),('dispatch2',4),('finish',5),('wall',6)]},**{key:statistics.median(row[key] for row in queue[1:]) for key in ('render','cache_clean')}})
 return result

before_l,after_l,rollback_l=map(latency,(BASE,ROOT,ROLLBACK))
before_n,after_n,rollback_n=map(neural,(BASE_NN,ROOT,ROLLBACK))
cts=Counter()
for path in ROOT.glob('cts-*/results-*.txt'):
 rows=dict(line.split()[:2] for line in path.read_text().splitlines())
 assert set(rows.values())<={'Pass','NotSupported'}
 cts.update(rows.values())
assert cts=={'Pass':1224,'NotSupported':29},cts
for module in ('gles2','gles3'):
 name='results-'+module+'.txt'
 a=dict(line.split()[:2] for line in (ROOT/('cts-'+module)/name).read_text().splitlines())
 b=dict(line.split()[:2] for line in (GRAPHICS/module/name).read_text().splitlines())
 assert a==b and load(ROOT/('cts-'+module),'status-changes.json')=={}
# Each deliberate reset happened while the compute fence was pending.
kernel=(ROOT/'kernel.txt').read_text()
for i in range(3):
 text=(ROOT/f'reset-sync-50-{i}.txt').read_text()
 pending=float(re.search(r'compute fence pending at uptime ([\d.]+)',text).group(1))
 begin=kernel.index('kstb neural milestone reset-sync-50-'+str(i))
 next_stage=kernel.find('kstb neural milestone ',begin+1)
 segment=kernel[begin:next_stage if next_stage!=-1 else len(kernel)]
 reset=re.findall(r'\[\s*([\d.]+)\].*Resetting GPU',segment)
 assert len(reset)==1 and pending<float(reset[0])

def sleeps(root):return {str(x['request_us']):x['median_ms'] for x in load(root,'sleep-calibration.json')}
summary=dict(candidate_kernel=load(ROOT,'meta.json')['uname'],rollback_kernel=load(ROLLBACK,'meta.json')['uname'],
 conformance=dict(cts),neural_rounds=sum(x['rounds'] for x in after_n.values()),
 latency_rounds=sum(x['rounds'] for x in after_l.values()),latency_render_jobs=sum(x['rounds']*x['jobs_per_round'] for x in after_l.values()),
 fence_chains=350,deliberate_gpu_resets=3,unexpected_gpu_resets=0,mmu_errors=0,oopses=0,
 before_latency=before_l,candidate_latency=after_l,rollback_latency=rollback_l,
 before_neural=before_n,candidate_neural=after_n,rollback_neural=rollback_n,
 sleep_median_ms=dict(before=sleeps(BASE_NN),candidate=sleeps(ROOT),rollback=sleeps(ROLLBACK)))
(ROOT/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
for key in ('empty-1001','queued-64-jobs'):
 print(key,*[d[key]['warm_median_ms'] for d in (before_l,after_l,rollback_l)],sep='\n  ')
for key in ('neural-b257-l128','neural-b4096-l128'):
 print(key,*[d[key]['warm_median_ms'] for d in (before_n,after_n,rollback_n)],sep='\n  ')
print('CTS',dict(cts),'neural',summary['neural_rounds'],'timing rounds',summary['latency_rounds'],'timing jobs',summary['latency_render_jobs'])
print('sleep median ms',summary['sleep_median_ms'])
