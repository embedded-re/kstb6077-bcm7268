import json,os,time
from pathlib import Path
hz=os.sysconf('SC_CLK_TCK');O=Path(Path('/root/gpu-system-prep/normal-path').read_text())
def snap():
 out={}
 for p in Path('/proc').glob('[0-9]*'):
  try:
   s=(p/'stat').read_text();v=s[s.rindex(')')+2:].split();out[p.name]=(s[s.index('(')+1:s.rindex(')')],int(v[11])+int(v[12]))
  except (FileNotFoundError,ProcessLookupError):pass
 return out
samples=[]
for i in range(3):
 a=snap();t=time.monotonic();time.sleep(5);b=snap();dt=time.monotonic()-t
 rows=sorted([dict(pid=k,name=v[0],cpu_percent=round((v[1]-a[k][1])/hz/dt*100,1)) for k,v in b.items() if k in a],key=lambda x:x['cpu_percent'],reverse=True)[:10]
 samples.append(dict(start=t,seconds=dt,rows=rows));print(i,json.dumps(rows[:5]),flush=True)
(O/'cpu-steady.json').write_text(json.dumps(samples,indent=2));(O/'cpu-run.py').write_text(Path(__file__).read_text())
