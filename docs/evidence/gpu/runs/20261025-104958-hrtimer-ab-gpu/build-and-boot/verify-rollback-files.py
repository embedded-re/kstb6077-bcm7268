from pathlib import Path
import hashlib,json,tarfile
root=Path('/root/gpu-backups/kernel29-before-hrtimer-ab')
meta=json.loads((root/'manifest.json').read_text())
for name,entry in meta['images'].items():
 saved=Path(entry['file']).read_bytes()
 with Path(entry['device']).open('rb') as f:current=f.read(len(saved))
 assert current==saved,name
 assert hashlib.sha256(current).hexdigest()==entry['sha256']
files=0
with tarfile.open(root/'modules.tar') as t:
 for entry in t:
  if entry.isfile():
   assert t.extractfile(entry).read()==(Path('/lib/modules')/entry.name).read_bytes(),entry.name
   files+=1
print('PASS: saved KRN/DTB match active partition bytes; all',files,'baseline module files unchanged')
