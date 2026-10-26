# Physical storage migration — 2026-10-26

Actual on-device execution of the reviewed 256MiB DATA proposal. See the
[completion report](../../../storage-repartition.md) for the resulting layout,
recovery precautions and scope of validation.

- `before.sfdisk` / `after.sfdisk`: physical partition table before and after.
- `journal.json`, `result.json`, fsck/grow logs: RAM-rescue migration sequence.
- `migrate.py`: actual fixed-layout migration program, retained for audit.
  It depends on private preflight artifacts and must not be run as a generic tool.
- `*-backup-verified.json`: complete old ROOT/DATA images decompressed and
  SHA256-checked on the PC before migration. Raw images remain private.
- `tree-comparison.json`: identical pre/post offline manifests, 58,481 entries.
  Full manifests contain private filenames and are intentionally excluded.
- `protected-manifest.json`: original region hashes; final comparison in
  `final-snapshot.json`. GPT sectors intentionally changed, so whole first/tail
  captures are recovery artifacts, not expected unchanged-region checks.
- `boot-*-proof.txt`: selected serial excerpts from two successful normal boots;
  the first includes the FAT write/read test. Temporary rescue files were
  removed before the second boot. The original eight DATA files match in
  `storage-cleanup.json`; `storage-final-data-fsck.txt` follows cleanup.
  `final-data-files.json` and `final-data-fsck.txt` independently recheck the
  exact eight-file inventory and FAT filesystem after the final boot.
- `post-*`: final CPU classifier, 100 fence chains, MSAA/depth/stencil and
  GLES capability checks. `post-kernel.txt` contains existing scheduler warnings;
  the test guard detected no GPU reset/MMU fault. This was a regression gate,
  not a new full conformance or compute-throughput run.

`python3 analyze.py` checks the recorded outcomes and evidence checksums.
It verifies evidence consistency offline; it does not retest the board or
prove power-loss recovery. A full rollback was not performed.
