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
with no MMU errors, resets or oopses. Full GLES3/GLES2/EGL regression is
still running; the candidate remains outside the numbered Mesa series.
