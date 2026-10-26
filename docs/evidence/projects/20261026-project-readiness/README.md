# Project platform preparation evidence — 2026-10-26

User authorized storage preparation, preservation of the working system,
a native project environment, real CPU image inference and a bounded sustained
check. This directory retains public metadata/results only. The private
recovery files and public model binaries are stored outside Git.

## Recovery/storage evidence

- `recovery-archive.sha 256`: checksum of private `working-state.tar.gz` on
  the development PC under `linux/build/project-prep/20261026-005000/recovery/`.
- `boot-images.json`: exact KRN/p6 and DTB/p7 partition readbacks, with hashes
  independently calculated on the board and PC. No flash operation occurred.
- `cleanup-result.json`: verified files moved off-board, actual space reclaimed.
- The private archive passed full gzip integrity and file-by-file comparison
  against all 6048 cleanup file/link entries. Current driver, world and six
  signed r34 APKs were extracted and hash-compared to the board. A full rootfs
  restore was not attempted. System configuration in the archive is private.

## Installed software and model

`world-before-projects` and before/after APK snapshots identify the actual
transaction; `project-packages-install.txt` records native package installation.
`python-packages.txt` records the venv-visible versions. The venv uses native
system packages via system-site-packages; no pip binary wheel was installed.
Explicit CPUExecutionProvider is used. Custom Mesa/kernel are held fixed.

`assets.json` records exact URLs, sizes and hashes for public model/labels/
sample image/license. Model digest matched the ONNX Git LFS pointer before
execution. The sample is a known Samoyed photograph, expected class index 258.
This checks application function, not general ImageNet accuracy.

## Measurement scopes

`benchmark.json` and `classifier-benchmark.txt` contain 20 warm inference
samples per CPU thread setting after three warmups, plus session load and
first preprocessing times. GPU discovery and unknown-CPU-vendor warnings
are retained. CPU inference succeeds despite those discovery messages.
All thread settings return finite, consistent logits within rtol/atol 5e-4.

`run-soak.py` runs the classifier as user kstb, under a root kernel-log monitor,
validated new-process-group fault guard, 390-second process timeout and
independent Linux hardware watchdog. It never signals an existing desktop.
The classifier uses four CPU threads for 300 seconds, verifies repeated logits,
and holds/checks a 256MiB uint64 pattern allocation. It stops at 80°C or below
256MiB available RAM. This is a bounded preparation check, not an exhaustive
RAM test or long-duration reliability qualification. It does not stress GPU
compute or induce a reset.

`pipeline-benchmark.py` separately measures the warm still-image pipeline,
including file open/JPEG decode, resize/crop/normalization and inference. It
runs after the sustained check, not concurrently. Camera capture, display,
process startup and model load are excluded. No camera frame-rate claim follows.

Final gate: 300.51 seconds, 2716 repeated inferences, 256MiB RAM-pattern checks
PASS; maximum sampled 57.452°C, minimum available 1,588,184KiB. No kernel events
were recorded during the gate. Warm full still-image pipeline median 261.54ms.
`post-state.json` confirms custom Mesa/kernel preserved and tests stopped;
`workspace-backup.json` records a PC backup whose scripts and completed reports
were read/extracted and checked. No external disk or network filesystem was
mounted. Private recovery/configuration archives are not committed. See [the preparation report](../../../../linux/project-readiness.md)
and [project usage](../../../../linux/project-tools/image-classifier/README.md).

GPU capability lines have trailing whitespace trimmed; values are unchanged.
