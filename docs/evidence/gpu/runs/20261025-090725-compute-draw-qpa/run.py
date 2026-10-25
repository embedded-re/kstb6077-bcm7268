import os,subprocess,threading,time,datetime,json,shutil
build='23.3.6-3112d69b'
out='/root/gpu-runs/'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S')+'-compute-draw-qpa'
os.makedirs(out)
shutil.copyfile(__file__,out+'/run.py')
base=dict(os.environ)
line=subprocess.check_output(['kstb-mesa','env',build],text=True).strip()
assert line.startswith('export ')
for kv in line[7:].split():
 k,v=kv.split('=',1);base[k]=v
for k in ('MESA_GLES_VERSION_OVERRIDE','MESA_GLSL_VERSION_OVERRIDE','V3D_DEBUG'):base.pop(k,None)
base['MESA_SHADER_CACHE_DISABLE']='true'
base['LINES']='1'
base['EGL_PLATFORM']='drm'
base.update(V3D_COMPUTE33='1')
with open(out+'/meta.txt','w') as f:
 subprocess.run(['sh','-c','uname -a; readlink /opt/mesa/current; sha256sum /opt/mesa/23.3.6-3112d69b/lib/dri/v3d_dri.so'],stdout=f,check=True)
 f.write('scratch Mesa V3D_COMPUTE33=1 GLES 3.1 override\n')
stop=threading.Event()
def logs():
 fd=os.open('/dev/kmsg',os.O_RDONLY|os.O_NONBLOCK);os.lseek(fd,0,os.SEEK_END)
 with open(out+'/kernel.txt','w') as f:
  while not stop.is_set():
   try:f.write(os.read(fd,8192).decode());f.flush()
   except BlockingIOError:time.sleep(.1)
 os.close(fd)
t=threading.Thread(target=logs);t.start()
subprocess.run('watchdog -T 30 -t 5 /dev/watchdog',shell=True,check=True)
tests=[('rgba8','deqp-gles31',[
'--deqp-case=dEQP-GLES31.functional.draw_indirect.compute_interop.combined.drawarrays_compute_cmd_and_data',
'--deqp-surface-type=window',
'--deqp-gl-config-name=rgba8888d24s8ms0', '--deqp-surface-width=256', '--deqp-surface-height=256',
'--deqp-log-filename='+out+'/result.qpa'],False)]
results=[]
try:
 for label,binary,args,override in tests:
  env=base.copy()
  if label.startswith('rgba32ui'):env['KSTB_GATHER_RGBA']='1'
  if label.startswith('rgba16f'):env['KSTB_GATHER_F16']='1'

  env['KSTB_GATHER_BORDER']='1'
  env.pop('MESA_GLES_VERSION_OVERRIDE',None)
  if binary!='kstb-gl-caps':env['V3D_DEBUG']='shaderdb'
  start=time.monotonic()
  with open(out+'/'+label+'.txt','w') as f:
   try:
    r=subprocess.run(['/opt/deqp/gles31/'+binary]+args,env=env,stdout=f,stderr=subprocess.STDOUT,timeout=20,cwd='/opt/deqp/gles31')
    status=r.returncode
   except subprocess.TimeoutExpired:status='Timeout'
  results.append(dict(test=label,exit=status,start=start,end=time.monotonic(),gles_override=env.get('MESA_GLES_VERSION_OVERRIDE'),v3d_debug=env.get('V3D_DEBUG')))
  open(out+'/results.json','w').write(json.dumps(results,indent=2)+'\n')
  print(label,status,flush=True)
  if status=='Timeout' or status<0:break
finally:
 subprocess.run('killall watchdog; sleep 1; printf V > /dev/watchdog',shell=True)
 time.sleep(1);stop.set();t.join()
 print(out,flush=True)
assert len(results)==len(tests) and all(x['exit']==0 for x in results)
