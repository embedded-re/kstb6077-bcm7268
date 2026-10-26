"""Bounded, sequential post-storage platform checks; leaves no running tests."""
import json,os,runpy,signal,subprocess,threading,time,hashlib
from pathlib import Path
P=Path('/root/platform-check-20261026');P.mkdir(exist_ok=True)
assert not subprocess.run(['pidof','sway','firefox','watchdog'],capture_output=True).stdout.strip(),'active application/watchdog'
stop=threading.Event();reader=threading.Thread(target=runpy.run_path('/opt/gpu-test/kstb-gpu-run')['kmsg_reader'],args=(str(P/'kernel.txt'),stop));reader.start();time.sleep(.2)
guard=runpy.run_path('/opt/gpu-test/kstb_fault_guard.py')['KernelFaultGuard'](P/'kernel.txt').start()
wd=subprocess.Popen(['watchdog','-F','-T','30','-t','5','/dev/watchdog'],start_new_session=True)
state={'state':'running','passed':[]};child=None
def test(name,cmd,expected,env=None,timeout=45):
 global child
 print('START',name,flush=True)
 with (P/(name+'.txt')).open('w') as f:
  child=subprocess.Popen(cmd,stdout=f,stderr=subprocess.STDOUT,env=env,start_new_session=True);guard.attach(child)
  try:child.wait(timeout=timeout)
  finally:
   if child.poll() is None:os.killpg(child.pid,signal.SIGKILL);child.wait(timeout=5)
 time.sleep(.2);guard.check();text=(P/(name+'.txt')).read_text()
 assert child.returncode==0,(name,child.returncode,text[-1500:]);assert expected in text,(name,text[-1500:])
 state['passed'].append(name);print('PASS',name,flush=True)
 (P/'state.json').write_text(json.dumps(state,indent=2)+'\n')
try:
 test('root-io',['python3','-c',"import os,tempfile,hashlib;f=tempfile.NamedTemporaryFile(prefix='.platform-check-',dir='/root');b=bytes(range(256))*65536;f.write(b);f.flush();os.fsync(f.fileno());f.seek(0);assert hashlib.sha256(f.read()).digest()==hashlib.sha256(b).digest();f.close();print('PASS: 16MiB ext4 fsync/read/hash/delete')"],'PASS:')
 test('cpu-math',['/root/math-audit/kstb-cpu-math-test'],'RESULT=PASS')
 test('python-precision',['python3','/root/math-audit/python-check.py'],'"independent_square_multiply_match": true')
 test('numpy',['su','-l','kstb','-c',"env OPENBLAS_NUM_THREADS=4 /home/kstb/projects/.venv/bin/python -c 'import numpy as n;g=n.random.default_rng(6077);a=g.normal(size=(128,128));a=a@a.T+n.eye(128);b=g.normal(size=128);x=n.linalg.solve(a,b);assert n.max(n.abs(a@x-b))<1e-10;y=g.normal(size=65536);assert n.max(n.abs(n.fft.ifft(n.fft.fft(y)).real-y))<1e-10;print(\"PASS: NumPy FP64 linear solve and FFT\")'"],'PASS:')
 test('classifier',['su','-l','kstb','-c','env OPENBLAS_NUM_THREADS=1 /home/kstb/projects/.venv/bin/python /home/kstb/projects/image-classifier/classify.py --self-test --threads 4 --soak-seconds 30 --report /home/kstb/projects/image-classifier/post-platform.json'],'"result": "PASS"',timeout=65)
 test('gles1',['/root/gpu-system-prep/kstb-gles1-smoke'],'')
 test('caps',['/root/math-audit/kstb-gl-caps','3'],'V3D 3.3')
 test('graphics',['/root/gpu-system-prep/kstb-msaa-depth-stencil-test'],'0 pixel checks failed')
 test('fences',['/root/gpu-system-prep/kstb-native-fence-test'],'100 native fence')
 env=dict(os.environ,V3D_COMPUTE33='1',MESA_SHADER_CACHE_DISABLE='true')
 test('compute-limits',['/root/math-audit/kstb-math-shader-check'],'RESULT=PASS checks=11 dispatches=0',env)
 test('compute-fft',['/root/math-audit/kstb-fft-test','1024','16','5'],'PASS',env)
 state['state']='complete'
except Exception as exc:
 state['state']='failed';state['error']=repr(exc);print('FAILED',repr(exc),flush=True)
finally:
 if child is not None and child.poll() is None:os.killpg(child.pid,signal.SIGKILL);child.wait(timeout=5)
 time.sleep(.2);guard.close();stop.set();reader.join();wd.terminate();wd.wait(timeout=5)
 with open('/dev/watchdog','wb',buffering=0) as f:f.write(b'V')
 state['fault']=guard.fault;(P/'state.json').write_text(json.dumps(state,indent=2)+'\n');print(state,flush=True)
