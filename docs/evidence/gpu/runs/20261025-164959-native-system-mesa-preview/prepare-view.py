from pathlib import Path
import os,shutil
root=Path('/root/gpu-system-prep');source=root/'new-lib';dest=root/'view-lib'
for p in source.rglob('*'):
 target=dest/p.relative_to(source)
 if p.is_dir():target.mkdir(exist_ok=True);continue
 if target.is_symlink() or target.exists():target.unlink()
 if p.is_symlink():target.symlink_to(os.readlink(p))
 else:shutil.copy2(p,target)
