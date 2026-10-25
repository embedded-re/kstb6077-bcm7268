"""Preview only our launcher in the user's existing desktop; never exit Sway."""
import datetime,json,os,re,runpy,subprocess,threading,time
from pathlib import Path
out=Path('/root/gpu-runs')/(datetime.datetime.now().strftime('%Y%m%d-%H%M%S')+'-launcher-final');out.mkdir()
(out/'run.py').write_text(Path(__file__).read_text());Path('/root/gpu-system-prep/launcher-path').write_text(str(out))
sway=subprocess.check_output(['pidof','sway'],text=True).split();assert len(sway)==1
assert not subprocess.run(['pidof','watchdog'],capture_output=True).stdout.strip()
r=Path('/tmp/xdg-1000');display=next(p.name for p in r.glob('wayland-*') if not p.name.endswith('.lock'))
stop=threading.Event();reader=runpy.run_path('/opt/gpu-test/kstb-gpu-run')['kmsg_reader'];thread=threading.Thread(target=reader,args=(str(out/'kernel.txt'),stop));thread.start();time.sleep(.2)
def check():assert not re.search(r'Resetting GPU|MMU error|Oops|BUG:|Unable to handle|Internal error|kmsg overrun',(out/'kernel.txt').read_text())
subprocess.run(['watchdog','-T','30','-t','5','/dev/watchdog'],check=True);proc=None;state='stopped'
try:
 sock=next(r.glob('sway-ipc.*.sock'))
 reload=subprocess.run(['su','-l','kstb','-c','XDG_RUNTIME_DIR=/tmp/xdg-1000 swaymsg -s '+str(sock)+' reload'],capture_output=True,text=True);assert reload.returncode==0,reload.stderr
 (out/'reload.txt').write_text(reload.stdout+reload.stderr)
 with (out/'launcher.txt').open('w') as log:
  proc=subprocess.Popen(['su','-l','kstb','-c','export XDG_RUNTIME_DIR=/tmp/xdg-1000 WAYLAND_DISPLAY='+display+'; exec wofi --show drun'],stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
  for i in range(40):
   check();assert proc.poll() is None,(out/'launcher.txt').read_text();time.sleep(.1)
  subprocess.run(['su','-l','kstb','-c','XDG_RUNTIME_DIR=/tmp/xdg-1000 WAYLAND_DISPLAY='+display+' grim /tmp/xdg-1000/launcher-preview.png'],check=True,timeout=10)
  # Keep the full screen only in /tmp; archive the launcher crop on the host.
  for i in range(20):check();time.sleep(.1)
  proc.terminate();proc.wait(timeout=5)
 check();assert Path('/proc/'+sway[0]).exists(),'existing desktop disappeared'
 state='complete'
finally:
 if proc and proc.poll() is None:proc.terminate();proc.wait(timeout=5)
 subprocess.run('killall watchdog; sleep 1; printf V > /dev/watchdog',shell=True,check=True);stop.set();thread.join()
 (out/'state.json').write_text(json.dumps(dict(state=state,existing_sway_pid=sway[0]),indent=2));print(out,state,flush=True)
