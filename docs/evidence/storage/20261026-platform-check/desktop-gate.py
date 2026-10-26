"""Normal /usr desktop session: persistent windows, native app, launcher, restart."""
import datetime,json,os,re,runpy,shlex,signal,subprocess,threading,time
from pathlib import Path
O=Path('/root/gpu-runs')/(datetime.datetime.now().strftime('%Y%m%d-%H%M%S')+'-post-storage-desktop-gate');O.mkdir()
(O/'run.py').write_text(Path(__file__).read_text());Path('/root/gpu-system-prep/desktop-path').write_text(str(O))
assert not subprocess.run(['pidof','sway'],capture_output=True).stdout.strip()
R=Path('/tmp/xdg-1000');stop=threading.Event();thread=threading.Thread(target=runpy.run_path('/opt/gpu-test/kstb-gpu-run')['kmsg_reader'],args=(str(O/'kernel.txt'),stop));thread.start();time.sleep(.2)
def check():assert not re.search(r'Resetting GPU|MMU error|Oops|BUG:|Unable to handle|Internal error|kmsg overrun',(O/'kernel.txt').read_text())
def run(args):return subprocess.run(['su','-l','kstb','-c',shlex.join(['env','XDG_RUNTIME_DIR='+str(R)]+args)],text=True,capture_output=True,timeout=10)
def ipc(command,raw=False):
 result=run(['swaymsg','-s',str(sock)]+(['-t',command,'-r'] if raw else [command]));assert result.returncode==0,result.stderr;return result.stdout

def pause(seconds):
 end=time.monotonic()+seconds
 while time.monotonic()<end:check();assert proc.poll() is None;time.sleep(.1)
def capture(label):
 display=next(p.name for p in R.glob('wayland-*') if not p.name.endswith('.lock'))
 r=run(['env','WAYLAND_DISPLAY='+display,'grim',str(R/(label+'.png'))]);assert r.returncode==0,r.stderr
 (O/(label+'.png')).write_bytes((R/(label+'.png')).read_bytes())
def treesave(label):
 tree=ipc('get_tree',True);(O/(label+'-tree.json')).write_text(tree);return tree
def cleanup():
 dma=Path('/sys/kernel/debug/dma_buf/bufinfo').read_text();assert 'Total 0 objects, 0 bytes' in dma,dma
 assert Path('/sys/class/tty/tty0/active').read_text().strip()=='tty1'
 (O/'dmabuf-after.txt').write_text(dma)
subprocess.run(['watchdog','-T','30','-t','5','/dev/watchdog'],check=True)
proc=None;state='stopped';sessions=0
try:
 for label in ('normal','restart'):
  with (O/(label+'-sway.txt')).open('w') as log:
   # su -c is noninteractive; source the profile an interactive login reads.
   proc=subprocess.Popen(['su','-l','kstb','-c','. /etc/profile; exec dbus-run-session sway -d'],stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
   start=time.monotonic();sock=None
   while time.monotonic()-start<12:
    check();assert proc.poll() is None,'Sway exited'
    sockets=list(R.glob('sway-ipc.*.sock'))
    if sockets and 'allocated by = sway' in Path('/sys/kernel/debug/dri/1/state').read_text():sock=sockets[0];break
    time.sleep(.1)
   assert sock,'startup timeout';pause(1)
   if label=='normal':
    assert 'foot' in treesave('initial');capture('initial-desktop')
    ipc('exec /usr/bin/kstb-wayland-gles-test 12 > /tmp/xdg-1000/native-desktop-client.txt 2>&1')
    pause(2);ipc('[app_id="kstb-wayland-gles-test"] floating enable, resize set 800 600')
    pause(2);ipc('[app_id="kstb-wayland-gles-test"] fullscreen enable');pause(2)
    capture('fullscreen');(O/'drm-state.txt').write_text(Path('/sys/kernel/debug/dri/1/state').read_text())
    found=0
    for p in Path('/proc').glob('[0-9]*'):
     try:
      if (p/'comm').read_text().strip()=='sway' or os.readlink(p/'exe')=='/usr/bin/kstb-wayland-gles-test':
       maps=(p/'maps').read_text();(O/('maps-'+p.name+'.txt')).write_text(maps)
       assert '/usr/lib/dri/' in maps and '/opt/mesa/' not in maps and '/usr/local/lib/' not in maps
       env=(p/'environ').read_bytes().split(b'\0')
       assert not any(v.startswith((b'LD_LIBRARY_PATH=',b'LIBGL_DRIVERS_PATH=',b'GBM_BACKENDS_PATH=')) for v in env)
       (O/('dri-environment-'+p.name+'.json')).write_text(json.dumps([v.decode() for v in env if v.startswith((b'WLR_',b'XDG_RUNTIME_DIR='))],indent=2))
       found+=1
     except FileNotFoundError:pass
    assert found==2,found
    ipc('[app_id="kstb-wayland-gles-test"] fullscreen disable');pause(7)
    text=(R/'native-desktop-client.txt').read_text();(O/'client.txt').write_text(text)
    assert 'PASS: frames ' in text and 'GL renderer: V3D 3.3' in text and 'FAIL' not in text
    assert float(re.search(r'PASS:.* fps ([\d.]+)',text).group(1))>55,text[-200:]
    tree=treesave('after-app');assert 'kstb-wayland-gles-test' not in tree
    ipc('[app_id="foot"] kill');pause(1)
    tree=treesave('empty');assert '"app_id": "foot"' not in tree;capture('empty-desktop')
    ipc('exec foot');pause(1);assert 'foot' in treesave('new-terminal')
    ipc('exec wofi --show drun');pause(1);capture('launcher')
    wm=subprocess.run(['pidof','wofi'],capture_output=True,text=True);assert wm.returncode==0,wm.stdout
    for pid in wm.stdout.split():os.kill(int(pid),signal.SIGTERM)
    pause(.5)
    (O/'persistent.txt').write_text('PASS: app exit and closing all terminals leave the desktop alive; new terminal and launcher open.\n')
   else:assert 'foot' in treesave('restart')
   run(['swaymsg','-s',str(sock),'exit']);proc.wait(timeout=8);assert proc.returncode==0,proc.returncode
  check();cleanup();sessions+=1;print(label,'PASS',flush=True)
 with (O/'cpu-fallback.txt').open('w') as log:
  proc=subprocess.Popen(['/usr/bin/tv','--cpu','sleep 3'],stdout=log,stderr=subprocess.STDOUT,start_new_session=True);start=time.monotonic()
  while proc.poll() is None:check();assert time.monotonic()-start<15;time.sleep(.1)
 assert proc.returncode==0;cleanup();check();sessions+=1
 (O/'installed.txt').write_text(subprocess.check_output(['apk','info','-W','/usr/lib/libEGL.so.1.0.0','/usr/lib/dri/v3d_dri.so'],text=True))
 (O/'final-state.txt').write_text(Path('/sys/kernel/debug/dri/1/state').read_text())
 state='complete'
finally:
 if proc and proc.poll() is None:
  subprocess.run(['pkill','-TERM','-x','sway']);proc.wait(timeout=8)
 subprocess.run('killall watchdog; sleep 1; printf V > /dev/watchdog',shell=True,check=True)
 stop.set();thread.join();(O/'state.json').write_text(json.dumps(dict(state=state,sessions=sessions),indent=2));print(O,state,flush=True)
