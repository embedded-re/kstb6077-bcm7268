# Compute spilling fails without shared memory or barriers (2026-10-25)

Stopped on the first incorrect-result probe. The naive GEMM shader passes
without spills, then fails with a temporary compiler forced-spill hook.
There are **zero new GPU resets, MMU errors or oopses** across this milestone.
This isolates a compute-spilling correctness failure independent of shared
memory/control barriers. It does not establish the cause of the earlier
heavy-spill tiled shader hang or identify a compiler fix.

## Controlled observations

All GEMM checks use the same deterministic A/B generation, full independent
reference, changing round bias and buffer guards. N is the square dimension.

| Driver / shader | Test | Result |
|---|---|---|
| Installed experimental package / naive | N=32, 3 rounds | Exact pass; zero spills/fills |
| Installed experimental package / shared1 | N=8,32,64, 3 rounds each | Exact pass; two threads, 23 max temporaries, zero spills/fills |
| Temporary force-one-spill driver / naive | N=32, first round | Two threads, 165 instructions, 13 max temporaries, 2 spills / 2 fills; 1,022 incorrect outputs out of 1,024 |

For the failed probe, output 0 is 0.0009765625 (the bias) rather than
-0.331054688. The full log records more examples and the error count.
The naive shader has no shared declaration or barrier; both are absent in
`tested-source.c`'s `naive_shader`. No output comparison is skipped.

Before these probes, three batch-257/local-128 neural rounds passed exact
hidden/logit/guard checks. This also supplies GPU correctness evidence after
the previous milestone's reset. The planned final fence gate and neural
check **did not run**, because the forced-spill result failed first.

## Shared-memory control

Added `shared1` mode to `linux/gpu-test/kstb-gemm-test.c`: local 8×8,
one output per invocation, 8×8 A/B tiles (512 bytes shared total), K block 8,
two workgroup barriers per block. It checks cross-invocation shared values
through complete GEMM results, including reuse across K blocks and multiple
workgroups/jobs. At N=64 the warm median is 1.182 ms / 0.444 GFLOPS in two
warm rounds. This small diagnostic is not the requested large GEMM study.
The control has 125 instructions, 23 max temporaries and no spilling.

## Temporary compiler instrumentation

Mesa source base is `e774a1202f` (the existing 32-patch series). The only
scratch change is `force1.patch`: the compiler's existing
`force_register_spills` constant is 1 for compute, 0 otherwise. This requests
a minimum spill allocation; the resulting naive shader reports 2 spill
and 2 fill instructions. No shader source or kernel change accompanies it.
The scratch driver is selected per child with
`LIBGL_DRIVERS_PATH=/tmp/kstb-gemm-force1`; `run.py` records the exact call.

`meta.json` hashes the test, original package and scratch drivers. The
force-one DRI SHA256 is
`3815711f5dce46f73bdb96fc66155f8e1b27751063802b6b347d84e19f8e5f8e`.
A force-four driver was built/prepared and hashed, but **never executed**.
The inner Mesa source was restored clean and its ordinary driver rebuilt;
no instrumentation was exported into the production patch series.

## Other diagnostic attempts and evidence

- [Baseline, shared control and wide32](../20261025-143152-gemm-isolation/):
  naive/shared/neural successes above; wide32 no-shared pressure kernel
  fails compilation before submission.
- [Wide24](../20261025-143236-gemm-isolation/): reduced no-shared pressure
  kernel also fails register allocation before submission. No GPU fault.
- This directory: forced-spill failure, kernel log, exact runner, patches,
  tested C snapshot, metadata and final process/kernel state.

Wide24/wide32 were attempts to obtain natural spills, not successful
controls; they are preserved as source snapshots rather than exposed in
final benchmark CLI. Final source removes their unused modes and folds the
equal tile dimensions back into one variable; the tested snapshot preserves
the actual source used for this run.

Installed kernel #30, selected `23.3.6-fe7dda5e`, `/usr/local` and packaged
compute DRI are unchanged. Board remains responsive with no GPU workload
or userspace watchdog running. Sway and 1024×1024 throughput are untested in
this milestone. Further spill investigation requires a new monitored gate;
this milestone ends at the first correctness failure.
