import hashlib,json,os,runpy,signal,subprocess,threading,time
from pathlib import Path
p=Path('/root/storage-migrate-result');p.mkdir(exist_ok=True)
assert not subprocess.run(['pidof','watchdog','sway','firefox'],capture_output=True).stdout.strip()
stop=threading.Event();reader=threading.Thread(target=runpy.run_path('/opt/gpu-test/kstb-gpu-run')['kmsg_reader'],args=(str(p/'kernel.txt'),stop));reader.start();time.sleep(.25)
guard=runpy.run_path('/opt/gpu-test/kstb_fault_guard.py')['KernelFaultGuard'](p/'kernel.txt').start()
wd=subprocess.Popen(['watchdog','-F','-T','30','-t','5','/dev/watchdog'],start_new_session=True)
state=dict(state='stopped',tests=[]);child=None
try:
 for name,cmd in [('classifier',['su','-l','kstb','-c','env OPENBLAS_NUM_THREADS=1 /home/kstb/projects/.venv/bin/python /home/kstb/projects/image-classifier/classify.py --self-test --threads 4 --report /home/kstb/projects/image-classifier/post-storage.json']),('fences',['/root/gpu-system-prep/kstb-native-fence-test']),('graphics',['/root/gpu-system-prep/kstb-msaa-depth-stencil-test']),('caps',['/root/math-audit/kstb-gl-caps','3'])]:
  with (p/(name+'.txt')).open('w') as log:
   child=subprocess.Popen(cmd,stdout=log,stderr=subprocess.STDOUT,start_new_session=True);guard.attach(child)
   try:child.wait(timeout=45)
   finally:
    if child.poll() is None:os.killpg(child.pid,signal.SIGKILL);child.wait(timeout=5)
  time.sleep(.3);guard.check();assert child.returncode==0,(name,child.returncode);state['tests'].append(name)
 state['state']='complete'
finally:
 if child is not None and child.poll() is None:os.killpg(child.pid,signal.SIGKILL);child.wait(timeout=5)
 time.sleep(.3);guard.close();stop.set();reader.join();wd.terminate();wd.wait(timeout=5)
 with open('/dev/watchdog','wb',buffering=0) as f:f.write(b'V')
 state['fault']=guard.fault;(p/'state.json').write_text(json.dumps(state,indent=2)+'\n')
 print(state,flush=True)
