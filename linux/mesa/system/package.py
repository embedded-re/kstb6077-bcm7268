#!/usr/bin/env python3
"""Split a /usr Mesa stage into native Alpine packages. Run with apk-tools 3."""
import argparse, hashlib, json, os, re, shutil, subprocess
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('stage', type=Path)
p.add_argument('output', type=Path)
p.add_argument('--version', default='23.3.6-r33')
p.add_argument('--sign-key', type=Path)
a = p.parse_args()
assert (a.stage/'usr/lib/dri/v3d_dri.so').is_file()
a.output.mkdir(parents=True, exist_ok=True)
names = ['mesa', 'mesa-egl', 'mesa-gbm', 'mesa-gles', 'mesa-gl', 'mesa-dev']
roots = {name: a.output/(name+'-files') for name in names}
for root in roots.values():
    assert not root.exists(), f'refusing to overwrite {root}'
    root.mkdir()

def owner(rel, source):
    s = str(rel)
    if s.startswith('usr/include/') or s.startswith('usr/lib/pkgconfig/'):
        return 'mesa-dev'
    if rel.parent == Path('usr/lib'):
        if source.is_symlink() and rel.name.endswith('.so'):
            return 'mesa-dev'
        for prefix, name in [('libEGL.', 'mesa-egl'), ('libgbm.', 'mesa-gbm'),
                             ('libGLES', 'mesa-gles'), ('libGL.', 'mesa-gl')]:
            if rel.name.startswith(prefix): return name
    return 'mesa'

# Preserve the many hardlinks used by Gallium's DRI aliases.
links = {}
for source in sorted(a.stage.rglob('*')):
    if source.is_dir(): continue
    rel = source.relative_to(a.stage)
    # Mesa's bundled protocol XML is a build dependency, not our system package.
    if str(rel).startswith('usr/share/wayland-protocols/') or str(rel) == 'usr/share/pkgconfig/wayland-protocols.pc':
        continue
    name = owner(rel, source)
    target = roots[name]/rel
    target.parent.mkdir(parents=True, exist_ok=True)
    if source.is_symlink():
        target.symlink_to(os.readlink(source))
    else:
        key = (name, source.stat().st_dev, source.stat().st_ino)
        if key in links: os.link(links[key], target)
        else:
            shutil.copy2(source, target)
            links[key] = target

metadata = {}
for name, root in roots.items():
    needed, provides = set(), set()
    seen = set()
    for f in root.rglob('*'):
        if not f.is_file() or f.is_symlink(): continue
        inode = f.stat().st_ino
        if inode in seen: continue
        seen.add(inode)
        if f.read_bytes()[:4] == b'\x7fELF':
            text = subprocess.check_output(['readelf', '-d', str(f)], text=True)
            needed.update(re.findall(r'\(NEEDED\).*?\[(.*?)\]', text))
            provides.update(re.findall(r'\(SONAME\).*?\[(.*?)\]', text))
        if f.suffix == '.pc':
            version = re.search(r'^Version:\s*(\S+)', f.read_text(), re.M).group(1)
            provides.add('pc:'+f.stem+'='+version)
    depends = {'so:'+s for s in needed-provides}
    if name != 'mesa': depends.add('mesa='+a.version)
    if name == 'mesa-dev':
        depends.update(n+'='+a.version for n in names if n not in ('mesa', name))
        depends.update(['libdrm-dev', 'libx11-dev', 'libxcb-dev', 'libxext-dev',
                        'libxfixes-dev', 'libxshmfence-dev', 'libxxf86vm-dev',
                        'libxdamage-dev', 'pkgconf'])
    exported = [s if s.startswith('pc:') else 'so:'+s for s in sorted(provides)]
    info = {'name': name, 'version': a.version, 'arch': 'aarch64',
            'description': 'Mesa for KAON KSTB6077 V3D 3.3 (local maintained driver)',
            'license': 'MIT', 'origin': 'mesa-kstb',
            'url': 'https://www.mesa3d.org/',
            'depends': ' '.join(sorted(depends)), 'provides': ' '.join(exported)}
    cmd = ['apk', 'mkpkg', '--files', str(root), '--output', str(a.output/(name+'-'+a.version+'.apk'))]
    for key, value in info.items():
        if value: cmd += ['--info', key+':'+value]
    if a.sign_key: cmd += ['--sign-key', str(a.sign_key)]
    subprocess.run(cmd, check=True)
    metadata[name] = info
(a.output/'metadata.json').write_text(json.dumps(metadata, indent=2)+'\n')
for f in a.output.glob('*.apk'):
    print(hashlib.sha256(f.read_bytes()).hexdigest(), f.name)
