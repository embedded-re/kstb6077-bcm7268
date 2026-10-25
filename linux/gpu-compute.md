# GPU: TMU writes, SSBOs and compute on V3D 3.3

Work toward GLES 3.1 (SSBOs, images, compute) on the KSTB6077's V3D 3.3.
Only results that ran on the box are here; the stack itself is in
[`gpu.md`](gpu.md).

## TMU writes are not visible to the CPU after the job (2026-10-22)

`kstb-tmu-write-test [W H]` ([`gpu-test/`](gpu-test)) draws one fragment per
pixel. Each fragment stores `seed ^ (i * 2654435761)` into word `i` of an
SSBO that the CPU filled with `0xdeadbeef`. The CPU then reads the buffer
right after the draw (`glMemoryBarrier`, `glFinish`, map), and again after
one more job (a clear) that writes nothing through the TMU.

Kernel `#28` reported no cache flush on 3.3 (kernel patch 0003), so the
installed Mesa had 0 SSBOs. The test ran on a scratch driver from
`23.3.6-848c0204` that reports SSBOs anyway and does not set
`DRM_V3D_SUBMIT_CL_FLUSH_CACHE`, with `MESA_GLES_VERSION_OVERRIDE=3.1`.

| Fragment shader | After the draw | After the clear |
| --- | --- | --- |
| 4 threads (Mesa's default) | 4096/4096 `0xdeadbeef`; the job hangs, GPU reset (`V3D_ERR_STAT` 0x1000) | same |
| 1 thread (scratch switch in `vir_compile_init`) | 4096/4096 `0xdeadbeef` | 4096 exact; next run 4032 exact + 64 words of the **previous run's** data at word 3840 |
| 1 thread, 256x256 | 65536/65536 `0xdeadbeef` | 65472 exact, 64 words `0xdeadbeef` at 64704 (row 252, x 192–255) |

Each line ran 1–4 times with the same result. The GPU was healthy afterward
(`kstb-cube-seam-test` gave correct results, no further resets).

- The data stays on the GPU side after the job. The CPU sees none of it
  until a later job starts, where `v3d_invalidate_caches()` flushes L2T.
- That flush does not drain everything: the last 256 bytes (64 words)
  stay behind. In the second run they landed in a buffer of the next
  process, written after the first process had exited and freed its
  buffer. So on kernel `#28`, TMU writes could reach memory after their
  buffer was freed.
- The store sequence Mesa emits is `mov tmud` / `add tmua` / `tmuwt`, with
  no config uniform (32-bit, per pixel, default op). It hangs in a
  4-thread fragment shader and completes in a 1-thread one. The same
  thread split is in [`gpu.md`](gpu.md) "Open": threaded spills hang.

## Cleaning after the job: kernel patch 0005 (2026-10-22)

[`patches/0005-v3d-cache-clean-after-render-jobs-on-3.3.patch`](patches/0005-v3d-cache-clean-after-render-jobs-on-3.3.patch)
creates the cache-clean queue on 3.3 too (mainline makes it only with a
CSD), reports `DRM_V3D_PARAM_SUPPORTS_CACHE_FLUSH` = 1 and accepts
`DRM_V3D_SUBMIT_CL_FLUSH_CACHE` again (0003 had refused it). A render job
with the flag is followed by `v3d_clean_caches()`: `L2TCACTL` = `TMUWCF`
(drain the TMU write combiner), then `L2TFLS | FLM_CLEAN`. These are the
same writes the closed driver's `BVC5_P_HardwareCoreClean` makes.

Kernel `#29`, flashed to `KRN`/`DTB` (kmon `linux info`: `linux-6.18.57-kstb
2026-10-22`, CRC `335f53c0`), started with kmon's `linux`.

- `kstb-tmu-write-test` (1-thread scratch driver; Mesa sets the flag now
  that the kernel reports the flush): **exact right after the draw**, with
  no old or stray words, in 8 runs: 64x64 three times, 256x256 twice,
  17x13, 1x1 and 1920x1080 (2,073,600 words). There were no kernel messages
  and no resets. `gpu_stats` counted one `cache_clean` job per run.
- Full runs on `23.3.6-848c0204` (which now also sets the flag when a
  shader writes through the TMU): GLES3 43,908 Pass, GLES2 17,041 Pass,
  EGL 833 Pass. All have 0 MMU errors, 0 resets and 0 oopses, and match
  the kernel `#28` runs case by case (0 of 65,722 results differ). GLES3
  ran 3 `cache_clean` jobs; GLES2 and EGL ran none.
  Raw kernel `#29` evidence:
  [GLES3](../docs/evidence/gpu/runs/20261022-220306-gles3-848c-krn29-full/summary.txt),
  [GLES2](../docs/evidence/gpu/runs/20261022-225849-gles2-848c-krn29-full/summary.txt),
  [EGL](../docs/evidence/gpu/runs/20261022-231119-egl-848c-krn29-full/summary.txt).
- The `shaders.indexing.varying_array` cases that oopsed before 0003 pass
  (64/64) on both `848c0204` and `/usr/local`, but neither build sends a
  clean job for them anymore (no scratch), so they no longer test this path.

The threaded-store hang above is a compiler matter and is still open.

## Capability queries on kernel #29 (2026-10-23)

`kstb-gl-caps 2|3|3.1` creates a surfaceless context and prints extensions
and limits without compiling shaders or submitting draws. On installed
`23.3.6-848c0204` (`v3d_dri.so` SHA256 `f65f014d239860c453cf6aa63e0a07163b88f74f0a770d596f1b7ace908b7dfe`),
requests for versions 2 and 3 both returned GLES 3.0 / ESSL 3.00.
With `MESA_GLES_VERSION_OVERRIDE=3.1`, the 3.1 request returned GLES 3.1 /
ESSL 3.10 and these limits:

| Query | Value |
| --- | ---: |
| Fragment shader storage blocks | 16 |
| Vertex shader storage blocks | 0 |
| Compute shader storage blocks | 8 |
| Storage-buffer offset alignment | 4 |
| Fragment / vertex / compute image uniforms | 0 / 0 / 0 |
| Compute workgroup invocations | 0 |
| Compute shared-memory bytes | 0 |
| Fragment atomic counters | 4096 |

The override also reports `GL_OES_shader_image_atomic` despite zero image
uniforms. These are query results, not evidence that those features work;
the override does not provide a working GLES 3.1 implementation.
Raw output is in [`../docs/evidence/gpu/caps-20261023/`](../docs/evidence/gpu/caps-20261023).

## Threaded-store investigation (2026-10-23, scratch driver)

On kernel #29, the 64x64 baseline store shader reproduced the hang with
four threads and no spills: 0/4096 exact, reset, `ERR_STAT=0x1000`.
Moving the thread switch after the address write still hung. Adding a
read before the switch, and discarding its result, gave exact results
with four threads at 1x1, 17x13, 256x256 and 1920x1080, with no resets or
MMU errors. A coherent/volatile store followed by a shader buffer barrier,
reload and store also read back 4096 exact words. The reproducer now exits
nonzero if either CPU readback differs.

This does not close threaded spilling. A scratch compiler that adds the
read to spills and permits threaded spills completed the 200-case
`gles3-texture-units-spill.txt` list with 187 Pass and 13 image-comparison
Fail, no MMU errors or resets. The failures were `all_units.only_2d.0`–`.9`
and `all_units.only_2d_array.4`, `all_units.only_3d.4`,
`all_units.mixed.4`; the selected shader for `only_2d.0`
used two threads, 22 spills and 32 fills. The production spill fallback
has not been removed from the patch series. Evidence is under
[`../docs/evidence/gpu/runs/`](../docs/evidence/gpu/runs), directories
`20261023-threaded-*` and `20261023-180438-gles3-threaded-spill-fix`.

All these opens also emitted `drm_sched_entity_init: called with
uninitialized scheduler`. No installed Mesa build or kernel was changed
in this investigation.

## Patch 0023: threaded stores and the final TMU result (2026-10-23)

The scratch driver above exposed two separate problems. A store-only batch
supplies no read response for a thread switch. Patch 0023 issues one read
from a valid buffer address before switching and discards the result;
`TMUWT` still waits for the stores. Register spills use the same sequence.

The image failures came from postponed spills: the allocator treated the
final `LDTMU` signal as the end of a request, although the next `MOV` still
needed to copy its result from `r4`. Inserting a spill's read between them
replaced that result. On hardware with implicit `r4` results, the allocator
now waits until that copy has finished. The threaded-spill restriction is
removed from the candidate.

On the clean scratch candidate:

- All 200 `gles3-texture-units-spill.txt` cases Pass, with 0 MMU errors,
  resets and oopses. All 13 image failures above are gone.
- Scalar stores, 4-thread fragment shader: exact after draw and clear at
  1x1, 17x13, 64x64, 256x256 and 1920x1080.
- Explicit `uvec4` stores and eight-word batches, 2-thread fragment
  shaders: exact at 1x1, 17x13, 256x256 and 1920x1080. The largest batch
  checks 16,588,800 words. Vector requests exceed the four-thread input
  FIFO allowance, so the compiler selects two threads.
- Forced spilling in all stages: exact 64x64 readback with 4 fragment
  spills/fills (4 threads), 6 vertex spills/fills and 6 coordinate
  spills/fills (1 thread each). This test failed before the boundary fix.

Evidence: `20261023-232253-gles3-threaded-spill-r4-fixed`,
`20261023-threaded-store-scalar-vector-batch`, and
`20261023-threaded-store-r4-force-all-spill4` under
[`../docs/evidence/gpu/runs/`](../docs/evidence/gpu/runs).
The scheduler-entity warning remains as recorded above.

The exported series builds as `23.3.6-cd573075` (Mesa branch `d7e482b`,
`v3d_dri.so` SHA256 `7df059cdb908fa5eb9ab534c3fdfe5412792597a6fd9fa599dc0e4d4498191d9`).
It is selected by `/opt/mesa/current`, with `848c0204` retained. The
[full GLES3 run](../docs/evidence/gpu/runs/20261023-232845-gles3-cd573-full/summary.txt)
completed with 43,908 Pass, 583 NotSupported and 277 QualityWarning,
0 MMU errors, 0 resets and 0 oopses. All 44,768 statuses match the kernel
`#29` reference. The [full GLES2 run](../docs/evidence/gpu/runs/20261024-002824-gles2-cd573-full/summary.txt)
has 17,041 Pass, 119 NotSupported and 5 QualityWarning; the
[full EGL run](../docs/evidence/gpu/runs/20261024-004055-egl-cd573-full/summary.txt)
has 833 Pass, 2,955 NotSupported and 1 QualityWarning. Both also have
0 MMU errors, 0 resets and 0 oopses. All 65,722 statuses across the three
suites match the reference. `/usr/local` and kernel `#29` are unchanged.

This run took 59m38s versus 55m41s for the reference. The 200 texture-unit
cases took about 205s versus 93s: allowing threaded spills changes some
shaders from one thread without spills to two threads with spills. These
are elapsed times from separate conformance runs, not a controlled benchmark;
the cost needs compiler strategy tuning.

## Fragment SSBO gates (2026-10-24)

On `cd573075`, with `MESA_GLES_VERSION_OVERRIDE=3.1` only for these tests:

- `kstb-ssbo-atomic-test`: unsigned add/increment/decrement, min/max,
  and/or/xor, exchange, compare-and-swap, and signed add/min/max are exact
  at 1x1, 17x13, 64x64 and 256x256. The CPU also checks returned-value
  permutations and exactly one CAS winner. The fragment shader uses
  four threads, no spills; the largest test has 65,536 participants.
- `kstb-ssbo-layout-test`: std430 and std140 arrays, vectors and matrices
  read back exactly at those sizes, with record strides 64/112, binding
  offsets 4096, and source/destination guards checked. Both fragment
  shaders use two threads, no spills.
- Nine official GLES31 fragment SSBO binding tests Pass, including
  arrays, the maximum binding point and negative binding cases.

The watchdog was active. No MMU error, reset or oops occurred; the known
scheduler warning appeared on context creation. Evidence:
[standalone gates](../docs/evidence/gpu/runs/20261024-004622-fragment-ssbo-stage2-cd573/results.json),
[binding cases](../docs/evidence/gpu/runs/20261024-004636-gles31-cd573-ssbo-fragment-binding/summary.txt).

The GLES31 CTS binary now uses the patched GBM/nullws target (SHA256
`5c6437e5bb38ad4a4f33f15d2943819a1a427063c3426c9f3cf6fd36ca50c94e`).
Without an override it stops at context creation with `EGL_BAD_MATCH`,
before running cases. With the explicit override all six info cases Pass,
with no GPU faults. This validates the test plumbing, not GLES3.1 conformance;
images and compute are not implemented. See the [build notes](gpu-test/deqp/README.md).

## First image load/store probe (2026-10-24, scratch driver)

A scratch driver implements R32UI image accesses with general TMU memory
operations. The shader computes addresses from the resource's tiling,
stride and base uniforms. Out-of-bounds loads return zero and stores are
discarded. A harmless read in uniform control flow supplies the final
thread switch when the last image access was conditional.

`kstb-image-test` compares image loads through an SSBO with CPU inputs,
and image stores through independent framebuffer readback. Thirteen cases
Pass: 1x1 through 513x257, including negative and overflowing coordinates
and fully inactive image accesses. The source/destination layouts cover
LINEARTILE, UBLINEAR one/two columns, UIF and UIF XOR. The fragment shader
uses two threads, with no spills. There are 0 MMU errors, resets and oopses;
the scheduler warning remains.

[Evidence and the exact candidate diff](../docs/evidence/gpu/runs/20261024-093340-image33-threaded/results.json)
are saved with scratch DRI SHA256
`862ebe0452b8934ee2a6e310b5a2fa5e7514d3fc3ac26039c34eafbd3b6ef6b5`.
Only R32UI is enabled for this probe. Other formats, dimensions, image
atomics and full regression gates remain; this is not installed in the
numbered Mesa series. `/opt/mesa/current` still uses validated `cd573075`.


## Image format and tiling probes (2026-10-24, scratch driver)

`kstb-image-format-test` verifies all 33 ES3.1 / NV_image_formats formats
supported by the candidate: signed/unsigned 8-, 16- and 32-bit components,
half/full floats, normalized bytes, RGB10_A2, RGB10_A2UI and R11F_G11F_B10F.
Image loads are checked against CPU uploads through an SSBO. Stores are
checked by a second shader using native texture fetches, independently of
the candidate's image address calculation. Integer and representable float
checks are exact; normalized image loads allow 4e-7 absolute error, native
texture readback of stores allows 0.0005 (texture conversion precision).

All 264 cases Pass: 33 formats at 1x1, 4x17, 8x17, 16x9, 17x13, 65x33,
256x256 and 256x512. These cover LINEARTILE, UBLINEAR one/two columns,
UIF and UIF XOR for texel sizes from 1 to 16 bytes. No MMU errors, resets
or oopses occurred; only the known context scheduler warning was logged.
The watchdog was active throughout.

Evidence: [17x13](../docs/evidence/gpu/runs/20261024-095545-image33-nvformats/results.json),
[boundary and larger layouts](../docs/evidence/gpu/runs/20261024-095710-image33-nvformats-layouts/results.json).
The former includes the complete candidate diff. Scratch DRI SHA256 is
`3dbbf5192203850ec38fe6df1c7af39304aa9a64f414b7726dae6e364de923d4`.
Layers, mip levels, image atomics and graphics regression gates remain.
This candidate is not installed in the numbered Mesa series.


## Image dimensions and atomics (2026-10-24, scratch driver)

On the same scratch DRI (`3dbbf519…`):

- All 60 [2D/array/3D cases](../docs/evidence/gpu/runs/20261024-100434-image33-dimensions/results.json)
  Pass, spanning 1-, 2-, 4-, 8- and 16-byte texels, mip levels 0–2 and
  three array layers or 3D slices.
- All 25 [cube cases](../docs/evidence/gpu/runs/20261024-100553-image33-cube/results.json)
  Pass across all six faces, including 1x1, mip levels 1/2 and 256x256.
- All 60 [individual-layer binding cases](../docs/evidence/gpu/runs/20261024-100653-image33-single-layer/results.json)
  Pass: array layers, 3D slices and cube faces bound as non-layered image2D.
- [Image atomics](../docs/evidence/gpu/runs/20261024-100517-image33-atomics/results.json)
  are exact at 1x1, 17x13, 64x64 and 256x256. The CPU checks unsigned
  add/min/max/and/or/xor/exchange/CAS and signed add/min/max, plus returned
  value permutations and a single CAS winner. The shader uses four
  threads, no spills; 65,536 fragments contend at the largest size.
- All 12 [official early/late fragment-image tests](../docs/evidence/gpu/runs/20261024-100627-gles31-image33-fragment-early/summary.txt)
  Pass, including depth/stencil tests with and without attachments.

No MMU errors, GPU resets or oopses occurred. The context scheduler warning
remains. These use an explicit GLES3.1 override, not a claim of conformance.

The first dimension run exposed a separate existing defect: RGBA32F
`texelFetch` at nonzero mip levels returns zeros when the sampler uses
GL_NEAREST without mipmapping. A CPU-upload-only probe reproduces it on
both the candidate and validated cd573075; R32UI is unaffected.
[Baseline diagnostic evidence](../docs/evidence/gpu/runs/20261024-100401-image33-mip-baseline/results.json)
records six failed readbacks. `KSTB_CPU_FETCH=1 KSTB_NO_MIP_FILTER=1` in the
format test reproduces that path. The passing image dimension tests use
GL_NEAREST_MIPMAP_NEAREST for mip-level readback. The native fetch defect
remains open; image loads and stores at those mip levels pass independently.


The candidate also passes all 200
[spill-heavy texture-unit cases](../docs/evidence/gpu/runs/20261024-100853-gles3-image33-focused-spill/summary.txt),
with no MMU errors, resets or oopses. Full GLES3/GLES2/EGL regression subsequently passed (see below); the
candidate subsequently passed numbered-build smoke checks (below).


The implementation is exported as Mesa patch 0024. `make -C linux/mesa
check` and the reproducible build pass; candidate artifact
`mesa-23.3.6-fe7dda5e.tar` has DRI SHA256
`084ed6ac8d1f1b528704d7aa36335274de8bd0f4028b6fc4b5e1fb0f3a522208`.
Building the committed source with the scratch driver's previous Git
version string reproduces its exact `3dbbf519…` binary. The candidate
artifact was subsequently installed after full graphics regression passed.


## Full image-candidate graphics regression (2026-10-24)

The scratch image driver (`3dbbf519…`) completes all 65,722 GLES3, GLES2
and EGL cases with statuses identical to the kernel #29 / 848c0204 reference.
No MMU errors, GPU resets or oopses occurred. The watchdog was active.

- [GLES3 evidence](../docs/evidence/gpu/runs/20261024-101221-gles3-image33-full/summary.txt): 43,908 Pass, 583 NotSupported, 277 QualityWarning.
- [GLES2 evidence](../docs/evidence/gpu/runs/20261024-111231-gles2-image33-full/summary.txt): 17,041 Pass, 119 NotSupported, 5 QualityWarning.
- [EGL evidence](../docs/evidence/gpu/runs/20261024-112509-egl-image33-full/summary.txt): 833 Pass, 2,955 NotSupported, 1 QualityWarning.

The numbered artifact fe7dda5e subsequently passed installation smoke checks
and is now selected under `/opt/mesa/current` (below). Compute and default
GLES3.1 remain off.


## Numbered image build selected (2026-10-24)

`23.3.6-fe7dda5e` was installed alongside cd573075 after the full regression.
The installed DRI hash matches the build artifact (`084ed6ac…`). All 46
[standalone smoke cases](../docs/evidence/gpu/runs/20261024-130558-fe7dda5e-image-smoke/results.json)
Pass: 33 formats, four contended atomic sizes, six layered/non-layered mip
cases and three coordinate-boundary cases. All 21
[official fragment smoke cases](../docs/evidence/gpu/runs/20261024-130624-gles31-fe7dda5e-fragment-smoke/summary.txt)
Pass: twelve image early/late tests and nine SSBO binding tests. No MMU
errors, resets or oopses occurred; the known scheduler warning remains.

`/opt/mesa/current` now selects fe7dda5e; cd573075 is retained. Kernel #29
and `/usr/local` are unchanged. Compute and default GLES3.1 remain off.


## Render-only compute submission probe (2026-10-24)

`kstb-compute-render-test` submits a 64x32 RGBA16UI tile directly to the
render queue, with an empty bin list. Pre-shaded integer XY and constant
ZW attributes feed the NV shader record (opcode 68), followed by compressed
line primitives. Only the fragment shader runs. No Mesa driver is loaded.
The CPU checks all 2,048 texels plus a trailing memory guard, and verifies
that exactly one render job and no bin job completed.

All sixteen [line-count cases](../docs/evidence/gpu/runs/20261024-132002-compute-nv-line-matrix/results.json)
Pass, from 128 through 2,048 fragment invocations. Four
[repeated submissions](../docs/evidence/gpu/runs/20261024-131749-compute-nv-repeat-one/results.json)
also Pass. These runs have no MMU errors, GPU resets or oopses; only the
known scheduler warning appears. The watchdog was active.

The prototype initially set the general-store `Last Tile of Frame` flag:
the first submission passed but the next hung and the kernel reset the
GPU (ERR_STAT 0x1000). Leaving that flag clear, matching Mesa and the
vendor, fixes repeated submissions. A
[control without the dummy initialization store](../docs/evidence/gpu/runs/20261024-131850-compute-nv-no-dummy-control/results.json)
also passes four repetitions, isolating the flag from the dummy-store
change. The shipped probe retains Mesa's GFXH-1742 initialization sequence
when changing tile-buffer format.

This proves render-only NV submission and fragment coverage, not the
compute API. ID-image reads, shared memory, workgroup barriers and Mesa
`glDispatchCompute` integration remain under development. The numbered
Mesa build, kernel and system selection are unchanged by these raw probes.


## Compute ID and memory-write probes (2026-10-24)

The vendor ID tile uses **RGBA16UI**, as confirmed by internal type 5 and
output format 24 in the packet definitions. Earlier handoff notes called
it RGBA16F; that label was incorrect. The numeric packet fields used by
the render probe already match the vendor's integer configuration.

`kstb-compute-id-test` reads all four 16-bit ID fields into zero-extended
32-bit QPU registers using integer TLB config `0xffffff7b`. It XORs the low
bit of R/B, repacks the four components and writes them to the tile buffer.
All sixteen [coverage cases](../docs/evidence/gpu/runs/20261024-133950-compute-nv-id-matrix/results.json)
Pass; all fields vary and include nonzero high components. Unshaded pixels
must retain the source ID image. Float-format reads were a prototype
mistake: they cycled through R/G and failed the full-field reference.

`kstb-compute-store-test` uses those IDs to derive a linear destination
address, then writes all four fields as a vector of 32-bit words through
the TMU. It requests kernel cache cleaning, performs no framebuffer
output, and checks both buffer guards plus all inactive records. Exactly
one render job and one cache-clean job, with no bin job, must complete.
All sixteen [line-count cases](../docs/evidence/gpu/runs/20261024-134411-compute-nv-store-matrix/results.json)
Pass, from 128 through 2,048 invocations (32,768 output bytes at the largest
size). No MMU errors, GPU resets or oopses occurred; only the known context
scheduler warning remains. The watchdog was active.

These probes use single-threaded QPU programs and raw DRM submission.
They establish the ID and memory paths needed for compute; they do not yet
implement `glDispatchCompute`, shared memory or workgroup barriers.

## Opt-in Mesa compute dispatch (2026-10-24)

A scratch driver from Mesa `c073df2` implements `glDispatchCompute` on
V3D 3.3 using render-only NV submissions. `V3D_COMPUTE33=1` and the explicit
GLES 3.1 override select this experiment. The installed/default driver still
reports GLES 3.0. Shared memory and workgroup barriers are not supported by
this candidate; this is not GLES 3.1 conformance.

- Seven ID tests pass for local sizes 1, 17, 32, 105 and 128, including
  three-dimensional workgroups, padding lanes and dispatches spanning several
  render jobs. All 12 words per invocation and both 4096-byte guards match.
- SSBO and image atomics pass at 1x1, 17x13, 64x64 and 256x256: all integer
  operations, returned-value permutations and CAS winner checks. The same
  tests also pass through the fragment path (16 tests total).
- Four ordering tests pass: CPU-generated indirect dispatch, GPU-generated
  indirect dispatch, a compute consumer that transforms the producer's entire
  buffer, and GPU-generated indirect dispatch followed by that consumer.
  Each uses 10,710 invocations and checks guards.
- All passing runs have zero MMU errors, resets and oopses. The initial
  experiment exposed a missing QPU scheduler dependency for pre-4.1 TLB reads
  into r3; that failed run logged MMU faults. The candidate fixes the dependency.

Evidence: [IDs](../docs/evidence/gpu/runs/20261024-150050-compute33-id-matrix/summary.txt),
[atomics](../docs/evidence/gpu/runs/20261024-150238-compute33-atomic-matrix/summary.txt),
[ordering and candidate patch](../docs/evidence/gpu/runs/20261024-150335-compute33-ordering/summary.txt),
[initial failed scheduler experiment](../docs/evidence/gpu/runs/20261024-145950-compute33-first/results.json).
`kstb-compute-test` checks IDs; `KSTB_INDIRECT=1`, `KSTB_GPU_INDIRECT=1`
(with indirect) and `KSTB_CHAIN=1` select the ordering tests.
`KSTB_COMPUTE=1` selects compute in the existing SSBO/image atomic tests.

### Shared memory without an execution barrier

The next scratch candidate allocates 16 KB per workgroup, with disjoint slots
for the 16 workgroups in each render job. Seven ID shapes pass with a dynamic
shared-memory store, `memoryBarrierShared`, `atomicAdd`, another memory
barrier and a readback checked on the CPU. Padding lanes and all SSBO guards
also pass, with zero MMU faults, resets or oopses. This checks each invocation's
own shared-memory access; cross-invocation exchange still needs an execution
barrier. `KSTB_SHARED=1 kstb-compute-test` selects this test.
[Evidence and incremental patch](../docs/evidence/gpu/runs/20261024-150624-compute33-shared/summary.txt).

## Threaded compute and workgroup barriers (2026-10-24)

Patches 0025–0027 implement render-only compute on V3D 3.3. The numbered
build `23.3.6-f5f33af0` is installed alongside the selected `fe7dda5e` build;
its DRI SHA256 is `09c969dc3a2fb5d24f4971ab73e47d022d47f1f715f183e7955daf36dc57d069`.
The series exports, applies and builds successfully. Kernel #29 and
`/usr/local` are unchanged. Compute remains disabled unless
`V3D_COMPUTE33=1` is set; tests also explicitly override GLES to 3.1.

The implementation runs two-thread shaders through NV shader records, with
no binning or vertex shader. Each workgroup uses a two-row raster line rounded
to 16 invocations; up to 16 workgroups share a 64x32 RGBA16UI ID tile. The ID
image supplies local index, workgroup XYZ and a shared-memory allocation slot.
Padding lanes use invocation 0's addresses, with their writes masked off.
The per-workgroup shared-memory limit is 16 KB and invocation limit is 128.
Barriers use YCD/SYNCU, a quorum matching the launched QPU batches, and the
vendor's `0xffffffe0` / `0xffffffeb` configurations. TSY commands stay out of
thread-switch delay slots.

- **195/195 official compute cases pass**, with zero MMU errors, resets and
  oopses, in both the scratch driver and the numbered package. This is the
  compute subset of the union GLES 3.1 mustpass list, not full GLES 3.1
  conformance. [Numbered-build results](../docs/evidence/gpu/runs/20261024-153415-gles31-f5f33-compute-full/summary.txt).
- Seven workgroup shapes pass 31 rounds of cross-invocation shared-memory
  exchange (62 execution barriers), including partial groups and multiple
  render jobs. [Barrier checks](../docs/evidence/gpu/runs/20261024-153144-compute33-barrier-loops/summary.txt).
- Sixteen SSBO/image atomic tests and four indirect/producer-consumer tests
  pass on the final scratch driver. The numbered package passes nine further
  smoke checks, including three conditional scalar broadcast variants.
  [Numbered-build smokes](../docs/evidence/gpu/runs/20261024-153533-f5f33-compute-smoke/summary.txt).

Earlier failed experiments are retained: a fixed 128-invocation raster line
hung with padded looped barriers; padded IDs caused four invalid reads despite
41 API-level Pass results; and the first 195-case run had six failures and
five resets. The final passing runs above include the corresponding fixes.
The full GLES3/GLES2/EGL graphics regression subsequently passed on the numbered build;
`/opt/mesa/current` still points to `fe7dda5e`.

`KSTB_BARRIER=1 KSTB_BARRIER_ROUNDS=31 kstb-compute-test` runs the exchange test.
`kstb-compute-broadcast-test` records intermediate scalar broadcasts;
`KSTB_BROADCAST_AGGREGATE=1` and `KSTB_BROADCAST_BOOL=1` exercise accumulated
results without intermediate SSBO writes. Official lists are
`cases/gles31-compute-basic.txt` (41) and `cases/gles31-compute.txt` (195).

## Full compute-candidate graphics regression (2026-10-24)

Numbered build `23.3.6-f5f33af0` completes the 200 focused spill/texture
cases and all 65,722 GLES3/GLES2/EGL cases. Every full-suite status matches
the kernel #29 / 848c0204 reference, with zero MMU errors, resets or oopses.
The watchdog was active and compute was disabled for these graphics runs.

- [GLES3 focused-spill](../docs/evidence/gpu/runs/20261024-153624-gles3-f5f33-focused-spill/summary.txt): 200 Pass.
- [GLES3 full](../docs/evidence/gpu/runs/20261024-153951-gles3-f5f33-full/summary.txt): 43,908 Pass, 583 NotSupported, 277 QualityWarning.
- [GLES2 full](../docs/evidence/gpu/runs/20261024-163957-gles2-f5f33-full/summary.txt): 17,041 Pass, 119 NotSupported, 5 QualityWarning.
- [EGL full](../docs/evidence/gpu/runs/20261024-165241-egl-f5f33-full/summary.txt): 833 Pass, 2,955 NotSupported, 1 QualityWarning.

`/opt/mesa/current` remains `fe7dda5e`; this collector does not change it.

## Texture gather and an opt-in GLES 3.1 context (2026-10-24)

Build `23.3.6-f37c1f42` adds patches 0028–0029. Its DRI SHA256 is
`d9f4ff7d0739636ee3dd57359f21f8d91ed044e86fc57bcced66aad58096e485`.
With `V3D_COMPUTE33=1`, a GLES 3.1 context now succeeds **without a version
override**. With the flag unset it reports GLES 3.0. The package is installed
alongside the selected build; `/opt/mesa/current` remains `fe7dda5e` and
`/usr/local` is unchanged. This is an experimental capability milestone;
full GLES 3.1 conformance has not yet been established.
[Numbered-build context queries and shadow smoke](../docs/evidence/gpu/runs/20261024-174434-gather33-numbered-smoke/summary.txt).

Gather uses the native TMU gather flag and separate texture descriptors for
each selected component. The descriptors select a linear footprint and the
base mip level independently of the application's filtering state. Ordinary
lookups retain their own descriptors. Compute now emits texture descriptors
before allocating its uniform stream. Descriptor BOs remain referenced by
the job across indirect-buffer changes; context destruction releases the
cached ordinary descriptor references.

Patch 0028 fixes float-to-integer instruction packing: ORing the low-half
unpack selector into an opcode with its no-unpack bit already set encoded
the high-half selector. The CPU regression covers all four conversions,
three unpack modes and V3D 3.3/4.1/4.2; all 36 combinations and 47 existing
instruction fixtures pass. The new test fails with the original packer.
[Instruction tests](../docs/evidence/gpu/runs/20261024-174500-qpu-conversion-unpack/summary.txt).

The gather checks also exercise corrected hardware-format border swizzles
for packed color formats and native depth border packing. Floating-point
2D/array shadow gathers with border wrapping gather raw depths and compare
on the QPUs, preserving infinite border values. Cubemap shadow gathers
retain native comparisons.

- The 1,059-case official gather-related subset finishes with **654 Pass,
  405 NotSupported, zero failures and zero GPU faults** on the final scratch
  path. The unsupported cases require optional extensions: 396 require
  `GL_EXT_gpu_shader5`, and nine negative coverage cases require GLES 3.2 and/or cube-array
  support. This is a subset result, not full GLES 3.1 conformance.
  [Gather results](../docs/evidence/gpu/runs/20261024-174029-gles31-gather33-border-compare/summary.txt).
- All 48 mixed gather/texelFetch checks pass: three formats (R32UI,
  RGBA32UI, RGBA16F), four components, identity or B/A/zero/one swizzles,
  fragment or compute execution, a 17x13 source and guarded result buffers.
  [Mixed reads](../docs/evidence/gpu/runs/20261024-172807-gather33-mixed/summary.txt).
- The numbered build passes **448 CPU-checked shadow draws**: all eight
  comparison functions, seven border values and eight reference values,
  including positive and negative infinity. Result-buffer guards remain
  intact. [Numbered-build shadow checks](../docs/evidence/gpu/runs/20261024-174434-gather33-numbered-smoke/summary.txt).

Retained experiments show the progression: the first complete gather run
had 70 border failures; forcing full-word returns and changing normalized
border encoding still left 40 failures. Native return widths plus corrected
border packing/swizzles reduced this to two infinite-border shadow failures.
Applying QPU comparisons to every F32 shadow gather fixed those two but
failed 12 cube cases. Restricting that path to non-cube border wrapping
passes the complete gather subset. The initial mixed-read failure records
the independently fixed low-half conversion bug.

Reproduce the focused suite with `cases/gles31-gather.txt` and
`V3D_COMPUTE33=1 kstb-gpu-run --mesa 23.3.6-f37c1f42 gles31 --cases ...`.
`kstb-gather-test [width height component]` checks mixed reads; its source
lists the format, swizzle and compute switches. `kstb-gather-shadow-test`
runs the 448 shadow checks. The numbered-build focused regressions pass. Its first full GLES 3.1
attempt exposed the counter mask-ordering issue described below.

## Compute counter mask ordering (2026-10-24)

Patch 0030, build `23.3.6-b1e71af4`, orders V3D 3.3 TMUA/TMUAU requests
against SETMSF updates. Its DRI SHA256 is
`20670af7459e8f4288f4435cee612c30c843d35c63dfc24dc7527d2993da9f3f`.

The first full GLES 3.1 attempt on `f37c1f42` exposed a compute case with
one valid invocation: a counter initialized to 2 became 18 instead of 3.
The scheduled atomic request preceded the initial padding-lane mask. A
scheduler dependency fixes this while retaining the native increment and
decrement operations. Forcing an explicit atomic-add operand alone did not
fix the ordering and was discarded.
[Reproducer before the fix](../docs/evidence/gpu/runs/20261024-224103-atomic-counter-first/summary.txt),
[after the fix](../docs/evidence/gpu/runs/20261024-224329-atomic-counter-msf-order/summary.txt).

The numbered package passes, without a GLES version override and with zero
MMU errors, resets or oopses:

- [298/298 atomic-counter cases](../docs/evidence/gpu/runs/20261024-224610-gles31-b1e71-counter/summary.txt), including padded workgroups, branches, offsets and returned values.
- [195/195 compute cases](../docs/evidence/gpu/runs/20261024-224704-gles31-b1e71-compute/summary.txt).
- [1,059 gather-related cases](../docs/evidence/gpu/runs/20261024-224738-gles31-b1e71-gather/summary.txt): 654 Pass and 405 optional-extension NotSupported.
- [48/48 mixed gather/texelFetch checks](../docs/evidence/gpu/runs/20261024-224600-b1e71-numbered-mixed/summary.txt).

The initial full run was intentionally stopped and is not a conformance
total. Its bad decrement wrapped around; CTS then tried to allocate a
16 GB CPU array while checking the returned-value range. The runner also
marked unexecuted cases after ResourceError as Missing. `kstb-deqp` now
retries the remaining cases after any nonzero process exit. A host-only
integration check verifies that a real remaining case runs in a fresh
process while a genuinely absent case is still Missing.
[Incomplete diagnostic run](../docs/evidence/gpu/runs/20261024-174737-gles31-f37c1-full/README.txt),
[runner check](../docs/evidence/gpu/runs/20261024-224500-deqp-resource-retry/summary.txt).

The corrected full GLES 3.1 run stopped on an RGBA32I renderbuffer-to-
renderbuffer image-copy mismatch: 5,292 cases reported, with 5,133 Pass,
158 NotSupported and one Fail, without GPU faults. Patch 0031 fixes the
isolated failure and its renderbuffer-copy subset, as described below.
The dependent neural/graphics queue stopped without running.
Full GLES 3.1 and post-0030 graphics regressions remain pending.
[Incomplete full-run evidence](../docs/evidence/gpu/runs/20261024-224854-gles31-b1e71-full/README.md).

`kstb-neural-test [batch]` prepares a real two-dispatch compute workload:
a deterministic dense 32 -> 16 ReLU -> 8 network, with CPU comparisons for
every hidden value, output and predicted class, plus guarded output buffers.
`--cpu-only [batch]` runs the reference without opening the GPU. The source
cross-builds and its CPU reference runs on the board for batches 1, 65 and
257. GPU execution subsequently passes on patch 0031, as described below.
[Reference preparation](../docs/evidence/gpu/runs/20261024-232200-neural-cpu/README.md).

## Compute synchronization and reset recovery (2026-10-25)

Compute on 3.3 uses render-only CL submissions, sharing the graphics render
scheduler and reset machinery. Synchronization and reset checks now run
alongside the focused compute gates, before the full graphics regression.

`kstb-compute-sync-test [rounds=200] [count=4097]` alternates two shared
GLES 3.1 contexts. A compute producer writes an iteration-dependent SSBO,
exports a native fence, and a compute consumer imports/server-waits on it
before transforming the buffer. A consumer fence must signal; every word
of both buffers and both sets of guards must match. Normal rounds do not
wait for the producer on the CPU.

With `KSTB_RESET_PROBE=1`, the midpoint starts `kstb-reset-probe`, an
intentional render-CL semaphore stall on a separate DRM file. Compute is
queued behind it. A zero-time poll and timestamp confirm its exported fence
is pending before the kernel resets the GPU. The original contexts and
buffers then continue through the fence chain and exact-result checks.
Run this mode only inside the watchdog-protected test harness; the reset
is an expected event recorded separately from unexpected faults.

On kernel #29 / `23.3.6-b1e71af4`, without a version override:

- **350 chains pass**, including three deliberate resets, with zero MMU
  errors or kernel oopses. All three compute fences were pending before
  their respective reset; both 4,097-word buffers and 4 KiB guards match.
  [Reset/fence evidence](../docs/evidence/gpu/runs/20261025-083310-compute-sync-recovery/README.md).
- **195/195 official compute cases pass again after the resets**, without
  further faults.
  [Post-reset compute](../docs/evidence/gpu/runs/20261025-083501-gles31-post-reset-compute/summary.txt).

These results cover queued compute and retained contexts. Reset points
inside an executing compute shader, dma-buf exchange and hours-long mixed
load remain separate stress coverage.

## Pending clears during image-copy storage replacement (2026-10-25)

Patch 0031, Mesa `23.3.6-3112d69b`, submits pending resource writers before
`PIPE_MAP_DISCARD_WHOLE_RESOURCE` replaces the resource BO. Its DRI SHA256 is
`8171c7bd743141f875616c7d8b9aeea9febd5c5d62a60fe28de479a317051dab`.

The full-suite failure reproduces in isolation on `b1e71af4`: clear new
RGBA32I renderbuffers, immediately copy the full image, then copy subregions.
Same-format copies use the existing CPU map/copy path. A full destination
map may orphan its storage while its clear job is pending. The render CL's
attachment addresses are emitted at submission, so that old clear can
target the new BO and overwrite the CPU copy. Submitting pending writers
before the storage change preserves the correct destination.

The isolated case changes from Fail to Pass. The scratch driver also passes
**all 2,011 noncompressed copy cases involving a renderbuffer**, covering
the formats and copy directions in `cases/gles31-copy-renderbuffer.txt`,
with zero MMU errors, resets or oopses.
[Before/fix evidence](../docs/evidence/gpu/runs/20261025-085226-copy-rgba32i-qpa/README.md),
[renderbuffer subset](../docs/evidence/gpu/runs/20261025-085423-gles31-copy-renderbuffer-scratch/summary.txt).

The numbered package is installed alongside the selected build and passes
the isolated case, 298 atomic-counter cases, 195 compute cases and the
1,059-case gather subset (654 Pass, 405 optional-extension NotSupported),
with zero GPU faults. It also passes the 350-chain compute fence/reset gate
with exactly three deliberate resets and no MMU errors or oopses.
[Numbered compute](../docs/evidence/gpu/runs/20261025-085919-gles31-3112-compute/summary.txt),
[gather](../docs/evidence/gpu/runs/20261025-085953-gles31-3112-gather/summary.txt),
[fence/reset gate](../docs/evidence/gpu/runs/20261025-090110-3112-compute-sync-recovery/README.md).
Full GLES 3.1 and post-0031 graphics regressions remain pending.

## Neural-network compute workload (2026-10-25)

On kernel #29 / `23.3.6-3112d69b`, `kstb-neural-test` runs both layers on
V3D 3.3 with `V3D_COMPUTE33=1` and no version override. Batches 1, 65 and
257 pass: every hidden value, output value and argmax classification agrees
with the CPU reference, with maximum absolute error zero and intact guards.
There are no MMU errors, resets or oopses.
[Neural workload evidence](../docs/evidence/gpu/runs/20261025-090212-3112-neural/README.md).

For batch 257 the CPU reference takes 0.410 ms and GPU dispatch plus
synchronization takes 113.884 ms. This small deterministic model validates
correct execution; the measured GPU path is substantially slower than the
CPU. Those timings include dispatch overhead and do not isolate shader
execution. The weights are generated test data, with no trained-model
accuracy claim.

## Native indirect draws on V3D 3.3 (2026-10-25)

Patch 0032, build `23.3.6-5a3fd8dd`, adds the missing 3.3
`Indirect Primitive Limits` packet before native indexed and nonindexed
indirect draws. Its DRI SHA256 is
`fa0fa078a2a15ee7e4daebcd03e469370be6a72f2855ed6758c5f1dc8349ed08`.

The patch-0031 full run stopped after 2,305 reported cases on five
compute-generated indirect-draw failures, without GPU faults. Controls
showed the same failure with CPU-generated buffers and no compute shader.
The generated command/vertex data matched CPU readback, and explicitly
waiting for compute completion did not repair the draw.
[Incomplete full run](../docs/evidence/gpu/runs/20261025-090216-gles31-3112-full/README.md).

Host execution of the vendor's decoder, size function, packer and printer
identifies opcode 44 as a 12-byte `indirect_primitive_limits` packet: a
24-bit maximum index, 32-bit maximum instance and 32-bit index-buffer size.
Mesa defined opcode 44 only as V3D 4.1+ `Index Buffer Setup`. The new 3.3
definition matches the vendor bytes; draws emit the limits and retain
native indirect execution.
[Vendor binary audit](../docs/evidence/gpu/runs/20261025-093100-vendor-indirect-limits/README.md).

The scratch fix passes **157/157 normal-sized indirect-draw cases**,
including the combined/separate compute interoperability cases, primitive
types, offsets, index formats and instancing. The numbered build also
passes all 157, plus the image-copy reproducer, 298 atomic-counter cases and
195 compute cases, without GPU faults.
[157-case sweep](../docs/evidence/gpu/runs/20261025-092549-gles31-indirect-limits-short/summary.txt).

The retained numbered package subsequently passes the gather subset
(654 Pass / 405 optional-extension NotSupported), 350 compute/native-fence
chains with three deliberate resets, and all three neural-network batches
with exact CPU agreement and intact guards. Normal checks have zero GPU
faults; the reset gate has zero MMU errors or oopses.
[Numbered indirect draw](../docs/evidence/gpu/runs/20261025-093310-gles31-5a3fd-indirect/summary.txt),
[fence/reset recovery](../docs/evidence/gpu/runs/20261025-093617-5a3fd-compute-sync-recovery/summary.txt),
[neural checks](../docs/evidence/gpu/runs/20261025-093719-5a3fd-neural/summary.txt).

The 50 `compute_interop.large` cases remain separate coverage. An incomplete
broader sweep records two SIGKILL/Timeout results for 500x500 compute-generated
grids, without GPU faults. The precise termination cause and dispatch cost
remain open; CTS has a 30-second watchdog interval. An attempted tile-column
packing optimization caused failures/resets and was reverted. Numbered
builds retain the previously tested 16-workgroup layout.
[Large-case diagnostic](../docs/evidence/gpu/runs/20261025-092011-gles31-indirect-limits-all/README.md),
[rejected experiment](../docs/evidence/gpu/runs/20261025-092644-gles31-compute-packed-basic/README.md).

Full GLES 3.1 and post-0032 graphics regressions remain pending. Large-case
timeouts are not classified as a hardware limit or silently omitted.
The resumed full run stops after 2,577 reported cases on the attachment-free
framebuffer case `fbo.no_attachments.npot_size.127x127`, with two GPU resets
and zero MMU errors or kernel oopses.
That failure is the next unresolved conformance blocker.
[Incomplete patch-0032 run](../docs/evidence/gpu/runs/20261025-093723-gles31-5a3fd-full/README.md).

## Neural workgroup sizing and repeated-run gate (2026-10-25)

The existing patch-0032 driver supports local sizes up to 128. The neural
workload now accepts `batch local-size rounds`, reuses programs/buffers,
changes input every round, and checks every layer, classification and
buffer guard. Original defaults remain 65/16/1; a tested faster invocation
is `V3D_COMPUTE33=1 /opt/gpu-test/kstb-neural-test 257 128 11`, using the
`23.3.6-5a3fd8dd` package environment.

At batch 257, warm median dispatch-plus-synchronization time falls from
114.369 ms at local size 16 to 26.469 ms at size 128, a 4.32× improvement.
The CPU reference is still much faster: 0.397 ms in the size-128 run.
These are host elapsed timings, excluding input upload and output checking;
they do not isolate GPU execution. Ten warm rounds follow a separate cold
round. Batch 65 improves from 39.874 to 15.903 ms; batch 1 stays 4.384 ms.
No Mesa or kernel source changed for this milestone.

All **677 neural inferences** pass with maximum absolute error zero,
correct classifications and intact guards. Coverage includes batches
1/65/257 at local sizes 16/32/64/128, batch 4096 at size 128, 501 repeated
batch-257 runs, and neural checks after reset recovery. Alongside them,
250 cross-context compute/native-fence chains pass, including compute
pending across one deliberate GPU reset. There are zero unexpected resets,
MMU errors or oopses. This is a bounded reliability check, not hours-long
stress or full-stack reliability certification. The framebuffer reset
blocker above remains unresolved. Work is stopped at the milestone;
selected Mesa and system defaults are unchanged.
[Measurements and complete gate evidence](../docs/evidence/gpu/runs/20261025-095653-neural-milestone/README.md).
