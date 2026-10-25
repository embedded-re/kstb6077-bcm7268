"""Validate the stored app checks; no hardware is accessed."""
import json,re
from pathlib import Path
P=Path(__file__).resolve().parent
runs=['20261025-211247-poc-files','20261025-211304-poc-editor','20261025-211322-poc-video','20261025-211614-poc-breakout','20261025-211637-poc-transport','20261025-211713-poc-supertux','20261025-211835-poc-supertux','20261025-211958-poc-supertux','20261025-212204-poc-gameplay']
for name in runs:
 d=P.parent/name;s=json.loads((d/'state.json').read_text());assert s['state']=='complete' and s['fault'] is None,name
 assert not re.search(r'Resetting GPU|MMU error|Oops|BUG:|Unable to handle|Internal error|kmsg overrun',(d/'kernel.txt').read_text()),name
assert (P/'driver.sha256').read_text().strip()=='ab8e3f40c4d368bc46661342bde624de3051a249aeed1b02f220e39b6e8bcb8a'
menu=json.loads((P/'poc-apps-menu.json').read_text());assert len(menu)==6
assert next(a for a in menu if a['name']=='SuperTux 2')['exec']=='env SDL_VIDEODRIVER=wayland supertux2 --renderer sdl'
for name in ['20261025-211614-poc-breakout','20261025-211637-poc-transport','20261025-211958-poc-supertux']:
 v=json.loads((P.parent/name/'engines.json').read_text());assert any(a.get('v3d-jobs-render',0)>v['before'].get(k,{}).get('v3d-jobs-render',0) for k,a in v['after'].items()),name
v=json.loads((P.parent/'20261025-211322-poc-video'/'engines.json').read_text());assert any(a.get('v3d-jobs-render',0)>0 for a in v['before'].values())
print('PASS: six apps registered; native driver; game GPU jobs; completed probes have zero GPU faults')
