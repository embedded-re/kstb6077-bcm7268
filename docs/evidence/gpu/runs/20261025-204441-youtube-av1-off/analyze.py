"""Calculate rates from transcribed Stats for nerds interval endpoints."""
import json,re
from pathlib import Path
P=Path(__file__).resolve().parent
c=json.loads((P/'comparison.json').read_text())
for codec in ('av1','vp9'):
 a,b=c[codec]['first'],c[codec]['second'];drops=b['dropped']-a['dropped'];frames=b['total']-a['total'];assert 0<=drops<=frames and frames>0
 print(f'{codec}: {drops}/{frames} new frames dropped ({100*drops/frames:.2f}%)')
s=json.loads((P/'state.json').read_text());assert s['state']=='complete' and s['fault'] is None and s['browser_left_open']
assert not re.search(r'Resetting GPU|MMU error|Oops|BUG:|Unable to handle|Internal error|kmsg overrun',(P/'kernel.txt').read_text())
assert (P/'driver.sha256').read_text().strip()=='ab8e3f40c4d368bc46661342bde624de3051a249aeed1b02f220e39b6e8bcb8a'
print('PASS: native system driver; zero GPU faults in guarded comparison')
