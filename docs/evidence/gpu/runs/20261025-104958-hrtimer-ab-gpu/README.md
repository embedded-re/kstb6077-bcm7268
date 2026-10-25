# Timer A/B/A: high-resolution timers remove the compute cache-clean delay

The tested change is **`CONFIG_HIGH_RES_TIMERS=y`**, now present in
`linux/kstb.config`. All V3D cache flushing, MMU flushing and synchronization
remain enabled. No Mesa driver source, V3D IRQ code or GPU clock settings
changed. The candidate is kernel **#30**, release
`6.18.57-kstb-hrtimer-ab`, built from kernel #29's exact config with the
high-resolution timer setting enabled and a separate module-version suffix.

The candidate was **booted from RAM over TFTP**, using the existing root DTB.
Neither KRN nor DTB was flashed. After validation, the board was rebooted
through kmon's `linux` command into the unchanged saved **kernel #29**.
The slow timings return after rollback. This is an A/B/A experiment with
identical Mesa/test executable hashes, not a comparison of different shaders.

## Results

Ten warm rounds follow one separate cold round for each neural measurement.
Host timings include dispatches, barriers and final `glFinish`, excluding
input upload and output verification. All model inputs change each round.

| Probe | Before, #29 | RAM candidate, #30 | Rolled back, #29 |
| --- | ---: | ---: | ---: |
| Empty shader, 1,000 warm rounds: median wall ms | 2.101 | 0.140 | 2.102 |
| 64 jobs, one final wait: median wall ms | 253.203 | 9.066 | 253.207 |
| Neural batch 257/local 128: median wall ms | 26.355 | **1.273** | 26.362 |
| Neural batch 4096/local 128: median wall ms | 190.500 | **9.897** | 188.805 |
| Requested 10 µs userspace sleep: median ms | 3.998 | 0.067 | 3.998 |

The matched neural comparisons are **20.7× faster** at batch 257 and
**19.2× faster** at batch 4096. CPU reference medians on the candidate are
0.398 and 6.520 ms respectively: **this small model still runs faster on the
CPU**. A 501-round batch-257 run also passes; its 500 warm rounds have median
0.860 ms. That repeated-run result is a separate measurement from the
matched ten-round comparisons and does not establish hours-long reliability.

Per-file queue accounting narrows the changed time:

| Kernel interval, warm median ms | #29 before | #30 candidate | #29 after rollback |
| --- | ---: | ---: | ---: |
| Neural 257: render queue | 0.427 | 0.412 | 0.429 |
| Neural 257: cache-clean routine | 25.661 | **0.175** | 25.683 |
| Neural 4096: render queue | 6.294 | 6.612 | 6.292 |
| Neural 4096: cache-clean routine | 182.934 | **1.320** | 181.347 |
| 64-job probe: cache-clean routine | 251.031 | **2.042** | 251.029 |

These are elapsed kernel regions, including any sleeps/preemption within
them, not isolated hardware execution times. The `wait_for` helper used
by `v3d_clean_caches` sleeps while polling, starting at 10–20 µs. The old
kernel has HZ=250 and high-resolution timers disabled. The controlled
configuration change removes the bulk cache-clean interval, and reverting
it restores that interval. Individual polling sites were not traced;
ftrace remains disabled in both kernels. No required clean was skipped.
After the change, much of the neural wall interval is inside the host
submission calls; batch 257 medians are 0.811/0.385 ms for the two submission
segments and 0.077 ms for the final barrier/finish segment. Segments are
measured separately and their medians need not add exactly.

## Reliability gate

All **38 candidate stages** complete successfully:

- **2,473 timing rounds**, accounting for **7,557 completed render jobs**
  and the same number of cache-clean jobs; no bin jobs in the compute probes.
- **600 neural inferences**, including batches 1/65/257/1024/4096, a
  local-size-16 control, 501 repeated batch-257 runs and post-reset checks.
  Every hidden value/logit equals CPU exactly; classifications and all
  prefix/suffix guards pass.
- **350 cross-context compute/native-fence chains**: 200 normal, followed
  by three 50-chain runs, each with one deliberate CL-stall/GPU-reset probe.
  Each compute fence is confirmed pending before the corresponding reset,
  and the same contexts/buffers continue with exact results afterward.
- **1,253 CTS cases**: 1,224 Pass and 29 NotSupported, with zero failures.
  Compute 195/195, atomic counters 298/298 and normal-sized indirect draws
  157/157 all Pass. GLES2 is 205 Pass; GLES3 is 369 Pass/29 NotSupported.
  All 603 GLES2/GLES3 case statuses are identical to the immediately preceding
  [kernel-29 baseline](../20261025-104138-kernel29-timer-ab-graphics/README.md).

There are exactly **three deliberate GPU resets**, **zero unexpected
resets, zero MMU errors and zero oopses**. The existing absent-CSD scheduler
warning still appears on context open. The known attachment-free framebuffer
reset, large indirect cases and complete-suite certification remain outside
this gate and unresolved.

The [rollback run](../20261025-110017-hrtimer-rollback29/README.md) completes
five stages: 1,032 timing rounds, 22 exact neural inferences and 50 normal
fence chains, with zero resets/MMU errors/oopses. It verifies the saved
kernel is usable and independently reproduces the slow timing again.

## Build, boot and rollback

The running #29 config was byte-identical to `linux/build/kernel/.config`.
An isolated output directory was copied from that baseline. The candidate
was built with:

```sh
linux/build/linux-6.18.57/scripts/config --file "$candidate_build/.config" \
  --enable HIGH_RES_TIMERS --set-str LOCALVERSION '-kstb-hrtimer-ab'
make -C linux/build/linux-6.18.57 O="$candidate_build" ARCH=arm64 LLVM=1 olddefconfig
make -C linux/build/linux-6.18.57 O="$candidate_build" ARCH=arm64 LLVM=1 -j6 Image modules
make -C linux/build/linux-6.18.57 O="$candidate_build" ARCH=arm64 LLVM=1 \
  M="$PWD/linux/drivers" MO="$candidate_drivers" -j6 modules
```

Here `candidate_build` is the absolute path to
`linux/build/gpu-timer-ab/kernel`, and `candidate_drivers` the sibling
`drivers` directory. All 30 modules were installed into an isolated
modroot and packaged. Every module's vermagic matches the candidate release;
its unique module directory was installed alongside #29's directory before
the RAM boot. Required/default config selections are listed in
`build-and-boot/config-diff.txt`. Regenerating the config from the updated
production fragment matches the candidate apart from its temporary
`LOCALVERSION` suffix; see `production-config-diff.txt`.

`tools/kstb-linux --root --watchdog 150 --listen 20` checked the Image/DTB/
boot-wrapper transfer CRCs and started the RAM candidate. The original TFTP
files were restored immediately afterward and byte-compared again at the
end. The root DTB payload is unchanged, CRC `c672cba9`. `ram-boot.txt`
records the new Image CRC `4f8a5020`; `candidate-boot-check.txt` confirms
kernel #30 and the enabled config. The embedded candidate config exactly
matches the built config.

For rollback, a clean reboot returns to kmon; its `linux` command checks and
boots the original saved KRN/DTB. `rollback-boot.txt` records original Image
CRC `335f53c0`, DTB CRC `c672cba9`, and the #29 kernel banner. Byte comparisons
before and after the experiment confirm that both partitions and all 44
baseline module files are unchanged. Checksummed backups remain on both
board and host, described by `kernel29-backup-manifest.json`.

Candidate artifacts are retained, without flashing:

- Host: `linux/build/gpu-timer-ab/out/{Image,krn.img,dtb.img,modules.tar}`.
- Board: `/root/gpu-kernel-ab/hrtimer/` (same package files).
- Matching modules: `/lib/modules/6.18.57-kstb-hrtimer-ab/`.
- Rollback images/modules: `/root/gpu-backups/kernel29-before-hrtimer-ab/`.

`build-and-boot/artifacts.json` records artifact and V3D source hashes.
Build/module installation logs and boot/rollback checks are retained there.
The tested V3D source still has patches 0001–0005 only. No TFU, perf-counter,
CSD-warning or other candidate kernel changes were bundled with this test.

## Reproduction and final state

`run.py` is the exact monitored board runner. It requires the candidate
kernel, starts the Linux watchdog, stops on an unexpected GPU fault, and
restores the original PM control setting. It calls `cts-stage.py` from the
baseline backup directory; a copy is included here. Run only one GPU gate
at a time. The analyzer validates the A/B/A files, matched executable hashes,
per-round job counts, reset/fence ordering and unchanged graphics statuses:

```sh
python3 analyze.py
```

`summary.json` contains the resulting complete measurements. The earlier
baseline timing and neural-accounting directories are its input datasets.

**Final board state: original kernel #29, selected Mesa `fe7dda5e`, system
Mesa unchanged, no GPU validation/watchdog processes running.** The source
config fix is ready for future builds; permanently installing a kernel was
not performed. Work stops at this milestone.
