import datetime,json,os,runpy,subprocess,threading,time
from pathlib import Path
from types import SimpleNamespace
pid=10728
assert os.getpgid(pid)==pid and Path('/proc',str(pid),'comm').read_text().strip()=='supertux2'
O=Path('/root/gpu-runs')/(datetime.datetime.now().strftime('%Y%m%d-%H%M%S')+'-poc-gameplay');O.mkdir();(O/'run.py').write_text(Path(__file__).read_text());Path('/root/gpu-system-prep/gameplay-path').write_text(str(O))
stop=threading.Event();t=threading.Thread(target=runpy.run_path('/opt/gpu-test/kstb-gpu-run')['kmsg_reader'],args=(str(O/'kernel.txt'),stop));t.start();time.sleep(.2)
def poll():
 try:
  v=Path('/proc',str(pid),'stat').read_text()
  return 0 if ') Z ' in v else None
 except FileNotFoundError:return 0
g=runpy.run_path('/opt/gpu-test/kstb_fault_guard.py')['KernelFaultGuard'](O/'kernel.txt').start();g.attach(SimpleNamespace(pid=pid,poll=poll));state='stopped'
subprocess.run(['watchdog','-T','30','-t','5','/dev/watchdog'],check=True)
try:
 for i in range(60):
  g.check()
  if poll() is not None:break
  time.sleep(1)
  if i in [20,50]:
   image=Path('/tmp/xdg-1000/poc-gameplay.png');r=subprocess.run(['su','-l','kstb','-c','env XDG_RUNTIME_DIR=/tmp/xdg-1000 WAYLAND_DISPLAY=wayland-1 grim '+str(image)],capture_output=True);assert r.returncode==0;(O/('screen-'+str(i)+'.png')).write_bytes(image.read_bytes())
 g.check();state='complete'
finally:
 g.close();subprocess.run('killall watchdog; sleep 1; printf V > /dev/watchdog',shell=True,check=True);stop.set();t.join();(O/'state.json').write_text(json.dumps(dict(state=state,fault=g.fault,game_pid=pid,game_left_open=poll() is None),indent=2));print(O,state,flush=True)
