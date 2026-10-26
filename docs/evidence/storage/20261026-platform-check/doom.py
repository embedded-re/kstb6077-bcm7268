from pathlib import Path
import socket,subprocess,time,json,runpy
P=Path(__file__).resolve().parent;read_until=runpy.run_path('tools/kstb-linux')['read_until']
s=socket.socket(socket.AF_UNIX,socket.SOCK_STREAM);s.connect('/tmp/kstb.sock');s.settimeout(.05)
f=(P/'doom-console.txt').open('wb',buffering=0)
def cmd(c,t=20):
 s.sendall(c.encode()+b'\r');out,_=read_until(s,rb'kmon> ',t);f.write(out);return out
state={'state':'running'}
p=subprocess.Popen(['ssh','root@192.168.1.33','reboot'],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
out,_=read_until(s,rb'kmon> ',100);f.write(out);p.communicate(timeout=5)
assert b"kboot's (4b423303)" in cmd('cpu')
cmd('wdt 90');s.sendall(b'doom\r')
end=time.monotonic()+8;buf=b''
while time.monotonic()<end:
 try:
  out=s.recv(4096);buf+=out;f.write(out)
 except socket.timeout:pass
state['startup_log']=buf.decode('latin1')
if b'kmon> ' in buf:
 state['state']='failed';state['error']='Doom returned before interactive test'
else:
 # Escape opens menu; q selects Quit; Enter activates it; y confirms. Hold each serial key separately.
 for key in (b'\x1b',b'q',b'\r',b'y'):
  s.sendall(key);time.sleep(.4)
 try:
  out,_=read_until(s,rb'kmon> ',20);f.write(out);state['state']='complete';state['quit_returned_to_monitor']=True
 except SystemExit as exc:
  state['state']='failed';state['error']=str(exc)
  # The armed watchdog returns a stuck program to the normal monitor.
  out,_=read_until(s,rb'kmon> ',100);f.write(out);state['watchdog_recovery']=True
cmd('wdt off');cmd('video');s.sendall(b'linux\r');out,_=read_until(s,rb'Run /sbin/init|Run /init|Welcome to Alpine|OpenRC',100);f.write(out)
state['linux_started']=True;(P/'doom-state.json').write_text(json.dumps(state,indent=2)+'\n');f.close();s.close();print(state,flush=True)
