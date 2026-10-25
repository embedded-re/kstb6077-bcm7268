"""Bounded Firefox Wayland demonstration in the user's existing Sway session."""
import datetime,json,os,re,shlex,signal,socket,subprocess,threading,time,runpy,sys,hashlib
from pathlib import Path
mode=sys.argv[1];assert mode in ('candidate','native')
O=Path('/root/gpu-runs')/(datetime.datetime.now().strftime('%Y%m%d-%H%M%S')+'-firefox-demo-'+mode);O.mkdir()
(O/'run.py').write_text(Path(__file__).read_text());Path('/root/gpu-system-prep/browser-path').write_text(str(O))
assert not subprocess.run(['pidof','firefox'],capture_output=True).stdout.strip(),'existing Firefox: leave it alone'
sway=subprocess.check_output(['pidof','sway'],text=True).split();assert len(sway)==1
R=Path('/tmp/xdg-1000');sock=next(R.glob('sway-ipc.*.sock'));display=next(p.name for p in R.glob('wayland-*') if not p.name.endswith('.lock'))
profile=Path('/home/kstb/.mozilla/firefox')/('demo-'+mode+'-'+O.name.split('-')[1]);profile.mkdir(parents=True,exist_ok=True);os.chown(profile,1000,1000);profile.chmod(0o700)
(profile/'user.js').write_text('user_pref("browser.shell.checkDefaultBrowser", false);\nuser_pref("browser.startup.homepage_override.mstone", "ignore");\nuser_pref("browser.aboutwelcome.enabled", false);\nuser_pref("marionette.port", 2828);\nuser_pref("remote.prefs.recommended", false);\n')
os.chown(profile/'user.js',1000,1000)
stop=threading.Event();thread=threading.Thread(target=runpy.run_path('/opt/gpu-test/kstb-gpu-run')['kmsg_reader'],args=(str(O/'kernel.txt'),stop));thread.start();time.sleep(.2)
def check():guard.check()
def user(args):return subprocess.run(['su','-l','kstb','-c',shlex.join(['env','XDG_RUNTIME_DIR='+str(R),'WAYLAND_DISPLAY='+display]+args)],capture_output=True,text=True,timeout=15)
def ipc(cmd):
 r=user(['swaymsg','-s',str(sock),cmd]);assert r.returncode==0,r.stderr;return r.stdout
def pause(t):
 end=time.monotonic()+t
 while time.monotonic()<end:check();assert proc.poll() is None,'Firefox exited';time.sleep(.1)
class Marionette:
 def __init__(self,s):self.s=s;self.buf=b'';self.serial=0;self.greeting=self.receive()
 def receive(self):
  while b':' not in self.buf:
   block=self.s.recv(65536)
   if not block:raise RuntimeError('Marionette closed')
   self.buf+=block
  prefix,body=self.buf.split(b':',1);length=int(prefix)
  while len(body)<length:
   block=self.s.recv(65536)
   if not block:raise RuntimeError('Marionette closed')
   body+=block
  self.buf=body[length:];return json.loads(body[:length])
 def call(self,name,args=None):
  self.serial+=1;msg=json.dumps([0,self.serial,name,args or {}]).encode();self.s.sendall(str(len(msg)).encode()+b':'+msg)
  response=self.receive();assert response[0]==1 and response[1]==self.serial,response
  if response[2]:raise RuntimeError(json.dumps(response[2]))
  value=response[3];return value.get('value',value) if isinstance(value,dict) else value
 def script(self,source):return self.call('WebDriver:ExecuteScript',dict(script=source,args=[],newSandbox=True,sandbox='default'))
state='stopped';proc=None;connection=None
guard=runpy.run_path('/opt/gpu-test/kstb_fault_guard.py')['KernelFaultGuard'](O/'kernel.txt').start()
subprocess.run(['watchdog','-T','30','-t','5','/dev/watchdog'],check=True)
try:
 with (O/'firefox.txt').open('w') as log:
  args=['firefox','--no-remote','--profile',str(profile),'--marionette','--remote-allow-system-access','file:///usr/share/kstb/browser-demo/index.html']
  env=['env','XDG_RUNTIME_DIR='+str(R),'WAYLAND_DISPLAY='+display]
  if mode=='candidate':env+=['LIBGL_DRIVERS_PATH=/var/tmp/kstb-firefox-fixed/dri']
  (O/'launch.json').write_text(json.dumps(env+args,indent=2))
  proc=subprocess.Popen(['su','-l','kstb','-c',shlex.join(env+args)],stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
  guard.attach(proc)
  start=time.monotonic()
  while time.monotonic()-start<75:
   check();assert proc.poll() is None,'Firefox startup failed'
   try:connection=socket.create_connection(('127.0.0.1',2828),timeout=1);break
   except OSError:time.sleep(.2)
  assert connection,'no automation connection';connection.settimeout(30);m=Marionette(connection)
  (O/'greeting.json').write_text(json.dumps(m.greeting,indent=2))
  session=m.call('WebDriver:NewSession',{'capabilities':{'alwaysMatch':{}}});(O/'session.json').write_text(json.dumps(session,indent=2))
  main=session['capabilities']['moz:processID'];print('Firefox started',main,flush=True)
  ipc('[pid='+str(main)+'] move container to workspace "Browser demo"');ipc('workspace "Browser demo"')
  m.call('WebDriver:Navigate',{'url':'file:///usr/share/kstb/browser-demo/index.html'});pause(12)
  demo=m.script('return JSON.parse(JSON.stringify(window.wrappedJSObject.demoState));');(O/'demo-state.json').write_text(json.dumps(demo,indent=2));print('DEMO',demo,flush=True);assert demo['status']=='running' and demo['checks']>0 and demo['failures']==0 and demo['frames']>30
  # Test interaction inside this browser only, without injecting global input.
  m.script('document.getElementById("pause").click();return true;');pause(.5)
  assert m.script('return window.wrappedJSObject.demoState.paused;') is True
  m.script('document.getElementById("pause").click();document.getElementById("colour").click();return true;');pause(2)
  image=R/'firefox-demo.png';r=user(['grim',str(image)]);assert r.returncode==0,r.stderr;(O/'demo.png').write_bytes(image.read_bytes())
  (O/'drm-state.txt').write_text(Path('/sys/kernel/debug/dri/1/state').read_text())
  m.call('Marionette:SetContext',{'value':'chrome'})
  graphics=m.call('WebDriver:ExecuteAsyncScript',{'script':'const done=arguments[arguments.length-1]; ChromeUtils.importESModule("resource://gre/modules/Troubleshoot.sys.mjs").Troubleshoot.snapshot().then(s=>done({graphics:s.graphics,application:s.application})).catch(e=>done({error:String(e)}));','args':[],'newSandbox':True,'sandbox':'default','scriptTimeout':30000})
  (O/'graphics.json').write_text(json.dumps(graphics,indent=2));print('Graphics report captured',flush=True);assert graphics['graphics']['windowLayerManagerType']=='WebRender',graphics['graphics']['windowLayerManagerType']
  m.call('Marionette:SetContext',{'value':'content'})
  m.call('WebDriver:Navigate',{'url':'https://www.wikipedia.org/'});pause(4)
  internet=m.script('return {title:document.title,url:location.href,ready:document.readyState,text:document.body.innerText.slice(0,300)};');(O/'internet.json').write_text(json.dumps(internet,indent=2));print('INTERNET',internet,flush=True)
  m.call('WebDriver:Navigate',{'url':'file:///usr/share/kstb/browser-demo/index.html'});pause(4)
  samples=[]
  for i in range(3 if mode=='native' else 1):
   m.call('WebDriver:Refresh');pause(12)
   before=m.script('return JSON.parse(JSON.stringify(window.wrappedJSObject.demoState));')
   assert before['status']=='running' and before['checks']>0 and before['failures']==0,before
   m.script('window.scrollTo(0,document.body.scrollHeight);return true;');pause(1)
   m.script('window.scrollTo(0,0);return true;');pause(2)
   after=m.script('return JSON.parse(JSON.stringify(window.wrappedJSObject.demoState));')
   assert after['frames']>before['frames'] and after['failures']==0,after
   samples.append(dict(before=before,after=after));print('reload',i,after,flush=True)
  (O/'samples.json').write_text(json.dumps(samples,indent=2))
  (O/'driver.sha256').write_text(hashlib.sha256(Path('/var/tmp/kstb-firefox-fixed/dri/v3d_dri.so' if mode=='candidate' else '/usr/lib/dri/v3d_dri.so').read_bytes()).hexdigest()+'\n')
  for p in Path('/proc').glob('[0-9]*'):

   try:
    maps=(p/'maps').read_text()
    if '/usr/lib/firefox/' in maps and '/dri/' in maps:
     (O/('maps-'+p.name+'.txt')).write_text(maps)
     for f in (p/'fdinfo').glob('*'):
      text=f.read_text()
      if 'drm-driver:' in text:(O/('fdinfo-'+p.name+'-'+f.name+'.txt')).write_text(text)
   except (FileNotFoundError,PermissionError,ProcessLookupError):pass
  # Close only our automation-profile browser; normal launch follows separately.
  m.call('Marionette:Quit',{'flags':['eAttemptQuit']});proc.wait(timeout=15)
  check();assert Path('/proc/'+sway[0]).exists();state='complete'
finally:
 if proc and proc.poll() is None:
  os.killpg(proc.pid,signal.SIGTERM)
  try:proc.wait(timeout=10)
  except subprocess.TimeoutExpired:os.killpg(proc.pid,signal.SIGKILL);proc.wait()
 if connection:connection.close()
 subprocess.run('killall watchdog; sleep 1; printf V > /dev/watchdog',shell=True,check=True);guard.close();stop.set();thread.join()
 (O/'state.json').write_text(json.dumps(dict(state=state,mode=mode,fault=guard.fault,original_sway_pid=sway[0]),indent=2));print(O,state,flush=True)
