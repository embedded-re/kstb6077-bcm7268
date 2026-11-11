# Storage inspection and offline layout checks — 2026-10-26

Current device inspection was read-only on the affected block devices. DATA
was temporarily mounted with `ro,nodev,nosuid,noexec`, inventoried and
unmounted. No partition-table/boot-file changes or reboots were performed.
See [the full proposal](../../../appendix-storage-repartition-plan.md).

- `layout.sfdisk`, `gpt-verify.txt`: current layout, sfdisk verification.
- `data-inventory.json`: all eight regular files, hashes/sizes, BPB fields,
  allocated/available space. Only 26MiB is allocated inside 2080MiB DATA.
- `root-superblock.json`: mounted ROOT's read-only metadata query; this is
  not an offline ext4 consistency check.
- `original-DATA-fsck.txt`: fsck of a sparse reconstruction containing the
  original metadata and every allocated cluster. The device itself was not
  fscked or modified. Both original FAT copies matched.
- `plans.json`, `256-*`, `512-*`: exact alternative layouts, valid GPT CRCs,
  FAT32 creation/restoration, and fsck results before/after shared-driver tests.
- `fat-image-check.c`, `kstb.h`: harness compiled with the unchanged repository
  `extras/fat/fat.c`. It uses regular PC files as block devices at the proposed
  absolute partition LBAs, reads/compares all files, writes/reads/deletes a
  153619-byte test file, then fsck checks the image. It performs no MMIO or
  actual board I/O. Compiler warned about existing fat.c initializers; the
  image checks passed. No existing FAT-driver source was changed.
- `model-layouts.py`: construction/CRC verification and MTools restore logic.
  All images it writes are regular files on the PC, not physical devices.
- `state.json`: boundaries, artifact locations and shared-driver source hashes.

Private, ignored artifacts in `port/build/storage-inspect/20261026/` include
first 3MiB/tail33 sector captures, the DATA tar/allocated-cluster snapshot,
sparse original DATA, proposed sparse disk images and proposed FAT images.
They contain raw factory/boot backups and must not be published. The eight
DATA payloads were copied and SHA 256-verified; the old DATA kboot file and
its raw splash fallback were read and compared byte-for-byte.

Both alternatives preserve ROOT start, DATA end, all partition identities,
the 14-entry GPT geometry, and protected partition boundaries. The 256MiB plan
uses 2KiB clusters and gains 1824MiB for Linux; 512MiB uses 4KiB and gains 1568MiB.
Each has enough FAT32 clusters. Shared-driver host execution passed for both.
These tests do not establish an actual BOLT boot of the changed FAT layout,
a successful physical migration or successful ROOT expansion.

A fresh verified RAM rescue boot and offline ROOT backup are still required
before execution. The running box lacks resize2fs. The existing rescue image
was inspected for tools, not booted or certified ready. No fallback boot,
golden write or BOLT environment change was attempted.

Offline check: `python3 analyze.py` validates the retained facts and manifests.
Rerunning `model-layouts.py` requires the private artifacts; the proposal has
not yet been written to the box.
