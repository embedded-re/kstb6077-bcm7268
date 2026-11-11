#!/usr/bin/env python3
"""Check or restore the pre-system-Mesa installation from its board backup."""
import argparse,json,os,shutil,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('backup',type=Path)
p.add_argument('--restore',action='store_true',help='restore packages, old local files and kiosk launcher')
a=p.parse_args();b=a.backup
packages=[b/'old-apks'/(n+'-26.1.6-r0.apk') for n in ('mesa','mesa-dev','mesa-egl','mesa-gbm','mesa-gl','mesa-gles','mesa-rusticl')]
for prefix in ('libclc-','spirv-llvm-translator-libs-'):
 found=list((b/'old-apks').glob(prefix+'*.apk'));assert len(found)==1,found;packages+=found
assets=[Path(s) for s in json.loads((b/'local-assets.json').read_text())]
for f in packages+[b/'world.before',b/'tv.before']:assert f.is_file(),f
for dest in assets:
 assert str(dest).startswith('/usr/local/'),dest
 src=b/'local-files'/dest.relative_to('/')
 assert src.exists() or src.is_symlink(),src
 assert not dest.exists() and not dest.is_symlink(),f'refusing to replace {dest}'
print('Backup package files and original local assets are present.')
if a.restore:
 assert os.geteuid()==0,'run as root'
 assert not subprocess.run(['pidof','sway'],capture_output=True).stdout.strip(),'exit the desktop first'
 shutil.copy2(b/'world.before','/etc/apk/world')
 subprocess.run(['apk','add']+[str(f) for f in packages],check=True)
 for dest in assets:(b/'local-files'/dest.relative_to('/')).rename(dest)
 shutil.copy2(b/'tv.before','/usr/bin/tv')
 for f in ('/etc/profile.d/kstb-desktop.sh','/etc/sway/config.d/50-kstb.conf'):Path(f).unlink(missing_ok=True)
 shutil.copy2(b/'world.before','/etc/apk/world')
 subprocess.run(['apk','fix','--simulate'],check=True)
 print('Previous Mesa packages, local drivers and launcher restored.')
