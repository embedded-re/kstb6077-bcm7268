"""Check recorded migration evidence; no device access or writes."""
from pathlib import Path
import hashlib,json
p=Path(__file__).resolve().parent
def j(n):return json.loads((p/n).read_text())
for line in (p/'SHA256SUMS').read_text().splitlines():
 digest,name=line.split('  ',1)
 assert hashlib.sha256((p/name).read_bytes()).hexdigest()==digest,name
assert j('result.json')['state']=='complete'
assert j('journal.json')[-1]['stage']=='physical_migration_complete'
assert j('tree-comparison.json')['entries']==58481
assert j('tree-comparison.json')['contents_links_permissions_ownership_identical']
for name,size in [('ROOT',5546967040),('DATA',2181038080)]:
 b=j(name+'-backup-verified.json');assert b['bytes']==size and b['gzip_integrity']
s=j('post-state.json');assert s['state']=='complete' and s['fault'] is None
assert set(s['tests'])=={'classifier','fences','graphics','caps'}
x=j('final-snapshot.json');assert x['cpus']==4
assert all(r['unchanged'] for r in x['protected'].values())
assert len(j('storage-cleanup.json')['original_files'])==8
assert 'table-length: 14' in x['layout']
assert 'size=    14569472' in x['layout'] and 'start=    14714880' in x['layout']
assert 'No errors detected' in x['verify']
assert '"result": "PASS"' in (p/'post-classifier.txt').read_text()
assert '100 native fence' in (p/'post-fences.txt').read_text()
assert '0 pixel checks failed' in (p/'post-graphics.txt').read_text()
for name in ('boot-normal-proof.txt','boot-final-proof.txt'):
 s=(p/name).read_text();assert "kboot's (4b423303)" in s and 'LBA #14714880-#15239167' in s and '#30 SMP' in s
print('PASS: migration, offline backups, preservation, boots and CPU/GPU regression evidence')
