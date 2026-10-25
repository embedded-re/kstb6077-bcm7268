"""Bounded HDMI Sway probe; leave defaults intact, stop on kernel faults."""
import datetime,json,os,re,runpy,shlex,signal,subprocess,threading,time
from pathlib import Path
BUILD='23.3.6-85a951cd'
OUT=Path('/root/gpu-runs')/(datetime.datetime.now().strftime('%Y%m%d-%H%M%S')+'-sway-gles-probe')
OUT.mkdir();(OUT/'probe.py').write_text(Path(__file__).read_text())
Path('/root/gpu-kernel-ab/sway-run-path').write_text(str(OUT)+'\n')
assert not subprocess.run(['pidof','sway'],capture_output=True).stdout.strip(),'existing compositor'
stage=Path('/tmp/kstb-sway-gpu-probe');stage.mkdir(exist_ok=True)
runtime=stage/'runtime';runtime.mkdir(exist_ok=True);runtime.chmod(0o700);os.chown(runtime,1000,1000)
(stage/'mesa-env').write_text(subprocess.check_output(['kstb-mesa','env',BUILD],text=True))
(stage/'sway.conf').write_text('''output * render_bit_depth 6
output * bg #243050 solid_color
default_border pixel 2
font pango:monospace 14
seat * hide_cursor 100
exec foot --app-id=kstb-gpu-test /tmp/kstb-sway-gpu-probe/client.sh
''')
(stage/'client.sh').write_text('''#!/bin/sh
printf '\\033[2J\\033[H GPU COMPOSITION TEST - KSTB6077\\n'
i=0
while [ "$i" -lt 100 ]; do
 printf '\\033[2;1HFrame %03d  - animated terminal, RGB565 HDMI\\n' "$i"
 printf '\\033[41m RED \\033[42m GREEN \\033[44m BLUE \\033[0m\\n'
 i=$((i+1)); sleep .1
done
sleep 20
''');(stage/'client.sh').chmod(0o755)
(stage/'launch.sh').write_text('''#!/bin/sh
. /tmp/kstb-sway-gpu-probe/mesa-env
. /etc/profile.d/kstb-locale.sh
unset DISPLAY WAYLAND_DISPLAY WAYLAND_SOCKET V3D_COMPUTE33 MESA_GLES_VERSION_OVERRIDE MESA_GLSL_VERSION_OVERRIDE
export XDG_RUNTIME_DIR=/tmp/kstb-sway-gpu-probe/runtime
export WLR_RENDERER=gles2 WLR_DRM_DEVICES=/dev/dri/card1 WLR_LIBINPUT_NO_DEVICES=1
export WLR_SCENE_DISABLE_DIRECT_SCANOUT=1 LIBSEAT_BACKEND=seatd MESA_SHADER_CACHE_DISABLE=true
exec sway -d -c /tmp/kstb-sway-gpu-probe/sway.conf
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
   if (proc/'comm').read_text().strip()!='sway':continue
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
  for i in range(30):kernel_ok();time.sleep(.1)
  # Discover Wayland socket rather than assuming its suffix.
  display=next(p.name for p in runtime.glob('wayland-*') if not p.name.endswith('.lock'))
  r=user_run(['env','WAYLAND_DISPLAY='+display,'grim',str(runtime/'capture.png')]);(OUT/'grim.txt').write_text(r.stdout+r.stderr);assert r.returncode==0,'screenshot failed'
  (OUT/'capture.png').write_bytes((runtime/'capture.png').read_bytes())
  (OUT/'fdinfo-after.json').write_text(json.dumps(fdinfo(),indent=2)+'\n')
  (OUT/'dmabuf-bufinfo.txt').write_text(Path('/sys/kernel/debug/dma_buf/bufinfo').read_text())
  (OUT/'drm-state.txt').write_text(Path('/sys/kernel/debug/dri/1/state').read_text())
  r=user_run(['swaymsg','-s',str(sock),'exit']);(OUT/'exit-request.txt').write_text(r.stdout+r.stderr)
  proc.wait(timeout=8);kernel_ok();assert proc.returncode==0,proc.returncode
  before=json.loads((OUT/'fdinfo-before.json').read_text());after=json.loads((OUT/'fdinfo-after.json').read_text())
  jobs=lambda d:sum(int(n) for row in d.values() for n in re.findall(r'v3d-jobs-render:\s*(\d+)',row['text']))
  delta=jobs(after)-jobs(before);assert delta>=20,('too few GPU frames',delta)
  text=(OUT/'sway.txt').read_text();assert 'GL renderer: V3D 3.3' in text,text[-1000:]
  (OUT/'summary.json').write_text(json.dumps(dict(gpu_render_jobs_delta=delta,renderer='V3D 3.3',format='RGB565',exit=proc.returncode),indent=2)+'\n')
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
