import datetime,hashlib,json,os,re,runpy,shlex,signal,subprocess,sys,threading,time
from pathlib import Path
name=sys.argv[1]
cases={
 'files':['thunar','/home/kstb/POC'],
 'editor':['mousepad','--disable-server','/home/kstb/POC/Welcome.txt'],
 'video':['mpv','--no-config','--length=12','av://lavfi:testsrc2=size=640x360:rate=25'],
 'supertux':['supertux2','--window','--geometry','960x540','--show-fps','/usr/share/games/supertux2/levels/world1/welcome_antarctica.stl'],
 'breakout':['lbreakouthd'],
 'transport':['openttd','-r','1024x640']}
assert name in cases
O=Path('/root/gpu-runs')/(datetime.datetime.now().strftime('%Y%m%d-%H%M%S')+'-poc-'+name);O.mkdir();(O/'run.py').write_text(Path(__file__).read_text());Path('/root/gpu-system-prep/poc-path').write_text(str(O))
R=Path('/tmp/xdg-1000');sway=subprocess.check_output(['pidof','sway'],text=True).split();assert sway==['5251'];sock=next(R.glob('sway-ipc.*.sock'));display=next(p.name for p in R.glob('wayland-*') if not p.name.endswith('.lock'))
se=dict(x.split('=',1) for x in Path('/proc/5251/environ').read_text().split('\0') if '=' in x)
env=['env','XDG_RUNTIME_DIR='+str(R),'WAYLAND_DISPLAY='+display]+[k+'='+se[k] for k in ['DBUS_SESSION_BUS_ADDRESS','DISPLAY'] if k in se]
def user(args):return subprocess.run(['su','-l','kstb','-c',shlex.join(env+args)],capture_output=True,text=True,timeout=15)
def ipc(args):
 p=user(['swaymsg','-s',str(sock)]+args);assert p.returncode==0,p.stderr;return json.loads(p.stdout)
def group():
 out=[]
 for q in Path('/proc').glob('[0-9]*'):
  try:
   s=(q/'stat').read_text();v=s[s.rindex(')')+2:].split()
   if int(v[2])==proc.pid:out.append(q.name)
  except (FileNotFoundError,PermissionError,ProcessLookupError):pass
 return out
def windows(tree,pids):
 out=[]
 if str(tree.get('pid')) in pids and tree.get('type') in ('con','floating_con') and tree.get('name'):out.append({k:tree.get(k) for k in ['id','pid','name','app_id','rect','shell']})
 for k in ['nodes','floating_nodes']:
  for n in tree.get(k,[]):out+=windows(n,pids)
 return out
def engines():
 out={}
 for pid in group():
  for f in Path('/proc',pid,'fdinfo').glob('*'):
   try:
    text=f.read_text();m=re.search(r'drm-client-id:\s*(\d+)',text)
    if m:out[m[1]]={k:int(v) for k,v in re.findall(r'(drm-engine-[\w_]+|v3d-jobs-[\w_]+):\s*(\d+)',text)}
   except (FileNotFoundError,PermissionError,ProcessLookupError):pass
 return out
stop=threading.Event();thread=threading.Thread(target=runpy.run_path('/opt/gpu-test/kstb-gpu-run')['kmsg_reader'],args=(str(O/'kernel.txt'),stop));thread.start();time.sleep(.2)
guard=runpy.run_path('/opt/gpu-test/kstb_fault_guard.py')['KernelFaultGuard'](O/'kernel.txt').start();proc=None;state='stopped';before=None
subprocess.run(['watchdog','-T','30','-t','5','/dev/watchdog'],check=True)
try:
 ipc(['workspace','POC tests'])
 with (O/'app.txt').open('w') as log:
  args=env+cases[name];(O/'launch.json').write_text(json.dumps(args,indent=2))
  proc=subprocess.Popen(['su','-l','kstb','-c',shlex.join(args)],stdout=log,stderr=subprocess.STDOUT,start_new_session=True);guard.attach(proc)
  start=time.monotonic();seen=[]
  while time.monotonic()-start<35:
   guard.check();assert proc.poll() is None,('app exited before window',proc.returncode)
   seen=windows(ipc(['-t','get_tree','-r']),group())
   if seen:break
   time.sleep(.3)
  assert seen,'no application window';(O/'windows.json').write_text(json.dumps(seen,indent=2));print(name,'window',seen,flush=True)
  before=engines();time.sleep(2);guard.check()
  image=R/('poc-'+name+'.png');p=user(['grim',str(image)]);assert p.returncode==0,p.stderr;(O/'screen.png').write_bytes(image.read_bytes())
  for i in range(12):
   guard.check()
   if proc.poll() is not None:
    assert name=='video' and proc.returncode==0,('app exited',proc.returncode)
    break
   time.sleep(1)
  after=engines();(O/'engines.json').write_text(json.dumps(dict(before=before,after=after),indent=2))
  for pid in group():
   try:
    text=Path('/proc',pid,'maps').read_text();lines=[l.rstrip() for l in text.splitlines() if any(t in l for t in ['/dri/','/libGL','/libEGL','/libgbm'])]
    if lines:(O/('maps-'+pid+'.txt')).write_text('\n'.join(lines)+'\n')
   except (FileNotFoundError,PermissionError,ProcessLookupError):pass
  guard.check();state='complete'
except Exception as e:
 (O/'error.txt').write_text(repr(e)+'\n');raise
finally:
 if proc and proc.poll() is None:
  os.killpg(proc.pid,signal.SIGTERM)
  try:proc.wait(timeout=5)
  except subprocess.TimeoutExpired:os.killpg(proc.pid,signal.SIGKILL);proc.wait()
 guard.close();subprocess.run('killall watchdog; sleep 1; printf V > /dev/watchdog',shell=True,check=True);stop.set();thread.join();(O/'state.json').write_text(json.dumps(dict(state=state,app=name,fault=guard.fault,original_sway_pid=sway[0]),indent=2));print(O,state,flush=True)
