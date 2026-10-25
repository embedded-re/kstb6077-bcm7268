import os,subprocess,threading,time,datetime,json,shutil
build='23.3.6-b1e71af4'
out='/root/gpu-runs/'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S')+'-compute-sync-recovery'
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
base.update(V3D_COMPUTE33='1')
with open(out+'/meta.txt','w') as f:
 subprocess.run(['sh','-c','uname -a; readlink /opt/mesa/current; sha256sum /opt/mesa/23.3.6-b1e71af4/lib/dri/v3d_dri.so /opt/gpu-test/kstb-compute-sync-test /opt/gpu-test/kstb-reset-probe'],stdout=f,check=True)
 f.write('numbered Mesa V3D_COMPUTE33=1 without version override; intentional reset in second phase\n')
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
tests=[('sync-200',False),('reset-sync-50',True)]
results=[]
try:
 for label,reset in tests:
  env=base.copy()
  env.pop('V3D_DEBUG',None)
  if reset:env['KSTB_RESET_PROBE']='1'
  start=time.monotonic()
  with open(out+'/'+label+'.txt','w') as f:
   try:
    r=subprocess.run(['/opt/gpu-test/kstb-compute-sync-test','50' if reset else '200','4097'],env=env,stdout=f,stderr=subprocess.STDOUT,timeout=240)
    status=r.returncode
   except subprocess.TimeoutExpired:status='Timeout'
  time.sleep(1)
  kernel=open(out+'/kernel.txt').read()
  faults=__import__('re').findall('MMU error|Oops|BUG:|Unable to handle|Internal error',kernel)
  resets=kernel.count('Resetting GPU')
  results.append(dict(test=label,exit=status,start=start,end=time.monotonic(),reset_expected=reset,reset_count=resets,faults=faults))
  open(out+'/results.json','w').write(json.dumps(results,indent=2)+'\n')
  print(label,status,'resets',resets,'faults',faults,flush=True)
  if status or faults or resets!=int(reset):break
finally:
 subprocess.run('killall watchdog; sleep 1; printf V > /dev/watchdog',shell=True)
 time.sleep(1);stop.set();t.join()
 print(out,flush=True)
assert len(results)==2 and all(x['exit']==0 and not x['faults'] and x['reset_count']==int(x['reset_expected']) for x in results)
