import datetime,json,os,runpy,subprocess,threading,time
from pathlib import Path
O=Path('/root/gpu-runs')/(datetime.datetime.now().strftime('%Y%m%d-%H%M%S')+'-firefox-fix-regression');O.mkdir();(O/'run.py').write_text(Path(__file__).read_text());Path('/root/gpu-system-prep/regression-path').write_text(str(O))
for n in ['gles3','mustpass']:(O/n).symlink_to('/opt/deqp/'+n)
(O/'cases.txt').write_bytes(Path('/root/gpu-system-prep/regression-cases.txt').read_bytes())
stop=threading.Event();t=threading.Thread(target=runpy.run_path('/opt/gpu-test/kstb-gpu-run')['kmsg_reader'],args=(str(O/'kernel.txt'),stop));t.start();time.sleep(.2)
g=runpy.run_path('/opt/gpu-test/kstb_fault_guard.py')['KernelFaultGuard'](O/'kernel.txt').start();state='stopped'
env=dict(os.environ,LIBGL_DRIVERS_PATH='/var/tmp/kstb-firefox-fixed/dri',LD_LIBRARY_PATH='/usr/lib',DEQP=str(O),DEQP_EGL_PLATFORM='drm',DEQP_SURFACE_TYPE='window',MESA_SHADER_CACHE_DISABLE='true')
subprocess.run(['watchdog','-T','30','-t','5','/dev/watchdog'],check=True)
try:
 for name,cmd in [('deqp',['/opt/gpu-test/kstb-deqp','gles3','--cases',str(O/'cases.txt')]),('fences',['/root/gpu-system-prep/kstb-native-fence-test']),('msaa',['/root/gpu-system-prep/kstb-msaa-depth-stencil-test']),('gles1',['/root/gpu-system-prep/kstb-gles1-smoke'])]:
  with (O/(name+'.txt')).open('w') as log:
   p=subprocess.Popen(cmd,env=env,stdout=log,stderr=subprocess.STDOUT,start_new_session=True);g.attach(p)
   try:p.wait(timeout=180)
   finally:
    if p.poll() is None:os.killpg(p.pid,9);p.wait()
   g.check();assert p.returncode==0,(name,p.returncode);print(name,'passed',flush=True)
 state='complete'
finally:
 g.close();subprocess.run('killall watchdog; sleep 1; printf V > /dev/watchdog',shell=True,check=True);stop.set();t.join();(O/'state.json').write_text(json.dumps(dict(state=state,fault=g.fault),indent=2));print(O,state,flush=True)
