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
It is installed alongside `848c0204`; full GLES3/GLES2/EGL validation is
in progress with explicit `--mesa 23.3.6-cd573075`. `/opt/mesa/current`
still selects `848c0204`; `/usr/local` and the kernel are unchanged.
