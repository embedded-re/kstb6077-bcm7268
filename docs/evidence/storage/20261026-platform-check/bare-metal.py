from pathlib import Path
import json,runpy,socket,subprocess,time,zlib,re
P=Path(__file__).resolve().parent
read_until=runpy.run_path('tools/kstb-linux')['read_until']
s=socket.socket(socket.AF_UNIX,socket.SOCK_STREAM);s.connect('/tmp/kstb.sock');s.settimeout(.05)
log=(P/'bare-metal-console.txt').open('wb',buffering=0);results=[]
def cmd(line,timeout=25):
 s.sendall(line.encode()+b'\r');out,_=read_until(s,rb'kmon> ',timeout);log.write(out)
 (P/('mon-'+re.sub('[^a-zA-Z0-9]','_',line)+'.txt')).write_bytes(out)
 return out
def passed(name):results.append(name);print('PASS',name,flush=True)
state={'state':'running','passed':results}
p=subprocess.Popen(['ssh','root@192.168.1.33','reboot'],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
out,_=read_until(s,rb'kmon> ',100);log.write(out);p.communicate(timeout=5)
try:
 out=cmd('cpu');assert b"kboot's (4b423303)" in out;passed('normal EL3 boot / PSCI monitor')
 cmd('info');cmd('mmc');cmd('mmc part')
 out=cmd('fs');assert b'#14714880-#15239167' in out and b'of 2 KB' in out;passed('new DATA geometry')
 out=cmd('ls');assert b'pcheck.bin' not in out
 inventory=json.loads(Path('linux/build/storage-migrate/20261026/storage-cleanup.json').read_text())['original_files']
 source=Path('linux/build/storage-inspect/20261026/data-files')
 for e in inventory:
  expected=zlib.crc32((source/e['name']).read_bytes())
  out=cmd('fget '+e['name']+' 0x02000000')
  assert f"{e['bytes']} bytes".encode() in out and f'CRC {expected:08x}'.encode() in out,e['name']
 passed('all eight original FAT files read with matching CRC')
 cmd('fill 0x02000000 0x100000 0xa5');out=cmd('crc 0x02000000 0x100000')
 crc=zlib.crc32(bytes([0xa5])*1048576);assert f'{crc:08x}'.encode() in out
 cmd('cp 0x02100000 0x02000000 0x100000');out=cmd('cmp 0x02000000 0x02100000 0x100000');assert b'differ' not in out.lower() or b'no differ' in out.lower(),out
 passed('1MiB RAM fill / copy / CRC')
 out=cmd('fput pcheck.bin 0x02000000 0x100000');assert f'CRC {crc:08x}'.encode() in out
 out=cmd('fget pcheck.bin 0x02200000');assert f'CRC {crc:08x}'.encode() in out
 cmd('rm pcheck.bin');assert b'pcheck.bin' not in cmd('ls');passed('1MiB FAT write / read / delete')
 cmd('irq on');cmd('irq timer 10');time.sleep(1.1);out=cmd('irq');cmd('irq timer off');cmd('irq off')
 ticks=re.search(rb'timer CNTHP_CTL .*ticks (\d+)',out);assert ticks and int(ticks[1])>=50,out;passed('GIC / EL2 timer interrupts')
 cmd('net dhcp');out=cmd('ping 192.168.1.38 3');assert b'timeout' not in out.lower() and (b'reply' in out.lower() or b'3 received' in out.lower()),out;passed('bare-metal DHCP / Ethernet ping')
 cmd('video');cmd('audio init');cmd('beep 440 200');cmd('audio stop');cmd('audio');passed('HDMI/audio commands completed (physical output unconfirmed)')
 cmd('usb init');cmd('usb');passed('USB enumeration commands completed')
 cmd('linux info');passed('installed KRN / DTB headers readable')
 state['state']='complete'
except Exception as exc:
 state['state']='failed';state['error']=repr(exc);print('FAILED',repr(exc),flush=True)
finally:
 # Return to installed Linux even if a regression is found.
 s.sendall(b'linux\r');out,_=read_until(s,rb'Run /sbin/init|Run /init|Welcome to Alpine|OpenRC',100);log.write(out)
 state['linux_started']=True;(P/'bare-metal-state.json').write_text(json.dumps(state,indent=2)+'\n')
 s.close();log.close();print(state,flush=True)
