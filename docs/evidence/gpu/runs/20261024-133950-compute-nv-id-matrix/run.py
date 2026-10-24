import os,subprocess,threading,time,datetime,json,shutil
build='23.3.6-fe7dda5e'
out='/root/gpu-runs/'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S')+'-compute-nv-id-matrix'
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
for name in ('render-only.py','v3dpack.py','v3d_packet_v33.xml'):shutil.copyfile('/tmp/kstb-compute-probe/'+name,out+'/'+name)
with open(out+'/meta.txt','w') as f:
 subprocess.run(['sh','-c','uname -a; readlink /opt/mesa/current; sha256sum /tmp/kstb-compute-probe/*.py /tmp/kstb-compute-probe/*.xml'],stdout=f,check=True)
 f.write('raw DRM SUBMIT_CL, bcl_start=bcl_end=0; no Mesa loading\n')
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
tests=[(str(n)+'-lines','raw-probe',[str(n)],False) for n in range(1,17)]
results=[]
try:
 for label,binary,args,override in tests:
  env=base.copy()
  env['LINES']=args[0]
  if override:env['MESA_GLES_VERSION_OVERRIDE']='3.1'
  if binary!='kstb-gl-caps':env['V3D_DEBUG']='shaderdb,surface'
  start=time.monotonic()
  with open(out+'/'+label+'.txt','w') as f:
   try:
    r=subprocess.run(['python3','/tmp/kstb-compute-probe/render-only.py'],env=env,stdout=f,stderr=subprocess.STDOUT,timeout=20)
    status=r.returncode
   except subprocess.TimeoutExpired:status='Timeout'
  results.append(dict(test=label,exit=status,start=start,end=time.monotonic(),gles_override=env.get('MESA_GLES_VERSION_OVERRIDE'),v3d_debug=env.get('V3D_DEBUG')))
  open(out+'/results.json','w').write(json.dumps(results,indent=2)+'\n')
  print(label,status,flush=True)
  if status:break
finally:
 subprocess.run('killall watchdog; sleep 1; printf V > /dev/watchdog',shell=True)
 time.sleep(1);stop.set();t.join()
 print(out,flush=True)
assert len(results)==len(tests) and all(x['exit']==0 for x in results)
