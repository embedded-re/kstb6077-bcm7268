"""Install native Mesa, run graphics smoke tests, exercise rollback, reinstall."""
import datetime,hashlib,json,os,re,runpy,shutil,subprocess,threading,time
from pathlib import Path
O=Path('/root/gpu-runs')/(datetime.datetime.now().strftime('%Y%m%d-%H%M%S')+'-system-mesa-migration');O.mkdir()
(O/'run.py').write_text(Path(__file__).read_text());Path('/root/gpu-system-prep/migration-path').write_text(str(O))
B=Path('/root/gpu-backups/system-mesa');P=Path('/root/gpu-system-prep')
assert not subprocess.run(['pidof','sway'],capture_output=True).stdout.strip()
stop=threading.Event();thread=threading.Thread(target=runpy.run_path('/opt/gpu-test/kstb-gpu-run')['kmsg_reader'],args=(str(O/'kernel.txt'),stop));thread.start();time.sleep(.2)
def check():assert not re.search(r'Resetting GPU|MMU error|Oops|BUG:|Unable to handle|Internal error|kmsg overrun',(O/'kernel.txt').read_text())
def run(label,args):
 with (O/(label+'.txt')).open('w') as log:
  p=subprocess.Popen(args,stdout=log,stderr=subprocess.STDOUT);start=time.monotonic()
  while p.poll() is None:
   check()
   if time.monotonic()-start>120:p.terminate();raise RuntimeError('timeout '+label)
   time.sleep(.1)
 check();assert p.returncode==0,(label,p.returncode);print(label,'PASS',flush=True)
def hashes():
 return {str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in [Path('/usr/local/lib/libEGL.so.1.0.0'),Path('/usr/local/lib/libgbm.so.1.0.0'),Path('/usr/local/lib/dri/v3d_dri.so')]}
original=hashes();(O/'original-local-hashes.json').write_text(json.dumps(original,indent=2))
assets=[]
for p in Path('/usr/local/lib').iterdir():
 if p.name in ('dri','gbm') or re.match(r'lib(EGL|GL|GLES|gbm|glapi).*\.so',p.name):assets.append(p)
for p in Path('/usr/local/lib/pkgconfig').glob('*.pc'):
 if p.stem in ('egl','gl','glapi','glesv1_cm','glesv2','gbm','dri'):assets.append(p)
for name in ('EGL','GL','GLES','GLES2','GLES3','KHR','gbm.h','gbm_backend_abi.h'):
 p=Path('/usr/local/include')/name
 if p.exists():assets.append(p)
(O/'local-assets.json').write_text(json.dumps([str(p) for p in assets],indent=2))
def park():
 for p in assets:
  dest=B/'local-files'/p.relative_to('/');dest.parent.mkdir(parents=True,exist_ok=True)
  assert p.exists() or p.is_symlink(),p
  assert not dest.exists() and not dest.is_symlink(),dest
  p.rename(dest)
def restore():
 for p in assets:
  src=B/'local-files'/p.relative_to('/');assert not p.exists() and not p.is_symlink(),p
  src.rename(p)
def native(label):
 run(label,['apk','add']+[str(p) for p in sorted((P/'signed-final').glob('*.apk'))]+[n+'=23.3.6-r33' for n in ('mesa','mesa-dev','mesa-egl','mesa-gbm','mesa-gl','mesa-gles')]+['!mesa-rusticl'])
 park()
state='stopped';subprocess.run(['watchdog','-T','30','-t','5','/dev/watchdog'],check=True)
try:
 native('install-native')
 for name,args in [('caps2',['2']),('caps3',['3']),('gles1',[]),('msaa',[]),('fence',[])]:
  exe={'caps2':'kstb-gl-caps','caps3':'kstb-gl-caps','gles1':'kstb-gles1-smoke','msaa':'kstb-msaa-depth-stencil-test','fence':'kstb-native-fence-test'}[name]
  run(name,[str(P/exe)]+args)
 shutil.copy2(B/'world.before','/etc/apk/world')
 old=[]
 for name in ('mesa','mesa-dev','mesa-egl','mesa-gbm','mesa-gl','mesa-gles','mesa-rusticl'):
  f=B/'old-apks'/(name+'-26.1.6-r0.apk');assert f.exists(),f;old.append(str(f))
 old += [str(p) for prefix in ('libclc-','spirv-llvm-translator-libs-') for p in (B/'old-apks').glob(prefix+'*.apk')]
 run('rollback-packages',['apk','add']+old)
 restore();shutil.copy2(B/'world.before','/etc/apk/world');assert hashes()==original
 run('rollback-caps',[str(P/'kstb-gl-caps'),'2'])
 run('rollback-consistency',['apk','fix','--simulate'])
 (O/'rollback-verified.json').write_text(json.dumps(dict(local_hashes_match=True,world_matches=True),indent=2))
 native('reinstall-native')
 run('final-caps3',[str(P/'kstb-gl-caps'),'3'])
 (O/'world.final').write_bytes(Path('/etc/apk/world').read_bytes())
 run('ownership',['apk','info','-W','/usr/lib/libEGL.so.1.0.0','/usr/lib/dri/v3d_dri.so'])
 state='complete'
finally:
 subprocess.run('killall watchdog; sleep 1; printf V > /dev/watchdog',shell=True,check=True)
 stop.set();thread.join();(O/'state.json').write_text(json.dumps(dict(state=state),indent=2));print(O,state,flush=True)
