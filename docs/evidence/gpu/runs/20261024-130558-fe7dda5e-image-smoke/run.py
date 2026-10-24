import os,subprocess,threading,time,datetime,json,shutil
build='23.3.6-fe7dda5e'
out='/root/gpu-runs/'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S')+'-fe7dda5e-image-smoke'
os.makedirs(out)
shutil.copyfile(__file__,out+'/run.py')
base=dict(os.environ)
line=subprocess.check_output(['kstb-mesa','env',build],text=True).strip()
assert line.startswith('export ')
for kv in line[7:].split():
 k,v=kv.split('=',1);base[k]=v
for k in ('MESA_GLES_VERSION_OVERRIDE','MESA_GLSL_VERSION_OVERRIDE','V3D_DEBUG'):base.pop(k,None)
base['MESA_SHADER_CACHE_DISABLE']='true'
base['LIBGL_DRIVERS_PATH']='/opt/mesa/'+build+'/lib/dri'
with open(out+'/meta.txt','w') as f:
 subprocess.run(['sh','-c','uname -a; readlink /opt/mesa/current; sha256sum /opt/mesa/'+build+'/lib/dri/v3d_dri.so /opt/mesa/'+build+'/lib/dri/v3d_dri.so /opt/gpu-test/kstb-image-format-test'],stdout=f,check=True)
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
tests=[(fmt+'-17x13','kstb-image-format-test',[fmt,'17','13'],True) for fmt in ('r32ui', 'r32i', 'r32f', 'rgba32ui', 'rgba32i', 'rgba32f', 'rgba16ui', 'rgba16i', 'rgba16f', 'rgba8ui', 'rgba8i', 'rgba8', 'rgba8_snorm', 'rg32f', 'rg16f', 'r11f_g11f_b10f', 'r16f', 'rgb10_a2ui', 'rg32ui', 'rg16ui', 'rg8ui', 'r16ui', 'r8ui', 'rg32i', 'rg16i', 'rg8i', 'r16i', 'r8i', 'rgb10_a2', 'rg8', 'r8', 'rg8_snorm', 'r8_snorm')]
tests += [(f'atomic-{w}x{h}','kstb-image-atomic-test',[str(w),str(h)],True) for w,h in ((1,1),(17,13),(64,64),(256,256))]
tests += [(f'layer-{dim}','kstb-image-format-test',['rgba32f','65','65',dim,'2'],True) for dim in ('array','3d','cube','array-single','3d-single','cube-single')]
tests += [(f'oob-{shift}','kstb-image-test',['17','13',str(shift)],True) for shift in (-1,0,1)]

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
  if status:break
finally:
 subprocess.run('killall watchdog; sleep 1; printf V > /dev/watchdog',shell=True)
 time.sleep(1);stop.set();t.join()
 print(out,flush=True)
assert len(results)==len(tests) and all(x['exit']==0 for x in results)
