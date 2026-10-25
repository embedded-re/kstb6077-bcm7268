"""Native GLES client under the installed TV launcher; controlled restarts."""
import datetime,hashlib,json,os,re,runpy,subprocess,threading,time
from pathlib import Path
BUILD='23.3.6-40ea9bc5'
OUT=Path('/root/gpu-runs')/(datetime.datetime.now().strftime('%Y%m%d-%H%M%S')+'-wayland-colour8-gate');OUT.mkdir()
(OUT/'run.py').write_text(Path(__file__).read_text());Path('/root/gpu-kernel-ab/sway-run-path').write_text(str(OUT)+'\n')
assert not subprocess.run(['pidof','sway'],capture_output=True).stdout.strip()
meta=dict(uname=subprocess.check_output(['uname','-a'],text=True).strip(),selected=os.readlink('/opt/mesa/current'),sha256={})
for p in ['/usr/bin/tv','/etc/kstb-tv/sway.conf','/etc/kstb-tv/sway-gpu.conf','/opt/gpu-test/kstb-wayland-gles-test','/opt/mesa/'+BUILD+'/lib/dri/v3d_dri.so','/opt/mesa/'+BUILD+'/lib/libEGL.so.1.0.0']:
 meta['sha256'][p]=hashlib.sha256(Path(p).read_bytes()).hexdigest()
(OUT/'meta.json').write_text(json.dumps(meta,indent=2)+'\n');(OUT/'tv-tested').write_bytes(Path('/usr/bin/tv').read_bytes())
stage=Path('/tmp/kstb-wayland-gles-probe');runtime=Path('/tmp/xdg-1000');ticks=os.sysconf('SC_CLK_TCK')
stop=threading.Event();reader=runpy.run_path('/opt/gpu-test/kstb-gpu-run')['kmsg_reader'];thread=threading.Thread(target=reader,args=(str(OUT/'kernel.txt'),stop));thread.start();time.sleep(.3)
def kernel_ok():
 assert not re.search(r'Resetting GPU|MMU error|Oops|BUG:|Unable to handle|Internal error|kmsg overrun',(OUT/'kernel.txt').read_text()),'kernel fault'
def snapshot():
 fds={};cpu={}
 for proc in Path('/proc').glob('[0-9]*'):
  try:
   exe=Path(os.readlink(proc/'exe')).name
   if exe not in ('sway','kstb-wayland-gles-test'):continue
   stat=(proc/'stat').read_text().rsplit(')',1)[1].split();cpu[proc.name]=dict(name=exe,seconds=(int(stat[11])+int(stat[12]))/ticks)
   for fd in (proc/'fd').iterdir():
    link=os.readlink(fd)
    if '/dev/dri/' in link:fds[proc.name+':'+fd.name]=dict(node=link,name=exe,text=(proc/'fdinfo'/fd.name).read_text())
  except OSError:pass
 return fds,cpu
def jobs(rows):return sum(int(n) for row in rows.values() for n in re.findall(r'v3d-jobs-render:\s*(\d+)',row['text']))
def user_run(args):
 import shlex
 return subprocess.run(['su','-l','kstb','-c',shlex.join(['env','XDG_RUNTIME_DIR='+str(runtime)]+args)],capture_output=True,text=True,timeout=10)
subprocess.run(['watchdog','-T','30','-t','5','/dev/watchdog'],check=True)
state='stopped';proc=None;results=[]
try:
 for label,seconds,protocol,kind in [('native-resize',12,False,'native'),('native-protocol',6,True,'native'),('native-restart',12,False,'native'),('cpu-fallback',3,False,'cpu'),('older-package',3,False,'old')]:
  out=OUT/label;out.mkdir();client_log=runtime/(label+'-client.txt')
  script=stage/(label+'.sh')
  if kind=='native':script.write_text('#!/bin/sh\nexec '+('env WAYLAND_DEBUG=client ' if protocol else '')+'/opt/gpu-test/kstb-wayland-gles-test '+str(seconds)+' > '+str(client_log)+' 2>&1\n')
  else:script.write_text('#!/bin/sh\nprintf "TV '+label+' rollback check\\n"\nsleep '+str(seconds)+'\n')
  script.chmod(0o755);(out/'client.sh').write_bytes(script.read_bytes())
  env=os.environ.copy()
  if kind=='old':env['KSTB_TV_MESA']='23.3.6-85a951cd'
  cmd=['/usr/bin/tv','--cpu' if kind=='cpu' else '--gpu',str(script)]
  start=time.monotonic()
  with (out/'launcher.txt').open('w') as log:
   proc=subprocess.Popen(cmd,env=env,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
   while True:
    kernel_ok();assert proc.poll() is None,'launcher exited early';assert time.monotonic()-start<12,'startup timeout'
    before,cpu_before=snapshot()
    if before and list(runtime.glob('sway-ipc.*.sock')) and (kind!='native' or any(row['name']=='kstb-wayland-gles-test' for row in before.values())):break
    time.sleep(.1)
   sock=next(runtime.glob('sway-ipc.*.sock'))
   drm=Path('/sys/kernel/debug/dri/1/state').read_text();(out/'drm-state.txt').write_text(drm)
   assert ('format=RG16' if kind=='cpu' else 'format=AR24') in drm,drm
   (out/'fdinfo-before.json').write_text(json.dumps(before,indent=2)+'\n')
   for pid in cpu_before:
    (out/('maps-'+pid+'.txt')).write_text(Path('/proc/'+pid+'/maps').read_text())
   sample_start=time.monotonic();latest=dict(before);cpu_after=dict(cpu_before);crc=[];crc_at=0;phase=0
   commands=['floating enable, resize set 800 600','fullscreen enable','fullscreen disable']
   while proc.poll() is None:
    kernel_ok();assert time.monotonic()-start<seconds+15,'session timeout'
    current,cpus=snapshot();latest.update(current);cpu_after.update(cpus)
    if kind=='native' and label=='native-resize' and phase<3 and time.monotonic()-sample_start>2+phase*3:
     r=user_run(['swaymsg','-s',str(sock),'[app_id="kstb-wayland-gles-test"] '+commands[phase]]);assert r.returncode==0,r.stderr;phase+=1
    if len(crc)<4 and time.monotonic()-crc_at>.7:
     crc.append(subprocess.check_output(['/usr/sbin/kstb-reg','0xf06f8014'],text=True).strip());crc_at=time.monotonic()
    time.sleep(.1)
   sample_elapsed=time.monotonic()-sample_start;assert proc.returncode==0,(label,proc.returncode)
  kernel_ok();(out/'sway.txt').write_text((runtime/'tv.log').read_text());(out/'fdinfo-after.json').write_text(json.dumps(latest,indent=2)+'\n');(out/'hdmi-crc.json').write_text(json.dumps(crc,indent=2)+'\n')
  cpu_result={cpu_before[pid]['name']:(row['seconds']-cpu_before[pid]['seconds'])/sample_elapsed*100 for pid,row in cpu_after.items() if pid in cpu_before}
  delta=jobs(latest)-jobs(before);summary=dict(label=label,seconds=time.monotonic()-start,gpu_render_jobs_delta=delta,cpu_percent_one_core=cpu_result,exit=proc.returncode)
  if kind=='native':
   text=client_log.read_text();(out/'client.txt').write_text(text);assert 'GL renderer: V3D 3.3' in text and 'PASS: frames ' in text and 'FAIL' not in text,text[-2000:]
   summary['frames']=int(re.search(r'PASS: frames (\d+)',text).group(1));summary['fps']=float(re.search(r'PASS: frames .*? fps ([0-9.]+)',text).group(1))
   assert summary['frames']>=seconds*45 and delta>=seconds*45,(label,summary)
   summary['minimum_reported_fps']=min(float(x) for x in re.findall(r'FRAME .*? fps ([0-9.]+)',text))
   assert summary['fps']>=55 and summary['minimum_reported_fps']>=45,summary
   assert len(set(crc))>=2,crc
   if protocol:
    assert ('zwp_linux_buffer_params_v1' in text and 'create_immed' in text) or 'create_prime_buffer' in text,'no GPU buffer protocol'
    summary['buffer_protocol']='linux-dmabuf' if 'zwp_linux_buffer_params_v1' in text else 'wl_drm-prime'
  elif kind=='cpu':assert delta==0,summary
  else:
   assert any('/opt/mesa/23.3.6-85a951cd/lib/' in f.read_text() for f in out.glob('maps-*.txt')),'override lost at su'
  assert Path('/sys/class/tty/tty0/active').read_text().strip()=='tty1'
  dma=Path('/sys/kernel/debug/dma_buf/bufinfo').read_text();(out/'dmabuf-after.txt').write_text(dma);assert 'Total 0 objects, 0 bytes' in dma,dma
  results.append(summary);(OUT/'results.json').write_text(json.dumps(results,indent=2)+'\n');print(label,'PASS',summary,flush=True);time.sleep(.5)
 env=os.environ.copy();line=subprocess.check_output(['kstb-mesa','env',BUILD],text=True).strip();env.update(dict(x.split('=',1) for x in line[7:].split()));env.pop('V3D_COMPUTE33',None)
 with (OUT/'native-fence.txt').open('w') as log:
  proc=subprocess.Popen(['/opt/gpu-test/kstb-native-fence-test'],env=env,stdout=log,stderr=subprocess.STDOUT,start_new_session=True);start=time.monotonic()
  while proc.poll() is None:kernel_ok();assert time.monotonic()-start<30;time.sleep(.1)
 assert proc.returncode==0 and 'OK: 100 native fence' in (OUT/'native-fence.txt').read_text()
 kernel_ok();state='complete'
finally:
 if proc and proc.poll() is None:
  subprocess.run(['pkill','-TERM','-x','sway'])
  try:proc.wait(timeout=5)
  except subprocess.TimeoutExpired:os.killpg(proc.pid,9);proc.wait()
 subprocess.run('killall watchdog; sleep 1; printf V > /dev/watchdog',shell=True,check=True);stop.set();thread.join()
 (OUT/'state.json').write_text(json.dumps(dict(state=state,sessions=len(results)),indent=2)+'\n');print(OUT,state,flush=True)
