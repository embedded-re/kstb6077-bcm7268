"""Root monitor; actual classifier runs as kstb. Bound total test duration."""
import datetime,json,os,re,runpy,signal,subprocess,threading,time
from pathlib import Path
root=Path('/root/project-readiness')/datetime.datetime.now().strftime('%Y%m%d-%H%M%S')
root.mkdir(parents=True)
(root/'run.py').write_bytes(Path(__file__).read_bytes())
Path('/root/math-audit/readiness-path').write_text(str(root))
assert not subprocess.run(['pidof','watchdog'],capture_output=True).stdout.strip(),'existing watchdog'
state=dict(state='stopped',boot_id=Path('/proc/sys/kernel/random/boot_id').read_text().strip())
(root/'state.json').write_text(json.dumps(state,indent=2))
stop=threading.Event()
reader=threading.Thread(target=runpy.run_path('/opt/gpu-test/kstb-gpu-run')['kmsg_reader'],args=(str(root/'kernel.txt'),stop))
reader.start();time.sleep(.25)
guard=runpy.run_path('/opt/gpu-test/kstb_fault_guard.py')['KernelFaultGuard'](root/'kernel.txt').start()
wd=subprocess.Popen(['watchdog','-F','-T','30','-t','5','/dev/watchdog'],start_new_session=True)
p=None
try:
    cmd=['su','-l','kstb','-c','env OPENBLAS_NUM_THREADS=1 /home/kstb/projects/.venv/bin/python /home/kstb/projects/image-classifier/classify.py --self-test --threads 4 --soak-seconds 300 --report /home/kstb/projects/image-classifier/soak.json']
    state['command']=cmd
    with (root/'classifier.txt').open('w') as log:
        p=subprocess.Popen(cmd,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
        guard.attach(p)
        try:p.wait(timeout=390)
        finally:
            if p.poll() is None:os.killpg(p.pid,signal.SIGKILL);p.wait(timeout=5)
        state['returncode']=p.returncode
        time.sleep(.4);guard.check()
        assert p.returncode==0,p.returncode
        report=json.loads(Path('/home/kstb/projects/image-classifier/soak.json').read_text())
        assert report['result']=='PASS' and report['soak']['seconds']>=300
        (root/'soak.json').write_text(json.dumps(report,indent=2))
        text=(root/'kernel.txt').read_text()
        assert not re.search(r'Out of memory|Killed process|I/O error|EXT4-fs error',text),text
    state['state']='complete'
finally:
    if p is not None and p.poll() is None:os.killpg(p.pid,signal.SIGKILL);p.wait(timeout=5)
    state['fault']=guard.fault
    time.sleep(.3);guard.close();stop.set();reader.join()
    wd.terminate();wd.wait(timeout=5)
    with open('/dev/watchdog','wb',buffering=0) as f:f.write(b'V')
    (root/'state.json').write_text(json.dumps(state,indent=2))
    print(root,state['state'],flush=True)
