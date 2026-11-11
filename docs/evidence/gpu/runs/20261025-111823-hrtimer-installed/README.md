# Authorized permanent installation: timer-enabled kernel #30

The user explicitly approved installing the validated timer-enabled kernel
on 2026-10-25. `kstb-flash-kernel` writes the retained candidate to KRN and
its matching DTB image to DTB, and reads both back successfully. The DTB
payload is unchanged. A clean reboot to kmon followed by `linux` boots
**6.18.57-kstb-hrtimer-ab #30 from the saved slots**, not over TFTP.

`flash.txt` records the writes/read-back checks. `boot.txt` records saved
Image CRC `4f8a5020`, DTB payload CRC `c672cba9` and the #30 banner.
`verification.txt` independently checks both active partition images against
the candidate hashes, confirms rollback backups are intact and all 44
original module files remain unchanged. The versioned #30 modules were
already installed alongside #29's directory in the preceding RAM test.

## Post-install gate

All ten stages complete, with **zero GPU resets, MMU errors or oopses**:

- 101 empty-shader timing rounds.
- 55 neural inferences: batches 1/65/257/1024/4096, local size 128, eleven
  rounds each with changing inputs. Every layer equals CPU exactly, all
  classifications correct and guards intact.
- 100 cross-context compute/native-fence chains, exact data and guards.
- 290 CTS cases: 261 Pass/29 NotSupported. Compute 195 Pass, UBO 25 Pass,
  framebuffer/MSAA 41 Pass/29 NotSupported. No failures.

The pre-existing absent-CSD scheduler warning remains on context open.
This gate supplements the same kernel's preceding RAM validation of
1,253 CTS cases, 600 neural runs and 350 fence chains including three
intentional resets; it does not repeat those intentional resets.

Warm medians, ten rounds after a separately measured cold round:

| Batch | GPU dispatch/barriers/finish ms | CPU reference ms |
| --- | ---: | ---: |
| 1 | 0.306 | 0.002 |
| 65 | 0.484 | 0.101 |
| 257 | 1.288 | 0.398 |
| 1024 | 3.733 | 1.594 |
| 4096 | 9.878 | 6.520 |

These timings are for the small generated-weight 32 → 16 ReLU → 8 network,
with reused buffers/programs and new inputs. GPU timing excludes input
upload and output verification. This workload still runs faster on the CPU.
General model runtime and optimized CPU/GPU framework performance have not
been measured by this test.

## State and rollback

Kernel #30 now persists in KRN. `/opt/mesa/current` remains `fe7dda5e`;
`/usr/local` is unchanged. Tests explicitly select `23.3.6-5a3fd8dd` with
`V3D_COMPUTE33=1` and no version override. Installing the kernel does not
promote that experimental Mesa build or change the compositor configuration.
No GPU tests, queues or userspace watchdog remain running.

Original #29 backups remain on board at
`/root/gpu-backups/kernel29-before-hrtimer-ab/{krn.img,dtb.img,modules.tar}`
and on the host under `port/build/gpu-timer-ab/kernel29-before-hrtimer-ab/`.
The prior A/B/A milestone actually booted #29 and exercised GPU checks,
so the rollback kernel is tested. If restoring becomes necessary, flash
the backed-up krn.img/dtb.img with `kstb-flash-kernel` and reboot through
kmon's `linux` command; the original module directory is still present.
No rollback is requested or performed after this authorized installation.

`run.py` is the exact watchdog/fault-monitored post-install runner. Its CTS
helper remains in the original backup directory. `meta.json` records
executable/driver hashes. `analyze.py` verifies the stage results and exact
neural checks, and regenerates `summary.json` from raw logs. The saved
partition/backups/process check is reproduced by `verify-installed.py`.
The known attachment-free framebuffer reset, large-dispatch termination
issue and GPU compositor work remain unresolved.
