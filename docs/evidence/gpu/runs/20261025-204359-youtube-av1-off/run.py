import datetime,hashlib,json,os,runpy,shlex,subprocess,threading,time
from pathlib import Path
O=Path('/root/gpu-runs')/(datetime.datetime.now().strftime('%Y%m%d-%H%M%S')+'-youtube-av1-off');O.mkdir();(O/'run.py').write_text(Path(__file__).read_text());Path('/root/gpu-system-prep/normal-path').write_text(str(O))
assert not subprocess.run(['pidof','firefox'],capture_output=True).stdout.strip()
sway=subprocess.check_output(['pidof','sway'],text=True).split();assert len(sway)==1
R=Path('/tmp/xdg-1000');display=next(p.name for p in R.glob('wayland-*') if not p.name.endswith('.lock'))
swayenv=dict(x.split('=',1) for x in Path('/proc/'+sway[0]+'/environ').read_text().split('\0') if '=' in x)
env=['env','XDG_RUNTIME_DIR='+str(R),'WAYLAND_DISPLAY='+display]+['DBUS_SESSION_BUS_ADDRESS='+swayenv['DBUS_SESSION_BUS_ADDRESS']] if 'DBUS_SESSION_BUS_ADDRESS' in swayenv else ['env','XDG_RUNTIME_DIR='+str(R),'WAYLAND_DISPLAY='+display]
args=env+['firefox','https://www.youtube.com/watch?v=T1br3HCqrds&t=392s'];(O/'launch.json').write_text(json.dumps(args,indent=2))
stop=threading.Event();t=threading.Thread(target=runpy.run_path('/opt/gpu-test/kstb-gpu-run')['kmsg_reader'],args=(str(O/'kernel.txt'),stop));t.start();time.sleep(.2)
g=runpy.run_path('/opt/gpu-test/kstb_fault_guard.py')['KernelFaultGuard'](O/'kernel.txt').start();p=None;state='stopped'
subprocess.run(['watchdog','-T','30','-t','5','/dev/watchdog'],check=True)
try:
 with (O/'firefox.txt').open('w') as log:
  p=subprocess.Popen(['su','-l','kstb','-c',shlex.join(args)],stdout=log,stderr=subprocess.STDOUT,start_new_session=True);g.attach(p)
  for i in range(180):g.check();assert p.poll() is None;time.sleep(1)
  maps=[]
  for q in Path('/proc').glob('[0-9]*'):
   try:
    text=(q/'maps').read_text()
    if '/usr/lib/firefox/' in text and '/dri/' in text:(O/('maps-'+q.name+'.txt')).write_text(text);maps.append(q.name)
   except (FileNotFoundError,PermissionError,ProcessLookupError):pass
  assert maps,'no browser DRI mapping captured'
  (O/'driver.sha256').write_text(hashlib.sha256(Path('/usr/lib/dri/v3d_dri.so').read_bytes()).hexdigest()+'\n')
  state='complete';print('YouTube comparison browser left open; desktop PID',sway[0],flush=True)
finally:
 if state!='complete' and p and p.poll() is None:os.killpg(p.pid,9);p.wait()
 g.close();subprocess.run('killall watchdog; sleep 1; printf V > /dev/watchdog',shell=True,check=True);stop.set();t.join();(O/'state.json').write_text(json.dumps(dict(state=state,fault=g.fault,original_sway_pid=sway[0],browser_left_open=state=='complete'),indent=2));print(O,state,flush=True)
