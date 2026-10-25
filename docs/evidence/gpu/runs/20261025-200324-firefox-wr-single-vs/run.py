"""Isolate CPU rendering, WebRender and WebGL in fresh Firefox test profiles."""
import datetime,json,os,re,runpy,shlex,signal,socket,subprocess,sys,threading,time
from pathlib import Path
from marionette import Marionette
mode=sys.argv[1];assert mode in ('cpu','wr','webgl','wr-single','wr-single-vs','wr-single-fs','wr-single-bin')
O=Path('/root/gpu-runs')/(datetime.datetime.now().strftime('%Y%m%d-%H%M%S')+'-firefox-'+mode);O.mkdir();(O/'run.py').write_text(Path(__file__).read_text());Path('/root/gpu-system-prep/browser-path').write_text(str(O))
assert not subprocess.run(['pidof','firefox'],capture_output=True).stdout.strip()
S=subprocess.check_output(['pidof','sway'],text=True).split();assert len(S)==1
R=Path('/tmp/xdg-1000');ipc_socket=next(R.glob('sway-ipc.*.sock'));display=next(p.name for p in R.glob('wayland-*') if not p.name.endswith('.lock'))
profile=Path('/home/kstb/.mozilla/firefox')/('isolate-'+mode+'-'+O.name.split('-')[1]);profile.mkdir();profile.chmod(0o700);os.chown(profile,1000,1000)
prefs={'browser.shell.checkDefaultBrowser':False,'browser.startup.homepage_override.mstone':'ignore','browser.aboutwelcome.enabled':False,'marionette.port':2828,'remote.prefs.recommended':False,'webgl.disabled':mode!='webgl'}
if mode in ('cpu','webgl'):prefs.update({'layers.acceleration.disabled':True,'gfx.webrender.software':True})
(profile/'user.js').write_text(''.join('user_pref('+json.dumps(k)+', '+json.dumps(v)+');\n' for k,v in prefs.items()));os.chown(profile/'user.js',1000,1000);(O/'prefs.json').write_text(json.dumps(prefs,indent=2))
page=R/'browser-baseline.html';page.write_text('<!doctype html><meta charset="utf-8"><title>Firefox browser check</title><body style="background:#1e1e2e;color:#cdd6f4;font:24px sans-serif;padding:60px"><h1>Firefox on your desktop</h1><p>A normal Wayland browser window.</p><button id="button" onclick="this.textContent=\'Button works\'">Try this button</button><p><a href="https://www.wikipedia.org/">Browse Wikipedia</a></p><p id="clock"></p><script>setInterval(()=>document.querySelector("#clock").textContent=new Date().toLocaleTimeString(),1000)</script>');os.chown(page,1000,1000)
stop=threading.Event();reader=runpy.run_path('/opt/gpu-test/kstb-gpu-run')['kmsg_reader'];thread=threading.Thread(target=reader,args=(str(O/'kernel.txt'),stop));thread.start();time.sleep(.2)
guard=runpy.run_path('/opt/gpu-test/kstb_fault_guard.py')['KernelFaultGuard'](O/'kernel.txt').start()
def check():guard.check()
def pause(t):
 end=time.monotonic()+t
 while time.monotonic()<end:check();assert proc.poll() is None;time.sleep(.1)
def user(args):return subprocess.run(['su','-l','kstb','-c',shlex.join(['env','XDG_RUNTIME_DIR='+str(R),'WAYLAND_DISPLAY='+display]+args)],capture_output=True,text=True,timeout=15)
def ipc(s):
 r=user(['swaymsg','-s',str(ipc_socket),s]);assert r.returncode==0,r.stderr
capture=profile/'shaders';capture.mkdir();os.chown(capture,1000,1000)
state='stopped';proc=None;connection=None
subprocess.run(['watchdog','-T','30','-t','5','/dev/watchdog'],check=True)
try:
 with (O/'firefox.txt').open('w') as log:
  env=['env','XDG_RUNTIME_DIR='+str(R),'WAYLAND_DISPLAY='+display,'MESA_SHADER_CACHE_DISABLE=true']
  if mode!='cpu':env+=['V3D_DEBUG=shaderdb','MESA_SHADER_CAPTURE_PATH='+str(capture)]
  if mode.startswith('wr-single'):env+=['LIBGL_DRIVERS_PATH=/var/tmp/kstb-firefox-single/dri','V3D33_SINGLE_THREAD='+('all' if mode=='wr-single' else mode.rsplit('-',1)[1])]
  args=env+['firefox','--no-remote','--profile',str(profile),'--marionette','--remote-allow-system-access','file://'+str(page)]
  proc=subprocess.Popen(['su','-l','kstb','-c',shlex.join(args)],stdout=log,stderr=subprocess.STDOUT,start_new_session=True);guard.attach(proc)
  start=time.monotonic()
  while time.monotonic()-start<60:
   check();assert proc.poll() is None
   try:connection=socket.create_connection(('127.0.0.1',2828),timeout=.5);break
   except OSError:time.sleep(.1)
  assert connection;connection.settimeout(25);m=Marionette(connection)
  session=m.call('WebDriver:NewSession',{'capabilities':{'alwaysMatch':{}}});(O/'session.json').write_text(json.dumps(session,indent=2));main=session['capabilities']['moz:processID'];print(mode,'session ready',main,flush=True)
  ipc('[pid='+str(main)+'] move container to workspace "Browser test"');ipc('workspace "Browser test"');pause(3)
  m.call('Marionette:SetContext',{'value':'chrome'})
  graphics=m.call('WebDriver:ExecuteAsyncScript',{'script':'const done=arguments[arguments.length-1];ChromeUtils.importESModule("resource://gre/modules/Troubleshoot.sys.mjs").Troubleshoot.snapshot().then(s=>done({graphics:s.graphics,application:s.application})).catch(e=>done({error:String(e)}));','args':[],'newSandbox':True,'sandbox':'default','scriptTimeout':20000});(O/'graphics.json').write_text(json.dumps(graphics,indent=2));print(mode,'graphics captured',flush=True)
  m.call('Marionette:SetContext',{'value':'content'})
  m.call('WebDriver:Navigate',{'url':'file://'+str(page)});pause(.2)
  interaction=m.script('document.getElementById("button").click();return {title:document.title,button:document.getElementById("button").textContent};');(O/'interaction.json').write_text(json.dumps(interaction,indent=2));assert interaction['button']=='Button works'
  if mode=='webgl':
   result=m.script('const c=document.createElement("canvas");c.width=c.height=16;document.body.append(c);const g=c.getContext("webgl",{antialias:false});if(!g)return {available:false};g.clearColor(1,0,0,1);g.clear(g.COLOR_BUFFER_BIT);const p=new Uint8Array(4);g.readPixels(0,0,1,1,g.RGBA,g.UNSIGNED_BYTE,p);const x=g.getExtension("WEBGL_debug_renderer_info");return {available:true,pixel:Array.from(p),renderer:x?g.getParameter(x.UNMASKED_RENDERER_WEBGL):g.getParameter(g.RENDERER)};');(O/'webgl.json').write_text(json.dumps(result,indent=2));print('WebGL',result,flush=True);pause(3)
  m.call('WebDriver:Navigate',{'url':'https://www.wikipedia.org/'});pause(3)
  result=m.script('return {title:document.title,url:location.href,ready:document.readyState};');(O/'internet.json').write_text(json.dumps(result,indent=2));print(mode,'internet',result,flush=True)
  r=user(['grim',str(R/'browser-isolation.png')]);assert r.returncode==0;(O/'browser.png').write_bytes((R/'browser-isolation.png').read_bytes())
  for p in Path('/proc').glob('[0-9]*'):
   try:
    maps=(p/'maps').read_text()
    if '/usr/lib/firefox/' in maps and '/dri/' in maps:(O/('maps-'+p.name+'.txt')).write_text(maps)
   except (FileNotFoundError,PermissionError,ProcessLookupError):pass
  m.call('Marionette:Quit',{'flags':['eAttemptQuit']});proc.wait(timeout=10);check();state='complete'
except Exception as e:
 (O/'error.txt').write_text(repr(e)+'\n');raise
finally:
 if proc and proc.poll() is None:os.killpg(proc.pid,signal.SIGKILL);proc.wait()
 if connection:connection.close()
 import shutil
 shutil.copytree(capture,O/'shaders')
 guard.close();subprocess.run('killall watchdog; sleep 1; printf V > /dev/watchdog',shell=True,check=True);stop.set();thread.join()
 (O/'state.json').write_text(json.dumps(dict(state=state,mode=mode,fault=guard.fault),indent=2));print(O,state,flush=True)
