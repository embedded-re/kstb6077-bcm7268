# Project platform preparation — 2026-10-26

The box now has a normal-user Python project environment and a working CPU
image classifier. Its working GPU/kernel stack was preserved, and verified
copies of historical build artifacts were moved off the box to reclaim space.
The recorded evidence is in [project readiness](../docs/evidence/projects/20261026-project-readiness/README.md).

## Recovery and storage

The development PC holds a private recovery capsule under
`port/build/project-prep/20261026-005000/recovery/` (ignored by Git):

- `working-state.tar.gz`: `/etc`, local system customization, native DRI
  libraries, kernel modules, kernel-build images/scripts, GPU rollback
  backups, Mesa staging/packages, historical GPU runs, old debug drivers,
  graphics tests and the user's desktop configuration/POC files.
- `KRN-partition.img`, `DTB-partition.img`: exact read-only copies of eMMC
  p6/p7, verified against hashes calculated on the box. No partition was written.
- `restore-check/`: the current DRI module, package world and six custom
  Mesa APKs extracted and compared byte-for-byte by SHA256 with the board.
- Archive checksum, boot-image manifest and a file-by-file cleanup manifest.

The archive contains private configuration, including system credentials;
its directory is mode 0700 and archive mode 0600. Keep it private. The archive
passed full gzip integrity verification. Extraction/hash comparison is the
recovery check performed; a destructive whole-machine restore was not tested.
This is a targeted recovery capsule, not a complete image of every rootfs file.

Before cleanup, all 6048 candidate file/link records were matched against the
archive, independently rechecked on the board, and checked for active process
mappings/open files. Cleanup removed only the verified archived run logs,
obsolete staging trees and historical debug builds. It reclaimed
**530,481,152 bytes (about 506MiB)**. On-box rollback backups, current r34 APKs,
`/opt/mesa/current` and two useful comparison builds were retained. User
profiles and caches were preserved. No protected eMMC partition was changed.

Root free space increased from about 648MiB to about 957MiB **after** installing
the new packages and model. A subsequently authorized [browser/Cargo cleanup](../docs/evidence/projects/20261026-cache-cleanup/README.md)
reclaimed another **795MiB**, reducing usage to **65%**, with about **1.8GB**
available. Both registered Firefox profiles were hash-checked unchanged. The
subsequent [verified storage migration](../docs/05-storage-repartition.md) expanded
ROOT to 7114MiB; about **3.4GiB is now available at 48% usage**. Large
training datasets/builds belong on the PC or future external storage; no
external disk or network mount was available/configured. The tested
[`backup-workspace`](project-tools/backup-workspace) tool copies normal project
files to the PC's ignored `port/build/project-workspaces/` directory, omitting
the recreatable venv. It does not create a new network service or SSH identity.

### Recover selected files safely

Inspect/extract the PC archive into a temporary directory first. To recover a
historical run or debug build, copy just that archived directory back to its
original board path. Keep current graphics users stopped before replacing
runtime libraries. The six signed r34 APKs and saved `/etc/apk/world` describe
the current custom Mesa state; restore packages with APK rather than overlaying
arbitrary old libraries. Raw KRN/DTB images are retained for an existing boot
recovery procedure, not an instruction to flash them during ordinary projects.

## Installed environment

Native Alpine packages installed: **py3-numpy, py3-pillow, py3-onnxruntime,
py3-pip**, with dependencies including OpenBLAS. The actual package snapshots
and install log are retained in the evidence directory. No general system
upgrade was performed. Kernel #30, all six Mesa 23.3.6-r34 pins and the native
DRI hash remain unchanged:

`ab8e3f40c4d368bc46661342bde624de3051a249aeed1b02f220e39b6e8bcb8a`

User `kstb` owns `/home/kstb/projects`. The shared project environment is
`/home/kstb/projects/.venv`, created with `--system-site-packages --without-pip`.
It inherits the native APK modules, including pip; project additions can use
`python -m pip` inside the venv without writing into system Python. SciPy,
OpenCV, PyTorch and TensorFlow were not installed by this milestone.
Alpine/musl and Python 3.14 compatibility still matters for further packages.

## Working image-recognition example

Project: `/home/kstb/projects/image-classifier`. It uses pretrained FP32
MobileNetV2 and explicit **CPUExecutionProvider**, with model/label/image
checksums recorded. It performs ImageNet classification, not bounding-box
detection or training. See [usage and recreation instructions](project-tools/image-classifier/README.md).

As `kstb`:

```sh
. ~/projects/.venv/bin/activate
cd ~/projects/image-classifier
python classify.py
python classify.py /path/to/your-image.jpg --threads 2
```

The known sample correctly returns **Samoyed**, top1 probability about 0.8303.
One sample is a functional check, not independent dataset accuracy validation.
Warm model-only latency, 20 samples per setting after three warmups:

| CPU threads | Median | 95th percentile |
|---|---:|---:|
| 1 | 245.06ms | 276.09ms |
| 2 | 145.89ms | 146.58ms |
| 4 | 103.19ms | 103.65ms |

The four-thread result is about 9.7 model inferences/second. It excludes image
loading, preprocessing, startup, camera capture and display. Thread settings
were tested sequentially, so cache/warmup effects are not fully separated.
Two threads may be a better everyday choice alongside desktop apps.

FP32 logits were finite and agreed across thread counts within the recorded
numerical tolerances. The benchmarking process peaked at 149,916KiB RSS,
including multiple retained sessions; this is not the minimum model footprint.
ONNX Runtime logged an unknown Broadcom CPU vendor and failure to read the
custom display device's PCI-style vendor file. Explicit CPU inference passed;
no GPU execution provider was used or installed.

## Sustained check

A separate four-thread CPU classifier check repeats inference for five minutes
while holding/verifying a 256MiB integer-pattern allocation. The gate monitors
new kernel messages, numerical repeatability, RAM patterns, available memory
and CPU temperature. Conservative guards stop at 80°C or below 256MiB available
RAM; the hardware's advertised thermal trips are 95/110/125°C. This gate does
not establish operation at those hardware limits or prove long-term stability.

The gate completed **300.51 seconds / 2716 repeated inferences**. All output
and RAM-pattern checks passed. Highest sampled CPU temperature was **57.452°C**;
minimum available RAM was **1,588,184KiB** (about 1.51GiB), and process peak RSS
including the 256MiB allocation was **360,132KiB** (about 352MiB). No new kernel
messages, GPU resets, OOMs or I/O errors were recorded during the gate. An independent Linux watchdog and process timeout bound this run.
No desktop is started automatically; no GPU optimization or boot setting is
changed as part of project preparation.


A separate warm still-image pipeline measurement includes reading/decoding the
sample JPEG and preprocessing before four-thread inference: median **261.54ms**,
p95 **262.90ms**, about 3.8 still images/second. Preprocessing median was 158.93ms;
model inference median was 102.53ms. This excludes process/session startup and
is not a camera/display throughput measurement. The project backup was then
copied to the PC, read back and checked against the deployed scripts and both
completed reports. All test processes and the hardware watchdog were stopped.
