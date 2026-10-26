"""Record file payloads, links and ownership from a read-only offline ROOT mount."""
import hashlib,json,os,stat,sys
from pathlib import Path
root=Path(sys.argv[1]);dest=Path(sys.argv[2]);records={};total=0
assert root.is_dir()
def scan(path):
 global total
 s=path.lstat();r=dict(mode=s.st_mode,uid=s.st_uid,gid=s.st_gid)
 if stat.S_ISREG(s.st_mode):
  h=hashlib.sha256()
  with path.open('rb') as f:
   for b in iter(lambda:f.read(1024*1024),b''):h.update(b);total+=len(b)
  r.update(size=s.st_size,mtime_ns=s.st_mtime_ns,sha256=h.hexdigest())
 elif stat.S_ISLNK(s.st_mode):r.update(link=os.readlink(path),mtime_ns=s.st_mtime_ns)
 elif not stat.S_ISDIR(s.st_mode):r['rdev']=s.st_rdev
 records[str(path.relative_to(root))]=r
 if stat.S_ISDIR(s.st_mode):
  for child in sorted(path.iterdir()):scan(child)
scan(root);dest.write_text(json.dumps(records,sort_keys=True)+'\n')
print(json.dumps(dict(entries=len(records),hashed_bytes=total,manifest_sha256=hashlib.sha256(dest.read_bytes()).hexdigest())))
