# Compute value decision: prioritize rendering (2026-10-25)

The user requested a measured decision on continuing GPU compute versus
focusing GPU work on rendering or background coprocessing. The declared
cutoff was a **1.2× speedup** on useful work, or a clear CPU-offload benefit.
The completed decision tool returns **`prioritize_rendering`**. Neither
single-job acceleration nor the tested coprocessor batch reaches 1.2×.

## Single-job measurements

All values below are warm elapsed medians in milliseconds. CPU selection is
the fastest measured baseline for each size, including packed and unpacked
four-thread NEON. GPU selection is the fastest of naive, tiled2 and tiled4.
Packing costs are included each round; GPU transfers are measured separately.

| Square FP32 matrix | CPU | GPU, data resident | GPU, upload/compute/copy-back |
|---|---:|---:|---:|
| 128×128 | 0.402 | 2.324 | — |
| 256×256 | 5.530 | 14.533 | 19.282 |
| 512×512 | 39.102 | 108.676 | — |
| 1024×1024 | 318.703 | 882.913 | 960.437 |

At 1024, four-thread packed NEON reaches **6.738 GFLOPS**, versus **2.432
GFLOPS** for resident tiled2 and **2.236 GFLOPS** including transfers. The
single-thread packed NEON result is 927.501 ms / 2.315 GFLOPS. Scalar is
3711.444 ms / 0.579 GFLOPS. The naive GPU is 2895.964 ms / 0.742 GFLOPS;
4×4-output tiled GPU is 1090.945 ms / 1.968 GFLOPS and passes correctness.

The earlier unpacked four-thread NEON baseline takes 1990.160 ms at 1024.
Using it alone would incorrectly qualify the GPU against our cutoff. The
packed baseline includes per-round conversion into K-by-four column panels.
At 256, unpacked four-thread NEON happens to be faster and is selected in
the table. These are measured implementations, not a claim of optimal BLAS
or physical peak throughput.

The GPU charges less process CPU time: at 1024, median CPU time is 121.050 ms
including transfers versus 1085.182 aggregate worker CPU ms for packed
four-thread NEON. GPU resident CPU time is 48.716 ms. Process CPU time
excludes work charged to separate kernel workers; it is not whole-system
energy or CPU utilization. Actual concurrent throughput is tested below.

## Coprocessor trial

Each trial completes **sixteen independent 1024×1024 GEMMs**. CPU-only runs
all sixteen on four NEON workers. Hybrid runs twelve CPU jobs concurrently
with four GPU tiled2 jobs, including GPU input uploads and copy-back. Each
process checks a cold round, then waits on a FIFO. Timing starts when both
ready processes are released, excluding setup, reference calculation and
cold compilation. It includes all sixteen timed jobs, result verification
and process cleanup. Inputs are deterministic and reused across jobs;
round-dependent bias changes outputs. The two processes own separate inputs,
outputs and GPU objects. Only one GPU workload runs at once.

| Trial | CPU-only seconds | CPU + GPU seconds | Throughput ratio |
|---|---:|---:|---:|
| 0 | 5.531 | 4.972 | 1.1125× |
| 1 | 5.481 | 4.929 | 1.1121× |
| 2 | 5.485 | 4.895 | 1.1205× |

Median paired speedup is **1.1125×**, an **11.25% throughput increase**,
below the predeclared 20% requirement. Trial order alternates to reduce
order bias. This fixed split is an application-level throughput experiment,
not a search over every possible workload balance or GPU kernel.

## Correctness and reliability

All three monitored gates complete, with **288 exact matrix rounds**
(**81 on the GPU**, including 1024×1024 and both tiled shapes), **22 exact
neural rounds** and **230 native-fence chains**. There are **zero GPU resets,
MMU errors or oopses**. Every matrix entry is checked against an independent
reference; 4 KiB guards at both ends of C are checked every round. Binary-
fraction inputs make exact FP32 equality valid for these dimensions.

Initial shader compilation/upload, reference generation and verification
are excluded from the isolated round timings. Transfer mode includes new
A/B uploads and output map/copy/unmap inside each timed round. Cold results
are reported but excluded from medians. CPU workers persist across rounds;
thread creation is excluded. Automatic vectorization is disabled for the
scalar baseline; explicit NEON FMA instructions remain. `cpu-microkernels.dis`
records the final generated CPU kernels. Queue elapsed times include real
GPU work and scheduling; final waits are not treated as pure overhead.

## Reproduction and artifacts

- [Initial CPU/GPU/transfer gate](../20261025-150359-gemm-value/): scalar,
  unpacked one/four-thread NEON, naive/tiled2/tiled4, dimensions 128–1024,
  tail checks, fence and neural checks; source/build hashes preserved.
- [Packed CPU gate](../20261025-150652-gemm-value-packed/): packed one/four-
  thread NEON, transfer-mode validation and another thirty fence chains.
- This directory: three paired trials, child logs, `coprocessor.py`, the
  monitored `run.py`, exact tested C source, hashes and stopped-board state.

Run the decision analysis without GPU work:

```
port/gpu-test/kstb-compute-value \
  docs/evidence/gpu/runs/20261025-150359-gemm-value \
  docs/evidence/gpu/runs/20261025-150652-gemm-value-packed \
  docs/evidence/gpu/runs/20261025-151108-gemm-coprocessor \
  --output /tmp/compute-decision.json
```

The tool checks gate completion, kernel logs, exact-result checks and driver
identity, then emits the comparison and verdict. Its board and host outputs
match byte for byte. `decision.json` preserves this run's output. The same
helper is installed under `/opt/gpu-test/kstb-compute-value`.

All measurements use kernel #30 and per-process Mesa **23.3.6-85a951cd**, DRI
SHA256 `18969788882886e842a68acba6818561eb9da09345e3586381c35d3f841f04e1`,
`V3D_COMPUTE33=1`, no API override, shader cache disabled. Selected Mesa
fe7dda5e and `/usr/local` are unchanged. No driver/kernel changes occur in
this milestone. No GPU tests or userspace watchdog remain running.

## Scope of the decision

The verdict applies to this driver and these FP32 GEMM/pool workloads.
Other kernels, energy use and rendering are not measured. The existing
small neural test is a correctness gate here, not a framework benchmark.
Sway GPU acceleration is still unverified. The user requested dropping
active compute work if it did not justify itself; compute optimization is
therefore parked, with the working opt-in packages and fixes preserved.
The next GPU work priority is rendering/compositor buffer sharing. This
milestone stops after the measured decision.
