# Neural compute: workgroup sizing and reliability milestone

Completed on kernel #29 and existing numbered Mesa `23.3.6-5a3fd8dd`
(inner Mesa `e774a1202f`, patches 1–32), with `V3D_COMPUTE33=1`, no
API-version override and the disk shader cache disabled. No Mesa or kernel
code changed. The selected Mesa and `/usr/local` were not changed.
`meta.json` records the tested executable/driver hashes and environment.

The test now accepts `batch local-size rounds`. It reuses programs and
buffers, changes inputs after every round, and checks every hidden value,
logit, classification and both output buffers' 4 KiB guards each time.
Round zero is measured separately; the following table uses medians of ten
warm rounds. GPU time is host elapsed time through both compute dispatches,
barriers and `glFinish`, excluding input upload and result verification.
It is not isolated GPU shader execution time.

| Batch | Local size 16, GPU ms | Local size 128, GPU ms | CPU reference ms, local-128 run |
| --- | ---: | ---: | ---: |
| 1 | 4.384 | 4.384 | 0.002 |
| 65 | 39.874 | 15.903 | 0.106 |
| 257 | 114.369 | 26.469 | 0.397 |

Batch 257 is **4.32 times faster** with local size 128. Both sizes use the
same model and input sequence; classification checksums agree between all
four tested local sizes (16, 32, 64 and 128). The existing driver's
16-workgroups-per-render-job layout requires 26 render jobs per inference
at local size 16 and five at local size 128 for batch 257. The earlier
estimate of four was an arithmetic error: layer one needs 33 workgroups
(three jobs), and layer two needs 17 (two jobs). Subsequent per-file kernel
counters confirm 26 versus five jobs; see the
[queue-accounting milestone](../20261025-101822-compute-latency-milestone/README.md). This changes legal application workgroup dimensions;
it does not use the rejected driver tile-column packing experiment.

Repeated timings do not remove the large CPU/GPU gap. In the batch-257,
local-128 run, median final barrier/finish elapsed time is 23.869 ms out of
26.469 ms total. These host intervals include waiting for GPU work; they do
not distinguish shader execution, interrupt latency or scheduling costs.
Batch 4096/local 128 also passes all eleven rounds: warm GPU median
189.110 ms versus CPU reference median 6.486 ms. This small generated-weight
model still shows no GPU acceleration over the CPU.

## Reliability checks

All 19 stages in `results.json` complete successfully:

- Twelve matched comparisons: batches 1/65/257 × local sizes 16/32/64/128,
  eleven rounds each.
- Batch 4096/local 128, eleven rounds.
- Batch 257/local 128, 501 rounds with changing inputs; warm median
  26.478 ms. The process spans about 16.5 seconds including verification;
  this is a repeated-run check, not hours-long stress.
- 200 normal cross-context compute/native-fence chains, then 50 chains
  containing one deliberate CL-stall/reset probe. The compute fence is
  pending at uptime 219436.976846, before the kernel reset at 219438.390497.
- Eleven further neural rounds at each batch 1/65/257 after recovery,
  with local size 128 and the same driver.

Total: **677 neural inferences**, all layers exactly equal to the CPU
(maximum absolute error zero), all class checks correct and guards intact;
**250 synchronization chains**; exactly **one deliberate GPU reset**,
**zero unexpected resets, zero MMU errors and zero oopses**.
The pre-existing `drm_sched_entity_init: called with uninitialized
scheduler` open warning remains visible in the complete kernel log.

## Reproduction and boundary

Build `kstb-neural-test` with `make -C port/gpu-test`. On the board, after
selecting this package's environment with `kstb-mesa env`, the optimized
workload command is:

```sh
V3D_COMPUTE33=1 /opt/gpu-test/kstb-neural-test 257 128 11
```

Use local size 16 for the matching baseline. Defaults remain 65/16/1 to
preserve the original single-shot test invocation. `run.py` is the exact
bounded board runner, including its watchdog, fault monitoring and reset
probe. Run it only when no other GPU workload is active. `analyze.py`, run
from the repository root, checks and regenerates `summary.json` from the
archived raw logs. No workloads remain queued by this milestone.

This establishes a faster tested configuration for this specific workload.
It does not resolve the known attachment-free 127×127 framebuffer reset,
large indirect-compute timeouts, general framework support or GPU desktop
composition. No system-default promotion occurred. Work stops at this
milestone as requested; another milestone requires the user to resume.
