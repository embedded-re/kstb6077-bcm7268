# Post-reboot complex FFT: native r34 execution and shared-memory boundary

2026-10-25, kernel #30; same native `/usr` Mesa23.3.6-r34 (`8b05308`).
Board rebooted by user, boot ID `3c548c6c-e2ce-4629-b9de-fc863743c1cb`.
No desktop, Firefox or game was running. About 1.84 GiB available RAM after
boot. No drivers/packages/configuration were changed; no reset was induced.

This milestone combines three sequential gates:

- [Smoke](../20261025-220730-fft-smoke/): one 128-point transform.
- [Sizes/batching](../20261025-220755-fft-full/): N128–1024, batches1–64.
- This directory: N2048, batches1/16/64, exercising **all 16,384 shared bytes**.

All gates completed. **132 FFT dispatches / 2,544 individual forward or
inverse transforms**, including cold rounds. Each gate then passed 100 native
fence export/import/server-wait chains and an MSAA/depth/stencil pixel test.
No GPU reset, MMU fault, kernel crash or buffer-guard failure was observed.
The existing `drm_sched_entity_init: called with uninitialized scheduler`
warning occurs in these logs; this is not a warning-free-kernel claim.
`post-check.json` confirms zero resets since reboot, unchanged driver hash,
and no watchdog/test/desktop processes remaining.

## Implementation and correctness

Source [`kstb-fft-test.c`](kstb-fft-test.c) is also tracked in
[`port/gpu-test`](../../../../../port/gpu-test/kstb-fft-test.c).
One 128-invocation workgroup per transform; complex values are pairs of FP32.
Radix-2 butterflies use bit-reversed input and workgroup barriers. Twiddles
are computed on the CPU and uploaded once. This does **not** test GPU trig
accuracy. N2048 requires shared `vec2[2048]`, exactly16KiB. The implementation
stops there; larger FFTs are mathematically possible with different algorithms
and multiple passes, not forbidden by a universal 2048-point FFT limit.

FFTW double precision supplies an independent reference from identical FP32
input values. Four input classes are exercised across rounds and batches:
impulse, complex sinusoid, deterministic pseudorandom and mixed DC/tone/noise.
Every output is checked for finiteness, relative L2 error and maximum absolute
error. Forward tolerance is relative L2 `<3e-5` and max-absolute
`<3e-5*(1+peak_reference)`; inverse absolute error must be `<3e-5`.
Both payloads have 4KiB prefix/suffix guards checked after every result.
Forward output feeds the inverse on GPU; inverse is normalized by N and
checked against the original input.

Worst recorded GPU forward relative L2 error: **1.581e-7** or less.
Worst inverse maximum absolute error: **5.365e-7** or less.
This validates these FP32 FFT sizes/input classes, not arbitrary shader
precision, every possible input magnitude, denormals or ill-conditioned math.

## Warm elapsed medians

Five warm rounds after one excluded cold round. CPU columns use FFTW FP32
`plan_many_dft`, FFTW_ESTIMATE, one or four threads, averaging32 executions
inside each timing sample. GPU samples contain one forward dispatch each.
Planning, shader compile cold round, reference generation and inverse checking
are excluded. Both sides use preallocated buffers and plans; one-time twiddle
setup is excluded. These are implementations, not physical hardware limits.

| Transform length × batch | FFTW one thread, ms | FFTW four threads, ms | GPU excluding upload/readback, ms | GPU upload/dispatch/readback/checks, ms |
|---|---:|---:|---:|---:|
| 128 × 1 | 0.001384 | 0.018031 | 0.183444 | 1.987328 |
| 128 × 16 | 0.019230 | 0.014593 | 0.559443 | 2.611290 |
| 256 × 1 | 0.003266 | 0.018876 | 0.192407 | 2.009884 |
| 256 × 16 | 0.055768 | 0.024822 | 0.638480 | 2.921067 |
| 512 × 16 | 0.131334 | 0.046779 | 0.961923 | 3.779028 |
| 1024 × 1 | 0.017568 | 0.023518 | 0.261222 | 2.174624 |
| 1024 × 16 | 0.321511 | 0.091618 | 1.723847 | 5.537950 |
| 1024 × 64 | 1.845510 | 0.547457 | 6.131504 | 16.147629 |
| 2048 × 1 | 0.042562 | 0.029243 | 0.366444 | 2.410405 |
| 2048 × 16 | 0.711510 | 0.193315 | 3.022590 | 8.845991 |
| 2048 × 64 | 6.073568 | 2.464082 | 11.066247 | 29.159080 |

GPU compute timing includes dispatch, memory barrier and `glFinish`: the
wait includes execution. Inputs are uploaded immediately beforehand; this is
an upload-excluded interval, not proof of an indefinitely resident pipeline.
The last column additionally includes input upload, mapping/copy-back and
readback guard inspection; it is not pure transfer latency. The CPU forward
output checks occur outside its timed interval. GPU loses even before these
extra costs: **11.2×** versus four-thread FFTW at1024×64 and **4.49×** at2048×64.
Small single-transform CPU cases benefit from using one thread.
No CPU/GPU overlap or energy measurements were made.

## Reproduction and safety

`make -C port/gpu-test fft` from repo root; requires the existing cross
sysroot FFTW headers/libraries and staged system Mesa link libraries. Binary
has no RPATH/RUNPATH (`elf-dynamic.txt`); runtime is the installed system Mesa.
FFTW installed-package versions are in `post-check.json`. `state.json` records
binary/driver hashes and environment. Per-process `V3D_COMPUTE33=1` only;
shader cache disabled, no driver/library/version override.

Archived `run.py` executes cases sequentially with a90-second process limit,
kernel-log fault guard and an independent foreground Linux watchdog. Only
new, validated test process groups are attached/signalled. First fault or
nonzero result stops the gate; watchdog is disarmed on exit. It refuses an
existing watchdog or known active desktop. Run on the board after copying
the FFT binary into `/root/math-audit/` and with existing `/opt/gpu-test`
helpers in place: `python3 run.py boundary`. The smaller gates retain their
original sources capped at1024; the only C source change before this gate
was raising the accepted maximum to2048.

Offline: `python3 analyze.py` validates all three archives and their manifests.
