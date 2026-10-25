import json,os,subprocess,sys
from pathlib import Path
module,casefile,directory,*baseline=sys.argv[1:]
out=Path(directory);out.mkdir()
for name in ('gles2','gles3','gles31','egl','mustpass'):
 (out/name).symlink_to(Path('/opt/deqp')/name)
env=os.environ.copy();env.update(DEQP=str(out),DEQP_EGL_PLATFORM='drm',DEQP_SURFACE_TYPE='window')
if module!='gles31': env.pop('V3D_COMPUTE33',None)
cases=Path(casefile).read_text().splitlines()
assert cases and len(cases)==len(set(cases))
r=subprocess.run(['/opt/gpu-test/kstb-deqp',module,'--cases',casefile,'--chunk','50'],env=env)
assert r.returncode==0,r.returncode
path=out/('results-'+module+'.txt')
rows=dict(line.split()[:2] for line in path.read_text().splitlines())
assert set(rows)==set(cases),(len(rows),len(cases))
assert set(rows.values()) <= {'Pass','NotSupported','QualityWarning','CompatibilityWarning'},rows
if baseline:
 prior=dict(line.split()[:2] for line in Path(baseline[0]).read_text().splitlines())
 changes={case:[prior.get(case),status] for case,status in rows.items() if prior.get(case)!=status}
 (out/'status-changes.json').write_text(json.dumps(changes,indent=2)+'\n')
 assert not changes,changes
print(f'PASS: {len(rows)} {module} cases; '+('baseline identical' if baseline else 'baseline recorded'),flush=True)
