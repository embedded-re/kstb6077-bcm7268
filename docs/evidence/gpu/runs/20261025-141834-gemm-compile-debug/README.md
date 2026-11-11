# GEMM milestone stopped on a GPU reset (2026-10-25)

This milestone did **not** complete. No 1024×1024 performance comparison or
post-GEMM fence/recovery gate ran. The monitored runner stopped on the first
unexpected reset. No GPU workload or userspace watchdog remains running;
see `final-board-state.txt`. Board SSH remained responsive after the reset;
post-reset GPU correctness was not tested.

The stack remains installed kernel #30 (`6.18.57-kstb-hrtimer-ab`), selected
Mesa `23.3.6-fe7dda5e`, experimental per-process compute `23.3.6-5a3fd8dd`
with `V3D_COMPUTE33=1`. No kernel/driver/default changes were made. Hardware
identification reports V3D 3.3.1.0, two slices, **eight QPUs and two TMUs**;
actual clock frequency remains unmeasured.

## Outcomes

| Probe | Observed result |
|---|---|
| Initial scalar, four-worker NEON mode, naive shader at N=1 | Two exact rounds each, guards intact. N=1 uses CPU tails, so this does not validate the NEON microkernel. |
| Initial bounded shared-memory shader, 4×4 outputs/thread | Register allocation failed; no shader execution. |
| Aligned 4×4 outputs/thread, N=32 | Register allocation failed; no shader execution. |
| Aligned 2×2 outputs/thread, N=32 | Compiler selected two threads, 1,071 instructions, 32 max temporaries, 84 spills / 109 fills. Execution produced one unexpected GPU reset; no output correctness pass. |

Kernel evidence: `Resetting GPU for hang`, `V3D_ERR_STAT: 0x00001000`.
The userspace render-job-count assertion also failed. Shared-memory/barrier
use and spilled shader execution are both present in the failing variant;
this run does not isolate which causes the hang. Compiler logs are not a
measurement of arithmetic throughput.

## Benchmark implementation

`port/gpu-test/kstb-gemm-test.c` provides naive FP32 GEMM, aligned shared-
memory tiled 4×4 and 2×2 output blocks per invocation, scalar blocked 4×4
CPU GEMM and four persistent NEON workers. `tiled` aliases `tiled4`.
**Tiled modes are failing diagnostic probes on this stack; do not run them
without a monitored watchdog/reset investigation.**

For each round it computes C=A×B+bias using deterministic binary-fraction
inputs, checks every result exactly against an independent scalar dot-
product reference and checks 4 KiB guards on each end of C. Round-dependent
bias detects stale results. Compilation, upload, reference and verification
are outside timed intervals; dispatch+wait and CPU C initialization/worker
barriers are included. First round is cold, remaining rounds form the warm
median. GFLOPS counts 2N³ operations. Process CPU time and DRM queue elapsed
times are separate; queue elapsed times are not pure shader execution.

Tiled4 uses local 8×8, 32×32 output tiles and K=16, 4 KiB shared storage.
Tiled2 uses local 8×8, 16×16 output tiles and K=16, 2 KiB shared storage.
The existing CL compute driver packs 16 workgroups per render job.

`build.txt` records compiler options and ELF/source hashes. Automatic CPU
vectorization is disabled; `cpu-microkernels.dis` preserves scalar/NEON
assembly. Larger CPU correctness checks remain pending. The board ELF hash
is also in `meta.json`. `tested-source.c` is the source for this final run.

## Evidence sequence

- [Initial bounded shader and N=1 checks](../20261025-141220-gemm-milestone/)
- [Initial shader register-allocation diagnostic](../20261025-141439-gemm-compile-debug/)
- [Aligned 4×4 register-allocation diagnostic](../20261025-141605-gemm-compile-debug/)
- This directory: 2×2 diagnostic and reset, complete compiler log,
  kernel log, runner, hardware identification and final process state.

Do not infer GPU-versus-NEON speed, peak utilization, or production compute
reliability from this milestone. Full GEMM and synchronization validation
remain pending. Sway still uses the CPU compositor; no compositor work was
performed here. Work is stopped at this failed reliability gate.
