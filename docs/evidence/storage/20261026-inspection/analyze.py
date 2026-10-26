#!/usr/bin/env python3
"""Validate the retained inspection and offline test results, no device I/O."""
from pathlib import Path
import hashlib,json
p=Path(__file__).resolve().parent
inv=json.loads((p/'data-inventory.json').read_text())
assert len(inv['files'])==8 and sum(f['bytes'] for f in inv['files'])==26660796
assert inv['bpb']['sectors_per_cluster']==64
assert inv['bpb']['total_sectors']==4259840
assert 'No errors detected' in (p/'gpt-verify.txt').read_text()
assert '818/66541 clusters' in (p/'original-DATA-fsck.txt').read_text()
root=json.loads((p/'root-superblock.json').read_text())
assert root['block_size']==4096 and root['blocks']==1354240
assert root['filesystem_bytes']==5546967040
plans=json.loads((p/'plans.json').read_text())
assert [n['data_mib'] for n in plans]==[256,512]
for n in plans:
    assert n['root_first_lba']==145408 and n['data_last_lba']==15239167
    assert n['root_last_lba']+1==n['data_first_lba']
    assert n['linux_gain_mib']==2080-n['data_mib']
    assert n['root_mib']==5290+n['linux_gain_mib']
    assert n['clusters']>=65525 and n['all_eight_file_hashes_match']
    assert n['changed_primary_sectors']==[1,3,4]
    assert n['changed_backup_sectors']==[15273596,15273597,15273599]
    assert 'No errors detected' in (p/(str(n['data_mib'])+'-gpt-verify.txt')).read_text()
    fat=(p/(str(n['data_mib'])+'-baremetal-fat-check.txt')).read_text()
    assert fat.count('read_compare=PASS')==8 and 'RESULT=PASS' in fat
s=json.loads((p/'state.json').read_text())
assert not s['physical_partition_changes'] and not s['filesystem_format_on_device'] and not s['reboots']
assert s['entry_count']==14 and s['kboot_data_and_splash_loader_identical']
assert not s['offline_root_image_created'] and not s['rescue_image_boot_tested_this_session']
for line in (p/'SHA256SUMS').read_text().splitlines():
    digest,name=line.split('  ',1)
    assert hashlib.sha256((p/name).read_bytes()).hexdigest()==digest,name
print('PASS: current layout/inventory, both proposed GPT/FAT layouts, shared FAT driver, source/log hashes; no physical migration')
