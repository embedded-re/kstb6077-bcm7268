import datetime,hashlib,json,shutil,subprocess
from pathlib import Path
O=Path('/root/gpu-runs')/(datetime.datetime.now().strftime('%Y%m%d-%H%M%S')+'-mesa-r34-install');O.mkdir();(O/'run.py').write_text(Path(__file__).read_text());Path('/root/gpu-system-prep/install-path').write_text(str(O))
assert not subprocess.run(['pidof','firefox'],capture_output=True).stdout.strip()
cache=Path('/var/cache/kstb/packages/mesa-23.3.6-r33');assert len(list(cache.glob('*.apk')))==6
shutil.copy2('/etc/apk/world',O/'world.before');(O/'driver.before.sha256').write_text(hashlib.sha256(Path('/usr/lib/dri/v3d_dri.so').read_bytes()).hexdigest()+'\n')
packages=sorted(Path('/root/gpu-system-prep/r34-packages').glob('*.apk'));assert len(packages)==6
pins=[n+'=23.3.6-r34' for n in ['mesa','mesa-dev','mesa-egl','mesa-gbm','mesa-gl','mesa-gles']]
state='stopped'
try:
 with (O/'verify.txt').open('w') as log:subprocess.run(['apk','verify']+list(map(str,packages)),stdout=log,stderr=subprocess.STDOUT,check=True)
 with (O/'install.txt').open('w') as log:subprocess.run(['apk','add']+list(map(str,packages))+pins,stdout=log,stderr=subprocess.STDOUT,check=True)
 shutil.copy2('/etc/apk/world',O/'world.after')
 got=hashlib.sha256(Path('/usr/lib/dri/v3d_dri.so').read_bytes()).hexdigest();expected=hashlib.sha256(Path('/root/gpu-system-prep/r34-stage/usr/lib/dri/v3d_dri.so').read_bytes()).hexdigest();assert got==expected
 (O/'driver.sha256').write_text(got+'\n');shutil.copy2('/root/gpu-system-prep/BUILD-ID',O/'BUILD-ID');shutil.copy2('/root/gpu-system-prep/r34-packages/metadata.json',O/'metadata.json')
 dest=Path('/var/cache/kstb/packages/mesa-23.3.6-r34');dest.mkdir(exist_ok=True)
 for p in packages:shutil.copy2(p,dest/p.name)
 (O/'packages.sha256').write_text(''.join(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+p.name+'\n' for p in packages))
 state='complete'
finally:(O/'state.json').write_text(json.dumps(dict(state=state),indent=2));print(O,state,flush=True)
