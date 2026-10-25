from pathlib import Path
import hashlib,json,tarfile,subprocess
root=Path('/root/gpu-kernel-ab/hrtimer')
for label,name in [('KRN','krn.img'),('DTB','dtb.img')]:
 devices=[]
 for p in Path('/sys/class/block').glob('mmcblk0p*/uevent'):
  if 'PARTNAME='+label in p.read_text().splitlines():devices.append(Path('/dev')/p.parent.name)
 assert len(devices)==1
 data=(root/name).read_bytes()
 with devices[0].open('rb') as f:assert f.read(len(data))==data
 print('PASS:',label,str(devices[0]),'matches installed candidate',hashlib.sha256(data).hexdigest())
backup=Path('/root/gpu-backups/kernel29-before-hrtimer-ab')
meta=json.loads((backup/'manifest.json').read_text())
for entry in meta['images'].values():assert hashlib.sha256(Path(entry['file']).read_bytes()).hexdigest()==entry['sha256']
assert hashlib.sha256((backup/'modules.tar').read_bytes()).hexdigest()==meta['modules_sha256']
files=0
with tarfile.open(backup/'modules.tar') as t:
 for item in t:
  if item.isfile():assert t.extractfile(item).read()==(Path('/lib/modules')/item.name).read_bytes();files+=1
print('PASS: rollback image/module backups intact;',files,'original module files unchanged')
assert Path('/opt/mesa/current').readlink()==Path('23.3.6-fe7dda5e')
print(subprocess.check_output(['uname','-a'],text=True).strip())
names={'deqp-gles31','deqp-gles3','deqp-gles2','deqp-egl','kstb-gpu-run','kstb-neural-test','kstb-compute-sync-test','kstb-compute-latency-test','watchdog'}
for p in Path('/proc').glob('[0-9]*/cmdline'):
 try: args={Path(x.decode()).name for x in p.read_bytes().split(b'\0') if x}
 except (OSError,UnicodeError):continue
 assert not names.intersection(args),(p,args)
print('PASS: no GPU validation processes or userspace watchdog running; Mesa selection unchanged')
