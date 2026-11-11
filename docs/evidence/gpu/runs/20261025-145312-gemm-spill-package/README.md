# Discarded-read spill boundary fix (2026-10-25)

Mesa patch 33 fixes the isolated compute spill corruption and the small
2×2-output tiled GEMM hang reproducer. The build is installed alongside
existing versions as **23.3.6-85a951cd**, not selected as the system default.

## Fault and fix

On pre-4.1 hardware, a texture-memory response arrives in accumulator r4.
The spill inserter waits until the following MOV consumes r4 before ending
the TMU sequence, protecting that value from a spill's own dummy read.
However, a dummy read can discard its response with no following MOV. The
old code never ended that sequence at its LDTMU signal. Its pending-spill
state then crossed basic-block boundaries.

The failing naive GEMM dump shows a conditional zero initialization in
BLOCK 1 postponed until BLOCK 6, inside the accumulation loop. Its spill
therefore overwrites the scratch sum on repeated iterations. This is a
barrier-free failure; it is not evidence that workgroup barriers themselves
are broken.

Patch 33 recognizes a discarded LDTMU response as a completed sequence.
When a following MOV consumes r4, it still waits for that MOV. No cache
flushes, thread counts, kernel scheduling or GPU command packets change.
The production patch leaves forced spilling disabled.

[Before-fix VIR/QPU dump](../20261025-144938-gemm-spill-compile-only/compile-naive.txt)
and [after-fix dump](../20261025-145043-gemm-spill-compile-only/compile-naive.txt)
were collected with a diagnostic driver that compiles then explicitly
returns before compute submission. The expected userspace job-count failure
in those logs is a compile-only guard, not a matrix correctness result.
The helper's `complete` state means that compilation-only gate succeeded.

## Execution gates

[Scratch-driver results](../20261025-145135-gemm-spill-fixed/) use the fix
plus the existing forced-spill hook at minimum allocations of one/four
slots for compute. Naive N=32/65 and shared-memory N=8/32/64 pass five full
matrix/guard rounds each, at each setting: **50 exact rounds**. Shader stats
confirm nonzero spills. The normal fixed driver then passes the originally
hanging tiled2 N=32/64, five rounds each, 100 native-fence chains and 11 exact
batch-257 neural rounds.

The numbered production package passes another **55 exact GEMM rounds**
(naive N=32/65, shared1 N=64, tiled2 N=32/64, eleven each), **22 neural rounds**
(batch 257 and 4096), **100 native-fence chains**, **195/195 compute CTS** and
**200/200 focused graphics texture/spill CTS**. All CTS statuses are Pass.

Totals: **115 exact GEMM rounds, 33 neural rounds, 200 fence chains and
395 CTS passes**, with **zero unexpected resets, MMU errors or oopses**.
`analyze.py` verifies these totals from archived logs, writing `summary.json`.
The tests use the real exposed GLES version, no API override or shader cache.
Each stage is monitored against kernel faults with a hardware watchdog;
no GPU workloads overlap.

The tiled2 shader changes from 1,071 instructions and 84 spills/109 fills
to 415 instructions and 8 spills/17 fills on the scratch fixed build.
Both fixed N=32/64 cases are exact; reduced spilling is recorded compiler
output, not a measurement of a new large-matrix throughput ceiling.

## Artifacts and limits

`BUILD-ID` and `meta.json` identify the packaged DRI:
`18969788882886e842a68acba6818561eb9da09345e3586381c35d3f841f04e1`.
The inner Mesa commit is `3263da5`; `make -C port/mesa check` and the full
package build pass. Production source contains no compile-only or forced-
spill instrumentation. Existing patch files 1–32 only gain the updated
series length in their subject headers when patch 33 is exported.

`tested-gemm-source.c` preserves the exact source corresponding to the
board's GEMM executable. The tracked source folds equal tile dimensions
back into one variable; shader strings and checked computations agree.

Kernel #30, selected Mesa `23.3.6-fe7dda5e` and `/usr/local` remain unchanged.
The new package is selected per application with `kstb-mesa env
23.3.6-85a951cd` and `V3D_COMPUTE33=1`. No default promotion was performed.
No workload or userspace watchdog remains active at the end; see
`final-board-state.txt`.

This is a bounded correctness/reliability gate. It does not establish full
GLES 3.1 support, long-run/reset stress reliability, 4×4-output shader
support, or 1024×1024 GEMM performance against NEON. Existing framebuffer
and large-dispatch blockers remain outside this milestone. Sway is still
using its CPU compositor. Work stops after this milestone.
