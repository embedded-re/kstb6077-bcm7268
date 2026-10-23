import os,subprocess,threading,time
out='/root/gpu-runs/20261023-threaded-store-fix-sizes'
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
  r=subprocess.run('eval "$(kstb-mesa env 23.3.6-848c0204)"; for dims in "1 1" "17 13" "256 256" "1920 1080"; do LIBGL_DRIVERS_PATH=/tmp/kstb-threaded-store MESA_SHADER_CACHE_DISABLE=true V3D_DEBUG=shaderdb MESA_GLES_VERSION_OVERRIDE=3.1 /tmp/kstb-threaded-store/kstb-tmu-write-test $dims || exit; done',shell=True,stdout=f,stderr=subprocess.STDOUT,timeout=45)
 print('test exit',r.returncode)
finally:
 subprocess.run('killall watchdog; sleep 1; printf V > /dev/watchdog',shell=True)
 stop.set();t.join()
print(out)
