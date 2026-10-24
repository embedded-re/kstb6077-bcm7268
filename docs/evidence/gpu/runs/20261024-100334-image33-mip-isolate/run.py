import os,subprocess,threading,time,datetime,json,shutil
build='23.3.6-cd573075'
out='/root/gpu-runs/'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S')+'-image33-mip-isolate'
os.makedirs(out)
shutil.copyfile(__file__,out+'/run.py')
base=dict(os.environ)
line=subprocess.check_output(['kstb-mesa','env',build],text=True).strip()
assert line.startswith('export ')
for kv in line[7:].split():
 k,v=kv.split('=',1);base[k]=v
for k in ('MESA_GLES_VERSION_OVERRIDE','MESA_GLSL_VERSION_OVERRIDE','V3D_DEBUG'):base.pop(k,None)
base['MESA_SHADER_CACHE_DISABLE']='true'
base['LIBGL_DRIVERS_PATH']='/tmp/kstb-image33-nvformats'
with open(out+'/meta.txt','w') as f:
 subprocess.run(['sh','-c','uname -a; readlink /opt/mesa/current; sha256sum /opt/mesa/'+build+'/lib/dri/v3d_dri.so /tmp/kstb-image33-nvformats/v3d_dri.so /opt/gpu-test/kstb-image-format-test'],stdout=f,check=True)
 f.write('explicit mesa '+build+'\nMESA_SHADER_CACHE_DISABLE=true\n')
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
base['KSTB_CPU_FETCH']='1'
tests=[(f'{fmt}-{dim}-{w}x{h}-mip{level}','kstb-image-format-test',[fmt,str(w),str(h),dim,str(level)],True) for fmt in ('rgba32f','r32ui') for dim in ('2d','array','3d') for w,h,level in ((17,13,0),(17,13,1),(65,33,2),(256,256,0))]
results=[]
try:
 for label,binary,args,override in tests:
  env=base.copy()
  if override:env['MESA_GLES_VERSION_OVERRIDE']='3.1'
  if binary!='kstb-gl-caps':env['V3D_DEBUG']='shaderdb,surface'
  start=time.monotonic()
  with open(out+'/'+label+'.txt','w') as f:
   try:
    r=subprocess.run(['/opt/gpu-test/'+binary]+args,env=env,stdout=f,stderr=subprocess.STDOUT,timeout=120)
    status=r.returncode
   except subprocess.TimeoutExpired:status='Timeout'
  results.append(dict(test=label,exit=status,start=start,end=time.monotonic(),gles_override=env.get('MESA_GLES_VERSION_OVERRIDE'),v3d_debug=env.get('V3D_DEBUG')))
  open(out+'/results.json','w').write(json.dumps(results,indent=2)+'\n')
  print(label,status,flush=True)
  # Complete diagnostic matrix, including failed readbacks.
finally:
 subprocess.run('killall watchdog; sleep 1; printf V > /dev/watchdog',shell=True)
 time.sleep(1);stop.set();t.join()
 print(out,flush=True)
assert len(results)==len(tests) and all(x['exit']==0 for x in results)
