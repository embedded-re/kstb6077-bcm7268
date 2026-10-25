# CPU and GPU mathematics: measured limits on the KSTB6077

Audit and post-reboot FFT milestone: **2026-10-25**. The CPU can run general scientific software;
GPU compute currently provides a restricted FP32/integer shader accelerator,
and the measured dense-matrix implementation is slower than the four-core CPU.
An operation being possible does not mean the GPU accelerates it.

This is a milestone report, not a promise of optimal performance or a complete
inventory of every mathematical algorithm. New CPU execution checks and GPU
query/compiler checks are in the [audit evidence](../docs/evidence/gpu/runs/20261025-214500-math-capability-audit/README.md).
The initial audit left the user's Sway, Firefox and game sessions running
and used GPU query/compiler checks only. After the user rebooted, an isolated
native-driver FFT execution gate added the findings below. No driver changes,
package installs or deliberate resets were needed.

## Hardware and installed software

- Four online **Broadcom Brahma B53 ARMv8-A** CPU cores, 128-bit NEON SIMD.
  Linux reports implementer `0x42`, part `0x100`. The probe's
  `-mcpu=cortex-a53` is a compiler target, not the CPU's identification.
- **V3D 3.3**, eight QPUs and two slices, as reported by `v3d_ident`.
  Earlier suggestions of twelve QPUs do not describe this box.
- Nominal 2 GiB shared system memory; Linux reports 1,999,124 KiB usable
  (about 1,952 MiB). This audit's live snapshot had about 845 MiB available.
  There is no separate GPU VRAM and no swap configured.
- Kernel **#30**, `6.18.57-kstb-hrtimer-ab`; native **Mesa 23.3.6-r34**,
  inner commit `8b05308`, installed in `/usr`.
- Normal context: **GLES 3.0**. Per-process `V3D_COMPUTE33=1` exposes
  **GLES 3.1** using the custom V3D 3.3 compute path, without a version or
  library-path override. Compute remains opt-in.
- Actual operating clocks and physical peak throughput were not measured.
  BogoMIPS is not a CPU clock measurement.

## Operations and number formats

“Software” below means an implementation using supported instructions, with
extra time/memory. Lack of a native instruction does not make the mathematics
impossible. CPU feature flags describe kernel-exposed instructions; see the
[Linux HWCAP documentation](https://docs.kernel.org/arch/arm64/elf_hwcaps.html).

| Operation or format | CPU | Current GPU shader interface |
|---|---|---|
| FP32 add/subtract/multiply/divide | Native scalar and four-lane NEON; FMA executed successfully | Supported; division can be lowered to a sequence |
| FP64 / double precision | Native scalar and two-lane NEON; FMA executed successfully | No native/exposed shader FP64; compiler rejects it |
| FP128 / higher precision | Binary128 `long double` via software helper verified; arbitrary precision possible | No native format; custom multiword emulation would be new work |
| Integer arithmetic/bit operations | Scalar 8/16/32/64-bit, NEON integer operations; compiler 128-bit arithmetic checked | 32-bit signed/unsigned shader arithmetic and bit operations; no exposed int64 |
| FP16 / BF16 / matrix-dot accelerators | No FP16 arithmetic, BF16, I8MM or dot-product ISA features advertised; software conversion/math possible | No explicit FP16 arithmetic extension or tensor/matrix engine interface |
| Complex numbers | C complex FFT and Python complex arithmetic executed | Pairs of FP32 values; batched complex FFT128–2048 now executed |
| Atomics | 64-bit C atomic add/load executed, lock-free through exclusive-load/store instructions | Integer SSBO/image atomics covered by earlier tests; no float atomic-add extension |
| Parallelism | Four CPU cores, threads, NEON; no SVE | Workgroups, shared storage, workgroup barriers; no subgroup extension |
| sqrt / trig / exp / log / power | Software libraries plus available scalar instructions | Shader functions and compiler lowering/SFU sequences; numerical accuracy needs workload-specific checks |

`HWCAP_ATOMICS=0` means the CPU lacks the **LSE instruction extension**,
not that atomics are unavailable. Disassembly confirms `ldaxr`/`stlxr` for
64-bit atomics, FP32/FP64 `fmla`, and `__addtf3` for software binary128.
No CPU FP16/BF16 acceleration should be inferred from having NEON.
On the GPU, `mediump` does not establish a faster FP16 mode: fragment
low/medium/high float queries all report a 23-bit fraction and range 127.

FP32 offers roughly 6–7 significant decimal digits, FP64 15–16 and binary128
33–34. These are format capacities, not error bounds for an algorithm.
Ill-conditioned solves, long sums and iterative methods can lose many digits.
CPU default FP32 gradual underflow was checked. GPU denormals, special values,
transcendental error bounds and general IEEE equivalence were not established
by these checks. The driver's FP32 FMA lowering and different reduction orders
also mean CPU/GPU rounding need not match.

## Advanced computations you can use

**Demonstrated on the CPU in this audit:** FP64 complex FFT/inverse FFT with
65,536 samples; FP64 Cholesky factorization and solve of a 96×96 positive
definite system; FP32/FP64 NEON FMA; 128-bit integer arithmetic; software
binary128; 210-digit decimal arithmetic; 4096-bit modular exponentiation.
FFT spectrum error relative to N was `2.04e-15` or less, round-trip maximum
error `1.05e-12` or less; the solve residual was below `1.16e-15`.
These are small correctness examples, not guarantees for all input matrices.

The simple single-thread CPU FFT took about **60 ms forward / 73 ms inverse**,
and the small solve **0.92 ms**, with the desktop active. These are untuned
probe timings, not FFTW/BLAS performance estimates. FFTW single/double/long-double
libraries and development files are already installed; GMP/MPFR runtime
libraries are installed. OpenBLAS, NumPy and SciPy were found in the board's
package repositories but were not installed by this audit.

**CPU-feasible with appropriate software, not newly benchmarked here:** larger
FFT/DSP, least squares, eigenvalue/SVD problems, optimization, numerical
integration, ODE/PDE solvers, Monte Carlo, arbitrary-precision number theory,
and small neural networks. Practical size and runtime depend on the actual
algorithm, conditioning, precision and memory use. Large dense problems can
be expensive even when fully supported.

**Executed on this GPU:** the new FP32 FFT described below; previously,
FP32 matrix multiplication, a small MLP,
integer SSBO/image operations and atomics, shared-storage/barrier tests and
195 selected official compute CTS cases. That subset is not full GLES 3.1
conformance. The existing [compute history](gpu-compute.md) records dispatch,
synchronization and recovery work.

**GPU candidates requiring new kernels and validation:** FP32 convolution,
larger/multipass FFTs, reductions/scans, particle updates, finite-difference stencils,
Monte Carlo and other regular independent calculations. Their feasibility
follows from the available operations; those particular implementations,
performance and useful problem sizes have not been demonstrated on this box. Use separate dispatches
and memory barriers for global phases. Workgroup barriers cannot synchronize
all workgroups; global spin barriers can deadlock.

**Unavailable as a native GPU facility in this setup:** FP64, shader int64,
explicit FP16 arithmetic, floating atomic add, subgroup operations, recursive
shader functions, CUDA, and a ready-to-use GPU backend for standard
PyTorch/TensorFlow applications. No usable OpenCL GPU ICD or Vulkan GPU driver
is installed. This is a GLES shader API, not a drop-in CUDA device. Wide math
can be emulated and recursive algorithms can be rewritten iteratively, but
neither has a demonstrated performance case here. Shader language constraints
are described by the [GLSL ES 3.10 specification](https://registry.khronos.org/OpenGL/specs/es/3.1/GLSL_ES_Specification_3.10.pdf).

## Post-reboot execution: complex FP32 FFT

The [native r34 FFT milestone](../docs/evidence/gpu/runs/20261025-220836-fft-boundary/README.md)
ran after the user's reboot, with the machine idle and no desktop apps running.
A new workgroup FFT implementation passed lengths **128, 256, 512, 1024 and
2048**, with selected batches up to 64. The 2048-point case uses exactly
**16 KiB of shared storage**, establishing execution coverage at that limit,
rather than shader-link acceptance alone. Larger FFTs need another layout or
multiple passes; 2048 is this implementation's bound, not a universal FFT bound.

Forward output was checked against FFTW double precision for impulse, complex
tone, deterministic random and mixed inputs; GPU inverse output was checked
against the original input. Worst observed forward relative L2 error was
`1.581e-7`; worst inverse absolute error was `5.365e-7`. Twiddles are generated
on CPU, so these results do not validate GPU trigonometric instructions.

Warm elapsed medians for one batch of FP32 forward transforms:

| Length × batch | Best FFTW CPU threads tested | CPU, ms | GPU excluding upload/readback, ms | GPU including upload/readback/checks, ms |
|---|---:|---:|---:|---:|
| 128 × 1 | 1 | 0.001384 | 0.183444 | 1.987328 |
| 256 × 16 | 4 | 0.024822 | 0.638480 | 2.921067 |
| 1024 × 64 | 4 | **0.547457** | **6.131504** | **16.147629** |
| 2048 × 64 | 4 | **2.464082** | **11.066247** | **29.159080** |

CPU uses existing FFTW FP32 plans with one/four threads, excluding planning.
GPU excludes the cold compile/first round. The last column also includes
mapping, copy-back and guard inspection; it is not pure transfer latency.
The CPU is **11.2× faster** at 1024×64 and **4.49× faster** at 2048×64 even
against the upload/readback-excluded GPU interval. This is much more useful
than comparing the GPU against our initial untuned CPU correctness FFT.

All **132 FFT dispatches / 2544 individual forward-or-inverse transforms**
passed with intact buffer guards. After each of the three gates, 100 native
fence chains and MSAA/depth/stencil pixel checks passed. No reset or MMU fault
was observed; existing uninitialized-scheduler warnings remain. This is
bounded reliability coverage, not a long-duration or deliberate-reset test.
The test watchdogs/processes were stopped; the box remains at its manually
started-desktop state. Reproduce with `make -C linux/gpu-test fft`; the
[gate evidence](../docs/evidence/gpu/runs/20261025-220836-fft-boundary/README.md)
contains runtime commands, environment, timing scope and offline validation.

## Reported GPU resource limits

These are **current installed-driver API limits**, not a promise that maximum
allocations or enormous grids fit or finish usefully. The initial shader checks
confirmed front-end/link acceptance at the workgroup/shared-storage boundary
and rejection immediately above it. They performed **zero dispatches**;
acceptance is not new backend execution coverage.

| Resource | Limit |
|---|---:|
| Invocations per workgroup, product of X×Y×Z | **128** |
| Each local axis | 128, subject to the product limit |
| Workgroup counts per dispatch axis | 65,535 reported; maximum grid not stress-tested |
| Shared storage per workgroup | **16,384 bytes** |
| Shader storage blocks per compute shader / binding slots | **16 / 16** |
| One shader storage block range | **128 MiB** |
| SSBO offset alignment | 4 bytes |
| Compute image uniforms | **8** |
| Uniform blocks per compute shader / one block | 15 / 64 KiB |
| Compute uniform components | 16,384 |
| Compute atomic counters | 4,096 |
| 2D texture dimension / 3D dimension / array layers | 2,048 / 2,048 / 2,048 |

The aggregate `GL_MAX_IMAGE_UNITS=192` does **not** mean 192 images per
compute shader; that stage reports eight. A 128 MiB SSBO block limit is not
necessarily the largest whole buffer object; larger datasets need partitioning
or multiple bindings and still must fit real RAM.

V3D 3.3 has **no hardware CSD compute dispatcher**. Our Mesa launches compute
through render-only command lists, without a bin or vertex shader stage.
The current path divides work into at most 16 workgroups per render job,
uses buffer-backed shared storage, and shares the graphics render queue.
Do not assume CUDA-like on-chip shared SRAM or independent compute/desktop
engines. Long compute can interfere with GUI responsiveness.

The kernel scheduler has a **500 ms progress-watchdog interval**. It examines
command-list progress before deciding to reset, so this is not a universal
500 ms total-program time limit. Long individual shaders without observable
command-list progress can nevertheless risk recovery. Chunk long work;
no deliberate hang/maximum-duration test was run on the live desktop.

## Memory determines the practical problem size

Minimum storage for three dense square matrices A, B and C:

| Matrix dimension | FP32 total | FP64 total |
|---|---:|---:|
| 4,096 | 192 MiB | 384 MiB |
| 8,192 | 768 MiB | 1,536 MiB |
| 16,384 | 3,072 MiB | 6,144 MiB |

These totals exclude packing, library workspaces, staging, OS and apps. An
8,192-square FP32 matrix alone is 256 MiB, above one shader storage block's
range limit. The live desktop's available memory is not a reserved compute
budget. A 16,384-square three-matrix in-memory job does not fit this machine;
out-of-core methods are a different, usually slower algorithm. Allocation
failure/OOM thresholds were deliberately not searched on the user's session.

## Does the GPU save time?

The earlier isolated [GEMM/coprocessor decision](../docs/evidence/gpu/runs/20261025-151108-gemm-coprocessor/README.md)
provides the fair comparison. These are warm medians from the **33-patch
Mesa `85a951cd` debug build**, not a remeasurement of native r34. Patch 34
changes the vertex spill workaround rather than compute; this does not make
old timings a new measurement. Packing is counted for CPU, transfers listed
separately for GPU.

| FP32 square GEMM | Best measured four-core CPU | Best GPU, resident | GPU including transfers |
|---|---:|---:|---:|
| 128 | 0.402 ms | 2.324 ms | — |
| 256 | 5.530 ms | 14.533 ms | 19.282 ms |
| 512 | 39.102 ms | 108.676 ms | — |
| 1,024 | **318.703 ms** | **882.913 ms** | **960.437 ms** |

At 1,024, packed NEON reaches **6.738 GFLOPS** versus **2.432 GFLOPS** for
tiled resident GPU work: CPU wins by about **2.77×**. The GPU is capable of
useful math, but current dense GEMM is not a faster primary accelerator.
These are implementation measurements, not physical peak limits or a claim
that every GPU algorithm loses. Special binary-fraction matrix inputs passed
exact checks; that is not general ill-conditioned numerical validation.

Running independent GEMMs concurrently on CPU and GPU improved total
throughput by **11.25%** in the prior coprocessor trial. It missed the declared
20% threshold for further optimization. Lower process CPU time on GPU work
is a possible offload benefit, not proof of lower energy consumption or an
idle CPU system. GUI contention also matters for everyday use.

**Practical choice:** use CPU FP64/NEON and numerical libraries first for
advanced scientific mathematics. Keep the GPU for rendering and selectively
validated FP32 background tasks. A particular computation may justify a new
GPU kernel, but it needs a named workload and a measured advantage over the
best available CPU implementation. There is no justified promise of a speedup
for untested FFT layouts, simulations or neural networks.

## Reproduce and extend

Sources: [`kstb-cpu-math-test.c`](gpu-test/kstb-cpu-math-test.c),
[`kstb-gl-caps.c`](gpu-test/kstb-gl-caps.c),
[`kstb-math-shader-check.c`](gpu-test/kstb-math-shader-check.c).
CPU build: `make -C linux/gpu-test cpu-math` from the repository root;
copy `linux/build/gpu-test/kstb-cpu-math-test` to the board and execute it.
The audit evidence contains native-system GL build commands, hashes and logs.

The FFT milestone supplies one such library comparison. Further useful work
requires a chosen workload with requested precision, dimensions and accuracy.
This investigation stops at the verified FFT milestone. No ongoing benchmark or driver
change is needed to retain the findings.
