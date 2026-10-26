"""Guarded, fixed-layout DATA relocation / ext4 growth from the RAM rescue."""
import fcntl,gzip,hashlib,json,os,struct,subprocess,time,zlib
from pathlib import Path
RUN=Path('/run/storage-migrate');RUN.mkdir(exist_ok=True)
DISK='/dev/mmcblk0';SECTOR=512;NEW_DATA=14714880;DATA_BYTES=268435456
journal=[]
def checkpoint(stage,**values):
 journal.append(dict(stage=stage,time=time.time(),**values));(RUN/'journal.json').write_text(json.dumps(journal,indent=2)+'\n');print(stage,values,flush=True)
def root_unmounted():
 s=Path('/proc/mounts').read_text();assert 'mmcblk0p8' not in s and 'mmcblk0p9' not in s and '/dev/root ' not in s
 pid=int(Path('/run/rescue-watchdog.pid').read_text());os.kill(pid,0)
 assert any(os.readlink(f)=='/dev/watchdog' for f in Path(f'/proc/{pid}/fd').iterdir())
def digest(device,offset=0,size=None):
 h=hashlib.sha256()
 with open(device,'rb',buffering=0) as f:
  f.seek(offset);remaining=size
  while remaining is None or remaining:
   b=f.read(min(4*1024*1024,remaining) if remaining is not None else 4*1024*1024)
   if not b:assert remaining is None;break
   h.update(b)
   if remaining is not None:remaining-=len(b)
 return h.hexdigest()
def verify_gpt(buf,header_offset,entry_offset):
 h=buf[header_offset:header_offset+512];assert h[:8]==b'EFI PART';size=struct.unpack_from('<I',h,12)[0]
 check=bytearray(h[:size]);expected=struct.unpack_from('<I',check,16)[0];struct.pack_into('<I',check,16,0);assert zlib.crc32(check)==expected
 count,length=struct.unpack_from('<II',h,80);assert (count,length)==(14,128)
 entries=buf[entry_offset:entry_offset+count*length];assert zlib.crc32(entries)==struct.unpack_from('<I',h,88)[0]
 return entries
root_unmounted();assert subprocess.check_output(['hostname'],text=True).strip()=='kstb-rescue'
assert '#30' in subprocess.check_output(['uname','-a'],text=True)
verified=json.loads(Path('/run/ROOT-backup-verified.json').read_text());assert verified['bytes']==5546967040 and verified['gzip_integrity']
assert Path('/run/tree-before.json').is_file()
meta=json.loads(Path('/run/new-DATA.json').read_text());assert meta['bytes']==DATA_BYTES and meta['data_first_lba']==NEW_DATA
protected=json.loads(Path('/run/protected-manifest.json').read_text())
for name in ('factory','KRN','DTB','splash','boot0','boot1'):
 m=protected[name];assert digest(m['device'],m.get('offset',0),m['bytes'])==m['sha256'],name
old_primary=Path('/run/old-primary.bin').read_bytes();new_primary=Path('/run/new-primary.bin').read_bytes()
old_tail=Path('/run/old-tail.bin').read_bytes();new_tail=Path('/run/new-tail.bin').read_bytes()
assert len(old_primary)==len(new_primary)==34*512 and len(old_tail)==len(new_tail)==33*512
old_entries=verify_gpt(old_primary,512,1024);new_entries=verify_gpt(new_primary,512,1024)
assert verify_gpt(new_tail,32*512,28*512)==new_entries
assert verify_gpt(old_tail,32*512,28*512)==old_entries
primary_changes=[n for n in range(34) if old_primary[n*512:(n+1)*512]!=new_primary[n*512:(n+1)*512]]
tail_changes=[15273567+n for n in range(33) if old_tail[n*512:(n+1)*512]!=new_tail[n*512:(n+1)*512]]
assert primary_changes==[1,3,4] and tail_changes==[15273596,15273597,15273599]
assert struct.unpack_from('<QQ',new_entries,7*128+32)==(145408,14714879)
assert struct.unpack_from('<QQ',new_entries,8*128+32)==(14714880,15239167)
assert new_entries[7*128:7*128+40]==old_entries[7*128:7*128+40]
assert new_entries[7*128+48:8*128]==old_entries[7*128+48:8*128]
assert new_entries[8*128:8*128+32]==old_entries[8*128:8*128+32]
assert new_entries[8*128+40:9*128]==old_entries[8*128+40:9*128]
for i in range(14):
 if i not in (7,8):assert new_entries[i*128:(i+1)*128]==old_entries[i*128:(i+1)*128]
# No allocated old DATA cluster may occupy the new partition staging region.
with open('/dev/mmcblk0p9','rb',buffering=0) as f:
 b=f.read(512);spc=b[13];reserved=struct.unpack_from('<H',b,14)[0];fatsectors=struct.unpack_from('<I',b,36)[0];data_start=reserved+b[16]*fatsectors;total=struct.unpack_from('<I',b,32)[0]
 assert struct.unpack_from('<I',b,28)[0]==10979328
 f.seek(reserved*512);fat=f.read(fatsectors*512)
 for c in range(2,(total-data_start)//spc+2):
  if struct.unpack_from('<I',fat,c*4)[0]&0xfffffff:assert 10979328+data_start+(c-1)*spc<=NEW_DATA
with open(DISK,'r+b',buffering=0) as disk:
 assert disk.read(len(old_primary))==old_primary;disk.seek(15273567*512);assert disk.read(len(old_tail))==old_tail
 checkpoint('preflight_passed',root_backup_sha256=verified['sha256'],new_data_sha256=meta['sha256'])
 # Authenticate the whole compressed source before any physical write.
 h=hashlib.sha256();size=0
 with gzip.open('/run/DATA-256.img.gz','rb') as f:
  for b in iter(lambda:f.read(4*1024*1024),b''):h.update(b);size+=len(b)
 assert size==DATA_BYTES and h.hexdigest()==meta['sha256']
 root_unmounted();disk.seek(NEW_DATA*512);written=0
 checkpoint('new_data_write_started')
 with gzip.open('/run/DATA-256.img.gz','rb') as f:
  for b in iter(lambda:f.read(4*1024*1024),b''):
   assert written+len(b)<=DATA_BYTES;assert disk.write(b)==len(b);written+=len(b)
 assert written==DATA_BYTES;os.fsync(disk.fileno())
 assert digest(DISK,NEW_DATA*512,DATA_BYTES)==meta['sha256']
 checkpoint('new_data_written_and_readback_verified',bytes=written)
 # Backup GPT first, primary last. Write only the six reviewed changed sectors.
 for lba in [15273596,15273597,15273599,3,4,1]:
  source=new_tail[(lba-15273567)*512:(lba-15273566)*512] if lba>34 else new_primary[lba*512:(lba+1)*512]
  assert len(source)==512;disk.seek(lba*512);assert disk.write(source)==512;os.fsync(disk.fileno())
  disk.seek(lba*512);assert disk.read(512)==source
 checkpoint('gpt_six_sectors_written',sectors=[15273596,15273597,15273599,3,4,1])
 fcntl.ioctl(disk.fileno(),0x125f)  # BLKRRPART: ROOT/DATA must remain unmounted.
assert int(Path('/sys/class/block/mmcblk0p8/size').read_text())==14569472
assert int(Path('/sys/class/block/mmcblk0p9/start').read_text())==NEW_DATA
assert int(Path('/sys/class/block/mmcblk0p9/size').read_text())==524288
checkpoint('kernel_partition_view_updated')
for name,args,allowed in [('gpt-verify',['sfdisk','--verify',DISK],(0,)),('new-data-fsck',['fsck.fat','-vn','/dev/mmcblk0p9'],(0,)),('root-fsck-before-grow',['e2fsck','-f','-p','/dev/mmcblk0p8'],(0,1)),('root-grow',['resize2fs','/dev/mmcblk0p8'],(0,)),('root-fsck-after-grow',['e2fsck','-f','-n','/dev/mmcblk0p8'],(0,))]:
 root_unmounted();checkpoint(name+'_started')
 r=subprocess.run(args,capture_output=True,text=True);(RUN/(name+'.txt')).write_text(r.stdout+r.stderr)
 assert r.returncode in allowed,(name,r.returncode,r.stdout,r.stderr);checkpoint(name+'_passed',returncode=r.returncode)
for name in ('factory','KRN','DTB','splash','boot0','boot1'):
 m=protected[name];assert digest(m['device'],m.get('offset',0),m['bytes'])==m['sha256'],name
checkpoint('physical_migration_complete',protected_boot_regions_unchanged=True)
