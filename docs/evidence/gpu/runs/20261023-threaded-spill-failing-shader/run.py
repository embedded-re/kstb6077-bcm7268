import os,subprocess,threading,time
out='/root/gpu-runs/20261023-threaded-spill-failing-shader'
os.makedirs(out,exist_ok=True)
subprocess.run(['sh','-c','uname -a; readlink /opt/mesa/current; sha256sum /tmp/kstb-threaded-store/v3d_dri.so'],stdout=open(out+'/meta.txt','w'))
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
try:
 with open(out+'/shader-and-result.txt','w') as f:
  cmd = 'eval "$(kstb-mesa env 23.3.6-848c0204)"; cd /opt/deqp/gles3; LIBGL_DRIVERS_PATH=/tmp/kstb-threaded-store MESA_SHADER_CACHE_DISABLE=true V3D_DEBUG=qpu,vir,shaderdb ./deqp-gles3 --deqp-case=dEQP-GLES3.functional.texture.units.all_units.only_2d.0 --deqp-surface-type=window --deqp-gl-config-name=rgba8888d24s8ms0 --deqp-surface-width=256 --deqp-surface-height=256 --deqp-log-filename='+out+'/test.qpa --deqp-log-images=disable'
  r=subprocess.run(cmd,shell=True,stdout=f,stderr=subprocess.STDOUT,timeout=120)
 print('test exit',r.returncode)
finally:
 subprocess.run('killall watchdog; sleep 1; printf V > /dev/watchdog',shell=True)
 stop.set();t.join()
print(out)
