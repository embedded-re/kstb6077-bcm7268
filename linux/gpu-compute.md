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

Our kernel reports no cache flush on 3.3 (kernel patch 0003), so the
installed Mesa has 0 SSBOs. The test ran on a scratch driver from
`23.3.6-848c0204` that reports SSBOs anyway and does not set
`DRM_V3D_SUBMIT_CL_FLUSH_CACHE`, with `MESA_GLES_VERSION_OVERRIDE=3.1`.
Kernel `6.18.57-kstb`.

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
  buffer. So today, TMU writes can reach memory after their buffer is
  freed.
- The store sequence Mesa emits is `mov tmud` / `add tmua` / `tmuwt`, with
  no config uniform (32-bit, per pixel, default op). It hangs in a
  4-thread fragment shader and completes in a 1-thread one. The same
  thread split is in [`gpu.md`](gpu.md) "Open": threaded spills hang.

Not tested yet: a cleaning job end (`TMUWCF`, then L2T clean, as mainline
`v3d_clean_caches()` does) on 3.3.
