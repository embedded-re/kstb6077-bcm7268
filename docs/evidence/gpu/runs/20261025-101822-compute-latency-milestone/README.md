# Compute latency milestone: queue accounting identifies cache-clean delay

Completed on unchanged kernel #29 / Mesa `23.3.6-5a3fd8dd` (patches 1–32),
with compute opt-in and no API override. No kernel or Mesa driver changes,
flashing, default selection changes or IRQ-affinity changes occurred.

The new `kstb-compute-latency-test` varies groups, dependent integer shader
loops and dispatch count. It checks exact output and guards after every
round, or unchanged buffers for the empty shader. There is one CPU wait
at the end of each round, with barriers between queued dispatches. Each
workgroup contains 128 invocations. Every round checks the kernel's
completed render/cache-clean job counts and confirms zero bin jobs.

`/proc/self/fdinfo` provides per-file queue runtime and completed counts.
`v3d_job_start_stats` starts the render interval immediately before perfmon
switching, MMU flush and hardware CL launch; `v3d_irq_signal_fence` ends it
at the render IRQ. The cache-clean interval wraps `v3d_clean_caches` in
`v3d_cache_clean_job_run`. These are **elapsed kernel intervals**, including
any sleep, lock wait or preemption in those regions. They are not hardware
cycle counters or isolated arithmetic timings. Queue counters are read
outside the host timing interval.

## Latency and scaling results

Round zero is cold and excluded below. All values are warm medians in ms.
Each metric has its own median; columns need not add exactly.

| Probe | Completed render jobs per round | Host wall | Render queue | Cache-clean routine |
| --- | ---: | ---: | ---: | ---: |
| Empty shader, one group; 1,000 warm rounds | 1 | 2.101 | 0.007 | 1.987 |
| Store shader, one group; 1,000 warm rounds | 1 | 2.101 | 0.008 | 1.983 |
| One dispatch, 16 groups | 1 | 1.660 | 0.014 | 1.479 |
| One dispatch, 64 groups | 4 | 12.325 | 0.077 | 12.018 |
| One dispatch, 256 groups | 16 | 62.919 | 0.243 | 62.277 |
| One dispatch, 1,024 groups | 64 | 253.203 | 1.037 | 251.031 |
| 64 dispatches of one group, one final wait | 64 | 253.619 | 0.597 | 251.961 |

Thus the delay is present with no shader arithmetic. It grows with the
number of render/cache-clean jobs, including when queued before one wait.
The 64-job run records 251.031 ms in the cache-clean routine, compared
with 253.203 ms host wall time and 1.037 ms in the render queue.

Keeping one job and 2,048 invocations, varying the dependent uint LCG
loop count from 0/16/64/256/1,024/4,096 increases median render time from
0.014/0.032/0.085/0.301/1.161/4.601 ms. The least-squares render fit is
`0.013972 + 0.00111989 * loops` ms. These are integer operations, not an
FP32 throughput benchmark. Output is checked with independently composed
CPU affine transforms. Short host wall times remain around 1.66 ms through
1,024 loops, while time moves from the cache-clean interval to rendering;
at 4,096 loops host wall rises to 5.658 ms.

## Actual neural workload

The companion [neural queue-accounting run](../20261025-102029-neural-queue-accounting/README.md)
uses the same driver and opt-in flag, changes inputs each round and reads
queue counters outside timing. Ten warm rounds follow a separate cold round.

| Batch / local size | Completed render jobs | Host wall ms | Render queue ms | Cache-clean routine ms | CPU reference ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| 257 / 16 | 26 | 114.136 | 0.640 | 112.873 | 0.403 |
| 257 / 128 | 5 | 26.355 | 0.427 | 25.661 | 0.398 |
| 1,024 / 128 | 12 | 48.230 | 1.568 | 46.141 | 1.603 |
| 4,096 / 128 | 48 | 190.500 | 6.294 | 182.934 | 6.527 |

The prior claim of four jobs for batch 257/local 128 was wrong: the layers
require 33 and 17 workgroups, hence three plus two jobs. The counters verify
**five**, without changing the measured 4.32× improvement from the earlier
workgroup-sizing milestone. The original evidence README is corrected.

## Platform checks and limits

- `CONFIG_HIGH_RES_TIMERS` and `CONFIG_FTRACE` are disabled in the running
  kernel; `CONFIG_HZ=250`. The selected settings are copied directly from
  `/proc/config.gz` into `kernel-config-selected.txt`. There is no tracefs
  entry in `/proc/filesystems`, nor a v3d event directory, so the requested
  ftrace timeline was unavailable without a kernel replacement.
- Kernel `v3d_clean_caches` polls pending L2 flush, TMU write-combiner flush
  and final L2 clean through `wait_for`. Its polling macro calls
  `usleep_range`, starting at 10–20 microseconds. Both code excerpts are
  retained in `kernel-polling-source.txt`.
- The companion run measures user-requested sleeps of 10, 100 and 1,000
  microseconds: each has a median of 3.998 ms over 101 samples. This is a
  userspace sleep calibration, not direct instrumentation of kernel polls.
  No timer-configuration A/B was performed and individual poll sites were
  not traced; the data does not prove which poll or sleep causes each delay.
- GPU runtime PM reports `unsupported`, with active/suspended times both
  zero. A reversible `auto` → `on` → original `auto` comparison leaves
  one-job store wall medians at 1.660/1.661/1.660 ms and cache-clean medians
  at 1.479/1.479/1.478 ms. `power-before.txt` and `power-after.txt` match.
- The only `clk_summary` entry is our enabled `v3d-power` gate at nominal
  rate zero. The provider has no rate-reporting operation. **GPU frequency
  remains unmeasured**; no clock rate was changed.
- During the main gate the core IRQ increases by exactly **14,759**,
  all on CPU0, matching the render-job total across timing, synchronization
  and final neural checks. The hub IRQ does not increase. This checks
  delivery/counts; it does not measure individual IRQ-service latency.
- The compute path already bypasses the binner and uses one 64×32 tile,
  loads its ID buffer and disables framebuffer stores. No render-target
  size sweep was performed: changing it would alter the compute ID layout,
  rather than resize an ordinary dummy framebuffer.

## Reliability and reproduction

All **26 main stages** and **eight companion neural stages** pass:
2,776 timing rounds / 7,860 render jobs plus the same number of cache-clean
jobs; 200 cross-context compute/native-fence chains; and 121 neural
inferences (33 after the main probes, 88 with queue accounting). All expected
outputs/guards pass, and all neural layers match CPU exactly. There are
**zero GPU resets, MMU errors or oopses**. The pre-existing absent-CSD
scheduler warning remains logged on context creation.

`run.py` is the exact watchdog-monitored, fault-stopping board runner;
`meta.json` records driver/executable hashes. Build with
`make -C port/gpu-test`; after setting the numbered Mesa environment:

```sh
V3D_COMPUTE33=1 /opt/gpu-test/kstb-compute-latency-test 1 0 1001 1 empty
V3D_COMPUTE33=1 /opt/gpu-test/kstb-compute-latency-test 1024 0 31 1 store
KSTB_NEURAL_STATS=1 V3D_COMPUTE33=1 /opt/gpu-test/kstb-neural-test 257 128 11
```

Run `python3 analyze.py` from this directory to validate raw-log counts,
IRQ deltas and unchanged PM state, and regenerate `summary.json`.

This milestone ends with measurement, not a kernel fix. Work is stopped;
no GPU tests remain queued. The known attachment-free framebuffer reset
and broader full-stack reliability work remain open.
