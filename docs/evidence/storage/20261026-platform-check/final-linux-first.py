from pathlib import Path
import json,subprocess,hashlib,os,socket,urllib.request,wave,math,struct,time
P=Path('/root/platform-check-final-20261026');P.mkdir(exist_ok=True)
state={'state':'running','passed':[]}
def cmd(name,args):
 r=subprocess.run(args,capture_output=True,text=True,timeout=30)
 (P/(name+'.txt')).write_text(r.stdout+r.stderr)
 assert r.returncode==0,(name,r.returncode,r.stderr)
 state['passed'].append(name);return r.stdout
try:
 p=Path('/root/.platform-check-persist.bin');b=p.read_bytes();assert len(b)==1048576 and hashlib.sha256(b).hexdigest()=='fbbab289f7f94b25736c58be46a994c441fd02552cc6022352e3d86d2fab7c83'
 p.unlink();fd=os.open('/root',os.O_DIRECTORY);os.fsync(fd);os.close(fd);state['passed'].append('ext4 fsync persistence across reboot')
 assert os.cpu_count()==4;state['passed'].append('four CPUs online')
 cmd('ethernet-ping',['ping','-c','3','-W','2','192.168.1.38'])
 addresses=socket.getaddrinfo('example.com',443);assert addresses;state['passed'].append('DNS')
 with urllib.request.urlopen('https://example.com',timeout=15) as response:
  assert response.status==200;response.read(1024);state['passed'].append('HTTPS / CA certificates')
 cmd('gpt',['sfdisk','--verify','/dev/mmcblk0'])
 cmd('fat',['/run/storage-fsck.fat','-vn','/dev/mmcblk0p9'])
 cmd('cpu-math',['/root/math-audit/kstb-cpu-math-test'])
 cmd('fences',['/root/gpu-system-prep/kstb-native-fence-test'])
 wav=Path('/tmp/platform-audio-20261026.wav');assert not wav.exists()
 with wave.open(str(wav),'wb') as w:
  w.setnchannels(2);w.setsampwidth(2);w.setframerate(48000)
  w.writeframes(b''.join(struct.pack('<hh',v,v) for v in (int(1500*math.sin(2*math.pi*440*i/48000)) for i in range(96000))))
 try:
  with (P/'audio.txt').open('w') as log:
   process=subprocess.Popen(['su','-l','kstb','-c','aplay -q -D hw:0,0 /tmp/platform-audio-20261026.wav'],stdout=log,stderr=subprocess.STDOUT)
   try:
    time.sleep(.5);status=Path('/proc/asound/card0/pcm0p/sub0/status').read_text();(P/'audio-running.txt').write_text(status)
    assert 'RUNNING' in status,status;process.wait(timeout=5);assert process.returncode==0
   finally:
    if process.poll() is None:process.terminate();process.wait(timeout=3)
  state['passed'].append('normal-user ALSA playback / RUNNING DMA state (audibility unconfirmed)')
 finally:wav.unlink()
 snapshot={}
 for name,fp in [('cpu','/proc/cpuinfo'),('memory','/proc/meminfo'),('network','/proc/net/dev'),('input','/proc/bus/input/devices'),('audio','/proc/asound/cards'),('drm','/sys/kernel/debug/dri/1/state'),('dmabuf','/sys/kernel/debug/dma_buf/bufinfo'),('temp','/sys/class/thermal/thermal_zone0/temp')]:
  snapshot[name]=Path(fp).read_text()
 snapshot['usb']=[{'vendor':(d/'idVendor').read_text().strip(),'product':(d/'idProduct').read_text().strip(),'name':(d/'product').read_text().strip() if (d/'product').exists() else ''} for d in Path('/sys/bus/usb/devices').iterdir() if (d/'idVendor').exists()]
 assert any(d['vendor']=='0461' and d['product']=='0010' for d in snapshot['usb'])
 assert 'Total 0 objects, 0 bytes' in snapshot['dmabuf']
 assert '[fbcon]' in snapshot['drm']
 snapshot['kernel']=cmd('kernel',['uname','-a']);snapshot['df']=cmd('space',['df','-k','/'])
 snapshot['dri']=cmd('dri',['sha256sum','/usr/lib/dri/v3d_dri.so'])
 assert 'ab8e3f40c4d368bc46661342bde624de3051a249aeed1b02f220e39b6e8bcb8a' in snapshot['dri']
 snapshot['processes']=cmd('processes',['ps','-o','pid,comm,args'])
 assert not subprocess.run(['pidof','sway','firefox','watchdog','kstb-fft-test'],capture_output=True).stdout.strip()
 (P/'snapshot.json').write_text(json.dumps(snapshot,indent=2)+'\n')
 state['state']='complete'
except Exception as exc:
 state['state']='failed';state['error']=repr(exc)
finally:
 (P/'state.json').write_text(json.dumps(state,indent=2)+'\n');print(state,flush=True)
