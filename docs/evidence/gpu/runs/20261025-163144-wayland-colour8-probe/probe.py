"""Bounded HDMI Sway probe; leave defaults intact, stop on kernel faults."""
import datetime,json,os,re,runpy,shlex,signal,subprocess,threading,time
from pathlib import Path
BUILD='23.3.6-40ea9bc5'
OUT=Path('/root/gpu-runs')/(datetime.datetime.now().strftime('%Y%m%d-%H%M%S')+'-wayland-colour8-probe')
OUT.mkdir();(OUT/'probe.py').write_text(Path(__file__).read_text())
Path('/root/gpu-kernel-ab/sway-run-path').write_text(str(OUT)+'\n')
assert not subprocess.run(['pidof','sway'],capture_output=True).stdout.strip(),'existing compositor'
stage=Path('/tmp/kstb-wayland-gles-probe');stage.mkdir(exist_ok=True)
runtime=stage/'runtime';runtime.mkdir(exist_ok=True);runtime.chmod(0o700);os.chown(runtime,1000,1000)
(stage/'mesa-env').write_text(subprocess.check_output(['kstb-mesa','env',BUILD],text=True))
(stage/'sway.conf').write_text('output * render_bit_depth 8\ndefault_border normal\nfont pango:monospace 16\nseat * hide_cursor 100\nfor_window [app_id="kstb-wayland-gles-test"] floating enable\nfor_window [app_id="kstb-wayland-gles-test"] resize set 960 640\nexec /opt/gpu-test/kstb-wayland-gles-test 45 > /tmp/kstb-wayland-gles-probe/runtime/client.txt 2>&1\n')
(stage/'launch.sh').write_text('''#!/bin/sh
. /tmp/kstb-wayland-gles-probe/mesa-env
. /etc/profile.d/kstb-locale.sh
unset DISPLAY WAYLAND_DISPLAY WAYLAND_SOCKET V3D_COMPUTE33 MESA_GLES_VERSION_OVERRIDE MESA_GLSL_VERSION_OVERRIDE
export XDG_RUNTIME_DIR=/tmp/kstb-wayland-gles-probe/runtime
export WLR_RENDERER=gles2 WLR_DRM_DEVICES=/dev/dri/card1 WLR_LIBINPUT_NO_DEVICES=1
export WLR_SCENE_DISABLE_DIRECT_SCANOUT=1 LIBSEAT_BACKEND=seatd MESA_SHADER_CACHE_DISABLE=true
exec sway -d -c /tmp/kstb-wayland-gles-probe/sway.conf
''');(stage/'launch.sh').chmod(0o755)
stop=threading.Event();reader=runpy.run_path('/opt/gpu-test/kstb-gpu-run')['kmsg_reader'];thread=threading.Thread(target=reader,args=(str(OUT/'kernel.txt'),stop));thread.start();time.sleep(.3)
def kernel_ok():
 text=(OUT/'kernel.txt').read_text()
 assert not re.search(r'Resetting GPU|MMU error|Oops|BUG:|Unable to handle|Internal error|kmsg overrun',text),'kernel fault'
def user_run(args):
 cmd=shlex.join(['env','XDG_RUNTIME_DIR='+str(runtime)]+args)
 return subprocess.run(['su','-l','kstb','-c',cmd],capture_output=True,text=True,timeout=10)
def fdinfo():
 rows={}
 for proc in Path('/proc').glob('[0-9]*'):
  try:
   if (proc/'comm').read_text().strip()!='sway' and not os.readlink(proc/'exe').endswith('/kstb-wayland-gles-test'):continue
   for fd in (proc/'fd').iterdir():
    link=os.readlink(fd)
    if '/dev/dri/' in link:rows[str(proc.name)+':'+fd.name]=dict(node=link,text=(proc/'fdinfo'/fd.name).read_text())
  except (OSError,ProcessLookupError):pass
 return rows
subprocess.run(['watchdog','-T','30','-t','5','/dev/watchdog'],check=True)
proc=None;state='stopped'
try:
 with (OUT/'sway.txt').open('w') as log:
  proc=subprocess.Popen(['su','-l','kstb','-c',str(stage/'launch.sh')],stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
  start=time.monotonic();sock=None
  while time.monotonic()-start<12:
   kernel_ok()
   if proc.poll() is not None:break
   sockets=list(runtime.glob('sway-ipc.*.sock'))
   if sockets:sock=sockets[0];break
   time.sleep(.1)
  assert sock,'Sway did not start'
  time.sleep(1);kernel_ok()
  for typ in ('get_outputs','get_tree'):
   r=user_run(['swaymsg','-s',str(sock),'-t',typ,'-r']);(OUT/(typ+'.json')).write_text(r.stdout);assert r.returncode==0,r.stderr
  (OUT/'fdinfo-before.json').write_text(json.dumps(fdinfo(),indent=2)+'\n')
  for phase,command in [('floating',''),('resized','[app_id="kstb-wayland-gles-test"] resize set 640 480'),('fullscreen','[app_id="kstb-wayland-gles-test"] fullscreen enable'),('windowed','[app_id="kstb-wayland-gles-test"] fullscreen disable')]:
   if command:
    r=user_run(['swaymsg','-s',str(sock),command]);assert r.returncode==0,r.stderr
   for i in range(65):kernel_ok();time.sleep(.1)
   display=next(p.name for p in runtime.glob('wayland-*') if not p.name.endswith('.lock'))
   capture=runtime/(phase+'.png')
   r=user_run(['env','WAYLAND_DISPLAY='+display,'grim',str(capture)]);assert r.returncode==0,r.stderr
   (OUT/(phase+'.png')).write_bytes(capture.read_bytes())
   (OUT/(phase+'-fdinfo.json')).write_text(json.dumps(fdinfo(),indent=2)+'\n')
   (OUT/(phase+'-dmabuf.txt')).write_text(Path('/sys/kernel/debug/dma_buf/bufinfo').read_text())
   print(phase,'VISIBLE',flush=True)
  (OUT/'fdinfo-after.json').write_text(json.dumps(fdinfo(),indent=2)+'\n')
  (OUT/'dmabuf-bufinfo.txt').write_text(Path('/sys/kernel/debug/dma_buf/bufinfo').read_text())
  (OUT/'drm-state.txt').write_text(Path('/sys/kernel/debug/dri/1/state').read_text())
  while 'PASS: frames ' not in (runtime/'client.txt').read_text():
   kernel_ok();assert time.monotonic()-start<60,'client timeout'
   assert 'FAIL' not in (runtime/'client.txt').read_text(),'client failed'
   time.sleep(.1)
  (OUT/'client.txt').write_bytes((runtime/'client.txt').read_bytes())
  r=user_run(['swaymsg','-s',str(sock),'exit']);(OUT/'exit-request.txt').write_text(r.stdout+r.stderr)
  proc.wait(timeout=8);kernel_ok();assert proc.returncode==0,proc.returncode
  before=json.loads((OUT/'fdinfo-before.json').read_text());after=json.loads((OUT/'fdinfo-after.json').read_text())
  jobs=lambda d:sum(int(n) for row in d.values() for n in re.findall(r'v3d-jobs-render:\s*(\d+)',row['text']))
  delta=jobs(after)-jobs(before);assert delta>=20,('too few GPU frames',delta)
  text=(OUT/'sway.txt').read_text();assert 'GL renderer: V3D 3.3' in text,text[-1000:]
  client=(OUT/'client.txt').read_text();assert 'GL renderer: V3D 3.3' in client and 'FAIL' not in client
  assert len(set(re.findall(r'configure [0-9]+ ([0-9]+x[0-9]+)',client)))>=3,client
  (OUT/'summary.json').write_text(json.dumps(dict(gpu_render_jobs_delta=delta,renderer='V3D 3.3',format='inspect-drm-state',exit=proc.returncode),indent=2)+'\n')
  state='complete'
finally:
 if proc and proc.poll() is None:
  subprocess.run(['pkill','-TERM','-x','sway'])
  try:proc.wait(timeout=5)
  except subprocess.TimeoutExpired:os.killpg(proc.pid,signal.SIGKILL);proc.wait()
 subprocess.run('killall watchdog; sleep 1; printf V > /dev/watchdog',shell=True,check=True)
 stop.set();thread.join()
 (OUT/'state.json').write_text(json.dumps(dict(state=state),indent=2)+'\n')
 print(OUT,state,flush=True)
