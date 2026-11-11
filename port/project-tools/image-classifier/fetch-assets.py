#!/usr/bin/env python3
"""Fetch the recorded public sample assets and verify exact SHA256 values."""
import hashlib
import json
from pathlib import Path
import urllib.request
root=Path(__file__).resolve().parent
manifest=json.loads((root/'assets.json').read_text())
out=root/'assets';out.mkdir(exist_ok=True)
for name,entry in manifest.items():
    data=urllib.request.urlopen(entry['url'],timeout=90).read()
    if len(data)!=entry['bytes'] or hashlib.sha256(data).hexdigest()!=entry['sha256']:
        raise RuntimeError('asset checksum mismatch: '+name)
    (out/name).write_bytes(data)
(out/'assets.json').write_text(json.dumps(manifest,indent=2)+'\n')
print('Verified model, labels, sample image and model license.')
