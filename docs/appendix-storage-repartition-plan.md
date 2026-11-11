# Reclaiming unused DATA space for Linux — inspected 2026-10-26

**Completed:** the recommended 256MiB layout was applied and verified. See the
[completion report](05-storage-repartition.md). The inspection below records the
pre-migration state and reviewed alternatives.

**State at inspection, before the authorized migration:** inspection and
PC-image tests complete; no physical partition changes at that stage.
The user's recollection is correct: bare-metal `DATA` reserves 2080MiB but
currently allocates only 25.56MiB. A 256MiB DATA partition is a practical
candidate, returning 1824MiB to Linux. A 512MiB alternative is also prepared.
Both passed PC GPT/FAT checks and the shared bare-metal FAT driver tests.
Physical migration and boot testing followed later, as recorded in the completion report.

[Recorded inspection/tests](evidence/storage/20261026-inspection/README.md).

## Actual pre-migration layout

eMMC user area: **7,820,083,200 bytes / 15,273,600 sectors / 7.28GiB**.
No large unpartitioned area exists. The apparent extra space is inside DATA.

| Entry | Name | First LBA | Last LBA | Purpose / size |
|---|---|---:|---:|---|
| p1 | macadr | 34 | 34 | Factory identity,512 bytes |
| p2 | nvram | 35 | 162 | Boot environment,64KiB |
| p3 | bsu | 163 | 2046 | Factory data,942KiB |
| p4 | misc | 2048 | 4095 | BOOT FAT,1MiB |
| p5 | hwcfg | 4096 | 6143 | Hardware configuration,1MiB |
| p6 | KRN | 6144 | 137215 | Linux kernel,64MiB |
| p7 | DTB | 137216 | 145407 | Linux device tree,4MiB |
| p8 | ROOT | 145408 | 10979327 | Mounted ext4 Linux,5290MiB |
| p9 | DATA | 10979328 | 15239167 | FAT32 bare-metal files,2080MiB |
| p10 | splash | 15239168 | 15273594 | Splash, kboot fallback, golden and slot A;16.81MiB |

Separate eMMC boot0/boot1 hardware areas are 4MiB each. They are not part of
this partitioning proposal and must not be rewritten. BOLT is in boot0.

Both current GPT headers/entry-array CRCs are valid, entries match, and
`sfdisk --verify` reports no errors. The table has **14 entries**, first usable
LBA 6 and last usable LBA 15273594. A generic 128-entry GPT would need more tail
space and conflict with the existing splash boundary. Preserve the existing
entry count, geometry, disk GUID, every partition GUID/type/name and all
entries except the ROOT end and DATA start.

ROOT ext4 is exactly the current partition size: 1,354,240 blocks of 4096 bytes.
There is no already-unclaimed space between its filesystem and partition end.
Its UUID is `435cda17-99fa-492f-ba47-d04cc34286ea`. ROOT stays p8, at the same
start LBA, so the kernel command line and fstab need no change.

## What DATA contains

Eight regular files, total **26,660,796 bytes**:

- `doom1.wad`:14,445,628 bytes.
- `backup/boot0.bin`, `backup/boot1.bin`, `backup/lba0.bin`, `backup/gptbak.bin`:
  old boot/factory/GPT backups,11,551,232 bytes together. The GPT backup is
  historical; it must not be mistaken for the current partition table.
- `scale22.wav`, `scale48.wav`:642,232 bytes together.
- **`kboot.bin`:21,704 bytes, part of the normal boot path.**

The partition was mounted explicitly read-only, inventoried/hashed, copied to
the PC, and unmounted. Every copied file matched its board SHA 256.
A read-only snapshot of metadata plus every allocated cluster reconstructed
an original-size sparse image; `fsck.fat -vn` reports clean, 818 allocated
clusters out of 66,541. Both FAT copies matched. Unallocated sectors were not
copied, so this is not a bit-for-bit whole-partition image.

The current boot sequence documented in [03-running-code.md](03-running-code.md) loads
`flash0.DATA:kboot.bin` through BOLT at EL3. If it cannot load the file, the
STARTUP fallback loads a raw copy from `splash +0x80000`. Both copies were
read and found byte-identical during this inspection. Keep both intact.
This inspection did not read/alter the active BOLT environment through its
console or test the fallback boot. The chain description comes from the
previously tested boot documentation.

## Prepared choices

DATA's end remains fixed immediately before splash. Its start moves right;
ROOT's start remains fixed and its end grows right into the reclaimed space.
This is **relocating/recreating DATA**, then growing ROOT—not simply reducing
a partition's end while leaving free space after it.

| Choice | DATA first–last LBA | DATA cluster | ROOT size | Linux gain |
|---|---|---:|---:|---:|
| **256MiB recommended** | 14714880–15239167 | 2KiB | **7114MiB /6.95GiB** | **1824MiB /1.78GiB** |
| 512MiB alternative | 14190592–15239167 | 4KiB | 6858MiB /6.70GiB | 1568MiB /1.53GiB |

256MiB leaves roughly ten times the current allocated data. 512MiB offers
more future bare-metal storage. Projected partition sizes are not `df` free
space: ext4 metadata and reserved blocks affect the usable/free result.

Why was 2080MiB needed before? The existing FAT uses 32KiB clusters; at 2GiB
that format falls just below FAT32's minimum cluster count. Smaller clusters
remove that sizing constraint. `extras/fat/fat.c` accepts power-of-two cluster sizes
from 512 bytes through 32KiB, and `extras/mon/mmc.c` discovers DATA by its GPT name
rather than a fixed start address. The proposed cluster sizes both fit those
checks. BOLT's actual boot on those new filesystems still needs validation.

## Tests completed entirely on PC images

For both choices:

1. CRC-checked current primary/backup GPT, patched only ROOT end/DATA start,
   regenerated entry/header CRCs and preserved all other fields.
2. Wrote sparse disk images with the exact physical disk size. Both passed
   `sfdisk --verify`; byte differences were confined to six existing GPT
   sectors: 1,3,4 and 15273596,15273597,15273599. No factory, KRN, DTB or splash
   sector changed in the modeled images. Existing 14-entry geometry retained.
3. Created standard FAT32 images with correct new hidden-sector fields,
   restored all eight files, extracted them with MTools and checked all hashes.
   The 256MiB image has 130,554 clusters; 512MiB has 130,811, both valid FAT32.
4. Compiled the actual shared `extras/fat/fat.c` with file-backed block callbacks.
   It read/compared every file, including kboot and the large WAD, and passed
   a 153,619-byte multi-cluster write/read/delete check at the proposed absolute
   partition LBA. Both images passed `fsck.fat -vn` afterward.

This verifies filesystem construction and shared-driver behavior on host
images. It does not verify BOLT compatibility, physical flash writes, ext4
resizing, power-loss recovery or a boot of the proposed layout. No ROOT image
was fabricated in the sparse GPT model, and it is not bootable.

## Required migration and rollback work

Before any disk write:

- Select the DATA size. Preserve and recheck current GPT and every protected
  region, plus the existing file backups. Obtain an **offline, verified full
  ROOT image** and preferably a raw DATA image on the PC. The earlier project
  recovery capsule is targeted backup material, not a full rootfs image.
- Prepare and **boot-test** a RAM/initramfs recovery environment with eMMC,
  network/serial access, `e2fsck`, `resize2fs`, filesystem and GPT tools.
  A rescue initramfs/TFTP path exists, but its artifacts date from Oct 22 and
  were not boot-tested this session. The running system has `e2fsck` but
  **no `resize2fs`**; do not assume a normal boot supplies the missing tools.
- Perform migration with ROOT unmounted. Keep BOLT/NVRAM, KRN/DTB, splash,
  its loader and both monitor slots unchanged. Retain their known offsets.

Then stage/verify the new DATA contents in the final location, change only
the reviewed GPT metadata, reread the table, and recheck the new DATA. Check
ROOT offline, grow its filesystem into the enlarged p8, then check it again.
Do not run a filesystem shrink on ROOT. The
[resize2fs manual](https://man7.org/linux/man-pages/man8/resize2fs.8.html)
requires enlarging the underlying partition first and preserving its start.
The [sfdisk manual](https://man7.org/linux/man-pages/man8/sfdisk.8.html)
describes preserving explicit sector geometry and backing up GPT metadata.

Finally validate BOLT→EL3 kboot→kmon→Linux, four CPUs, normal kernel/driver,
root files, all DATA file hashes, a small FAT read/write check and reboot.
A repaired/expanded root filesystem makes restoring only the old GPT unsafe:
rollback after growth must restore the offline ROOT image (and DATA/GPT as
needed), rather than truncating the grown filesystem with the old partition
boundary. Keep the recovery boot available throughout.

No actual migration, filesystem resize, forced reboot or golden-slot write
was performed during this inspection.
