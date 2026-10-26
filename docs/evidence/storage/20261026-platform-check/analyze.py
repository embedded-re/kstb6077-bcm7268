"""Validate recorded platform regression evidence, not live hardware."""
from pathlib import Path
import json,hashlib
p=Path(__file__).resolve().parent
def j(n):return json.loads((p/n).read_text())
for line in (p/'SHA256SUMS').read_text().splitlines():
 h,n=line.split('  ',1);assert hashlib.sha256((p/n).read_bytes()).hexdigest()==h,n
assert j('bare-metal-state.json')['state']=='complete'
assert j('doom-state.json')['state']=='complete' and j('doom-state.json')['quit_returned_to_monitor']
assert 'program returned, status 0' in (p/'doom-boot-proof.txt').read_text()
assert j('linux-state.json')['state']=='complete' and j('linux-state.json')['fault'] is None
assert len(j('linux-state.json')['passed'])==11
assert j('desktop-state.json')=={'state':'complete','sessions':3}
assert 'PASS: frames 709' in (p/'desktop-client.txt').read_text()
assert j('final-state.json')['state']=='complete'
r=j('classifier-report.json');assert r['result']=='PASS' and r['soak']['inferences']==275
assert r['soak']['memory_check']=='PASS' and r['soak']['repeatability']=='PASS'
r=j('final-preservation.json');assert r['data_files']=={'verified':8,'extra_files':0}
for n in ('factory','KRN','DTB','splash','boot0','boot1'):assert r[n]['unchanged']
assert 'RUNNING' in (p/'final-audio-running.txt').read_text()
assert 'RESULT=PASS' in (p/'linux-compute-fft.txt').read_text()
assert 'Available formats:' in (p/'audio-first-probe.txt').read_text()
print('PASS: recorded bare-metal, Doom, Linux, desktop, compute, audio and preservation checks')
