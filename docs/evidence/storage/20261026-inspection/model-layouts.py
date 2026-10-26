from pathlib import Path
import hashlib,json,struct,subprocess,os,zlib
p=Path(__file__).resolve().parent
DISK=7820083200; LAST=15273599; END=15239168
prefix=(p/'first-3MiB.bin').read_bytes();tail=(p/'last-33-sectors.bin').read_bytes()
def header(b):
 assert b[:8]==b'EFI PART'
 n=struct.unpack_from('<I',b,12)[0];r=bytearray(b[:n]);expected=struct.unpack_from('<I',r,16)[0];struct.pack_into('<I',r,16,0)
 assert zlib.crc32(r)==expected
 return dict(lba=struct.unpack_from('<Q',b,24)[0],other=struct.unpack_from('<Q',b,32)[0],first=struct.unpack_from('<Q',b,40)[0],last=struct.unpack_from('<Q',b,48)[0],entries=struct.unpack_from('<Q',b,72)[0],count=struct.unpack_from('<I',b,80)[0],size=struct.unpack_from('<I',b,84)[0],crc=struct.unpack_from('<I',b,88)[0])
a=header(prefix[512:1024]);b=header(tail[-512:]);assert a['count']==b['count']==14 and a['size']==b['size']==128
entries=prefix[a['entries']*512:a['entries']*512+a['count']*a['size']]
backup=tail[(b['entries']-(LAST-32))*512:(b['entries']-(LAST-32))*512+len(entries)]
assert entries==backup and zlib.crc32(entries)==a['crc']==b['crc']
assert a['lba']==1 and b['lba']==LAST and a['other']==LAST and b['other']==1
plans=[]
for mib,spc in [(256,4),(512,8)]:
 d=p/str(mib);d.mkdir(exist_ok=True)
 start=END-mib*2048;modified=bytearray(entries)
 struct.pack_into('<Q',modified,7*128+40,start-1)
 struct.pack_into('<Q',modified,8*128+32,start)
 for i in range(14):
  if i not in (7,8):assert modified[i*128:(i+1)*128]==entries[i*128:(i+1)*128]
 before=bytearray(prefix[:34*512]);after=bytearray(tail)
 for buf,offset,entryoffset in [(before,512,a['entries']*512),(after,len(after)-512,(b['entries']-(LAST-32))*512)]:
  buf[entryoffset:entryoffset+len(modified)]=modified
  struct.pack_into('<I',buf,offset+88,zlib.crc32(modified));struct.pack_into('<I',buf,offset+16,0)
  n=struct.unpack_from('<I',buf,offset+12)[0];struct.pack_into('<I',buf,offset+16,zlib.crc32(buf[offset:offset+n]))
  header(buf[offset:offset+512])
 (d/'primary-lba0-33.bin').write_bytes(before);(d/'tail33.bin').write_bytes(after)
 image=d/'disk.sparse.img'
 with image.open('wb') as f:
  f.truncate(DISK);f.seek(0);f.write(before);f.seek(DISK-len(after));f.write(after)
 check=subprocess.check_output(['sfdisk','--verify',str(image)],stderr=subprocess.STDOUT,text=True)
 (d/'gpt-verify.txt').write_text(check)
 (d/'layout.sfdisk').write_text(subprocess.check_output(['sfdisk','--dump',str(image)],text=True))
 # Verify byte changes are confined to existing GPT metadata, not the splash
 # bytes included in the 33-sector backup window or protected low partitions.
 changed_primary=[i for i in range(34) if before[i*512:(i+1)*512]!=prefix[i*512:(i+1)*512]]
 changed_tail=[LAST-32+i for i in range(33) if after[i*512:(i+1)*512]!=tail[i*512:(i+1)*512]]
 assert set(changed_primary)<={1,2,3,4,5}
 assert set(changed_tail)<=set(range(b['entries'],LAST+1))
 fat=d/'DATA.img'
 with fat.open('wb') as f:f.truncate(mib*1024*1024)
 subprocess.run(['mkfs.fat','-F','32','-s',str(spc),'-S','512','-n','KSTB','-h',str(start),str(fat)],check=True,stdout=subprocess.DEVNULL)
 for item in sorted((p/'data-files').iterdir()):
  subprocess.run(['mcopy','-s','-m','-i',str(fat),str(item),'::/'],check=True)
 restored=d/'restored';restored.mkdir(exist_ok=True)
 subprocess.run(['mcopy','-s','-i',str(fat),'::*',str(restored)],check=True)
 inv=json.loads((p/'data-inventory.json').read_text())
 for item in inv['files']:assert hashlib.sha256((restored/item['name']).read_bytes()).hexdigest()==item['sha256']
 fsck=subprocess.check_output(['fsck.fat','-vn',str(fat)],stderr=subprocess.STDOUT,text=True);(d/'fsck-before.txt').write_text(fsck)
 with fat.open('rb') as f:boot=f.read(512)  # Read-only PC artifact.
 total=struct.unpack_from('<I',boot,32)[0];reserved=struct.unpack_from('<H',boot,14)[0];fats=boot[16];fatsectors=struct.unpack_from('<I',boot,36)[0]
 clusters=(total-reserved-fats*fatsectors)//boot[13]
 assert clusters>=65525 and boot[13]==spc and struct.unpack_from('<I',boot,28)[0]==start
 plans.append(dict(data_mib=mib,cluster_bytes=spc*512,data_first_lba=start,data_last_lba=END-1,root_first_lba=145408,root_last_lba=start-1,root_mib=(start-145408)//2048,linux_gain_mib=2080-mib,clusters=clusters,changed_primary_sectors=changed_primary,changed_backup_sectors=changed_tail,all_eight_file_hashes_match=True))
print(json.dumps(plans,indent=2));(p/'plans.json').write_text(json.dumps(plans,indent=2)+'\n')
