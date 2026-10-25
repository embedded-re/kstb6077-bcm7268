"""Validate the installed tv selectors, animated rendering and CPU rollback."""
import datetime,hashlib,json,os,re,runpy,subprocess,threading,time
from pathlib import Path
OUT=Path('/root/gpu-runs')/(datetime.datetime.now().strftime('%Y%m%d-%H%M%S')+'-tv-gpu-gate');OUT.mkdir()
(OUT/'run.py').write_text(Path(__file__).read_text());Path('/root/gpu-kernel-ab/sway-run-path').write_text(str(OUT)+'\n')
assert not subprocess.run(['pidof','sway'],capture_output=True).stdout.strip()
meta=dict(uname=subprocess.check_output(['uname','-a'],text=True).strip(),selected=os.readlink('/opt/mesa/current'),sha256={})
for p in ['/usr/bin/tv','/etc/kstb-tv/sway.conf','/opt/mesa/23.3.6-85a951cd/lib/dri/v3d_dri.so']:
 meta['sha256'][p]=hashlib.sha256(Path(p).read_bytes()).hexdigest()
(OUT/'meta.json').write_text(json.dumps(meta,indent=2)+'\n')
(OUT/'tv-tested').write_bytes(Path('/usr/bin/tv').read_bytes());(OUT/'sway.conf').write_bytes(Path('/etc/kstb-tv/sway.conf').read_bytes())
stage=Path('/tmp/kstb-sway-gpu-probe');runtime=Path('/tmp/xdg-1000')
stop=threading.Event();reader=runpy.run_path('/opt/gpu-test/kstb-gpu-run')['kmsg_reader'];thread=threading.Thread(target=reader,args=(str(OUT/'kernel.txt'),stop));thread.start();time.sleep(.3)
def kernel_ok():
 assert not re.search(r'Resetting GPU|MMU error|Oops|BUG:|Unable to handle|Internal error|kmsg overrun',(OUT/'kernel.txt').read_text()),'kernel fault'
def fdinfo():
 rows={}
 for proc in Path('/proc').glob('[0-9]*'):
  try:
   if (proc/'comm').read_text().strip()!='sway':continue
   for fd in (proc/'fd').iterdir():
    link=os.readlink(fd)
    if '/dev/dri/' in link:rows[proc.name+':'+fd.name]=dict(node=link,text=(proc/'fdinfo'/fd.name).read_text())
  except OSError:pass
 return rows
def jobs(rows):
 return sum(int(n) for row in rows.values() for n in re.findall(r'v3d-jobs-render:\s*(\d+)',row['text']))
subprocess.run(['watchdog','-T','30','-t','5','/dev/watchdog'],check=True)
state='stopped';proc=None;results=[]
try:
 for label,selector,frames in [('gpu-first','--gpu',30),('gpu-long','--gpu',300),('cpu-rollback','--cpu',30),('default-cpu',None,30),('gpu-restart','--gpu',30)]:
  out=OUT/label;out.mkdir();gpu=selector=='--gpu'
  script=stage/(label+'.sh');script.write_text('#!/bin/sh\nprintf "\\033[2J\\033[H KSTB6077 GPU GUI TEST\\n"\ni=0\nwhile [ "$i" -lt '+str(frames)+' ]; do\n printf "\\033[2;1HFrame %04d  '+label+'\\n\\033[41m RED \\033[42m GREEN \\033[44m BLUE \\033[0m\\n" "$i"\n i=$((i+1)); sleep .1\ndone\n');script.chmod(0o755)
  (out/'client.sh').write_bytes(script.read_bytes())
  cmd=['/usr/bin/tv']+([selector] if selector else [])+[str(script)]
  start=time.monotonic()
  with (out/'launcher.txt').open('w') as log:
   proc=subprocess.Popen(cmd,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
   while not list(runtime.glob('sway-ipc.*.sock')):
    kernel_ok();assert proc.poll() is None,'launcher exited early';assert time.monotonic()-start<12,'startup timeout';time.sleep(.1)
   time.sleep(.5);before=fdinfo();(out/'fdinfo-before.json').write_text(json.dumps(before,indent=2)+'\n')
   pids={key.split(':')[0] for key in before}
   for pid in pids:
    (out/('maps-'+pid+'.txt')).write_text(Path('/proc/'+pid+'/maps').read_text())
    raw=Path('/proc/'+pid+'/environ').read_bytes().split(b'\0')
    selected={x.split(b'=',1)[0].decode():x.split(b'=',1)[1].decode() for x in raw if b'=' in x and x.split(b'=',1)[0] in (b'WLR_RENDERER',b'WLR_DRM_DEVICES',b'LD_LIBRARY_PATH',b'LIBGL_DRIVERS_PATH')}
    (out/'environment.json').write_text(json.dumps(selected,indent=2)+'\n')
   crc=[];crc_at=0
   while proc.poll() is None:
    kernel_ok();assert time.monotonic()-start<frames*.1+15,'session timeout'
    current=fdinfo()
    if current:(out/'fdinfo-after.json').write_text(json.dumps(current,indent=2)+'\n')
    if (out/'dmabuf-bufinfo.txt').exists() is False and time.monotonic()-start>2:
     (out/'dmabuf-bufinfo.txt').write_text(Path('/sys/kernel/debug/dma_buf/bufinfo').read_text())
     (out/'drm-state.txt').write_text(Path('/sys/kernel/debug/dri/1/state').read_text())
    if len(crc)<4 and time.monotonic()-crc_at>.5:
     value=subprocess.check_output(['/usr/sbin/kstb-reg','0xf06f8014'],text=True).strip()
     crc.append(value);crc_at=time.monotonic();(out/'hdmi-crc.json').write_text(json.dumps(crc,indent=2)+'\n')
    time.sleep(.2)
   assert proc.returncode==0,(label,proc.returncode)
  kernel_ok();text=(runtime/'tv.log').read_text();(out/'sway.txt').write_text(text)
  after=json.loads((out/'fdinfo-after.json').read_text());delta=jobs(after)-jobs(before)
  if gpu:
   assert selected['WLR_RENDERER']=='gles2',selected;assert delta>=frames*.7,delta
   assert any('/opt/mesa/23.3.6-85a951cd/lib/dri/' in f.read_text() for f in out.glob('maps-*.txt')),'wrong Mesa'
  else:assert selected['WLR_RENDERER']=='pixman' and delta==0,(label,delta)
  assert len(set(crc))>=2,('HDMI CRC did not change',crc)
  assert Path('/sys/class/tty/tty0/active').read_text().strip()=='tty1','console not restored'
  results.append(dict(label=label,selector=selector,seconds=time.monotonic()-start,gpu_render_jobs_delta=delta,exit=proc.returncode,console='tty1'))
  (OUT/'results.json').write_text(json.dumps(results,indent=2)+'\n');print(label,'PASS',delta,'GPU jobs',flush=True);time.sleep(.5)
 state='complete'
finally:
 if proc and proc.poll() is None:
  subprocess.run(['pkill','-TERM','-x','sway'])
  try:proc.wait(timeout=5)
  except subprocess.TimeoutExpired:os.killpg(proc.pid,9);proc.wait()
 subprocess.run('killall watchdog; sleep 1; printf V > /dev/watchdog',shell=True,check=True)
 stop.set();thread.join();(OUT/'state.json').write_text(json.dumps(dict(state=state,sessions=len(results)),indent=2)+'\n');print(OUT,state,flush=True)
