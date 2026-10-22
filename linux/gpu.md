# GPU stack: V3D 3.3 under Linux

The KSTB6077's GPU is a Broadcom V3D 3.3 (`brcm,7268-v3d`). Linux drives it
with mainline `v3d` plus our kernel patches; userspace is Mesa 23.3.6, the
last Mesa release with V3D 3.3, plus our patch series. Upstream will never
carry these fixes, so this repo is their home. This page is the entry point:
what is where, how to change it, how to test it, and where it stands. The
history of how each piece was found is in [`README.md`](README.md) "GPU".

## Where things are

| What | In the repo | On the box |
| --- | --- | --- |
| Kernel patches (`v3d`) | [`patches/`](patches) 0001, 0003, 0004 (0002 is kstb-drm) | in the kernel on `KRN` |
| Mesa patch series | [`mesa/patches/`](mesa/patches), one patch per fix | |
| Mesa build | [`mesa/Makefile`](mesa/Makefile) (cross build on the PC) | `/opt/mesa/<id>`, `/opt/mesa/current` = the build in use |
| Mesa installer | [`rootfs/usr/sbin/kstb-mesa`](rootfs/usr/sbin/kstb-mesa) | `/usr/sbin/kstb-mesa` |
| Test runner, tests, case lists | [`gpu-test/`](gpu-test) | `/opt/gpu-test` |
| dEQP binaries and mustpass lists | cross-built from VK-GL-CTS (README "dEQP") with [`gpu-test/deqp/`](gpu-test/deqp); hashes in `/opt/deqp/SHA256SUMS` | `/opt/deqp` |
| Run results | evidence copied to [`../docs/evidence/gpu/`](../docs/evidence/gpu) | `/root/gpu-runs/<date>-<module>-<name>` |

`linux/build/` (not tracked) holds the work trees: `mesa` (git, branch
`kstb`), `mesa-build`, `sysroot`, `out/mesa-<id>.tar`, `gpu-test`.

The system's default Mesa is still the older build in `/usr/local` (fixes
1–11 of patch 0001): every program that doesn't set a library path, such
as sway for `tv`, uses it. Nothing here changes it.
Moving the system to `/opt/mesa/current` is a separate step, only with the
user's OK.

## Changing Mesa

```
make -C linux/mesa branch      # once: linux/build/mesa = Mesa 23.3.6 + the series, branch kstb
# edit, build and try in linux/build/mesa; commit each fix on kstb
make -C linux/mesa export      # the branch's commits -> linux/mesa/patches/
make -C linux/mesa             # -> linux/build/out/mesa-<id>.tar (about a minute)
```

`<id>` is the Mesa version and a hash of the series, so every change gives a
new build id. `make` refuses to build when the branch and `patches/` differ.
`make sysroot` copies `/usr/include`, `/usr/lib` and `/lib` from ROOT once
(again after installing new `-dev` packages there).

For a quick experiment without a series change: `ninja -C
linux/build/mesa-build`, then copy `src/gallium/targets/dri/libgallium_dri.so`
to the box as `v3d_dri.so` in a scratch directory and point
`LIBGL_DRIVERS_PATH` at it. Never overwrite an installed build.

## Installing and switching on the box

```
scp linux/build/out/mesa-<id>.tar root@kstb6077:/tmp/     # or make -C linux/mesa tftp
kstb-mesa install /tmp/mesa-<id>.tar   # -> /opt/mesa/<id> (or: kstb-mesa install, over TFTP)
kstb-mesa use <id>                     # /opt/mesa/current -> <id>
kstb-mesa list | env [id] | remove <id>
```

Programs reach a build through `LD_LIBRARY_PATH`/`LIBGL_DRIVERS_PATH`
(`eval $(kstb-mesa env <id>)`); the tests in `/opt/gpu-test` have
`/opt/mesa/current/lib` as their rpath.

## Testing

```
make -C linux/gpu-test install         # C tests (cross-built), scripts, cases/ -> /opt/gpu-test
# on the box:
/opt/gpu-test/kstb-gpu-run [--mesa <id>] [--dri DIR] [--name x] gles2|gles3|egl [--cases FILE]
```

`--dri DIR` runs a scratch `v3d_dri.so` (the quick experiment below) on
top of the build's libraries; `meta.txt` records its hash.

`kstb-gpu-run` runs dEQP (GBM window 256×256, `rgba8888d24s8ms0`) on one
Mesa build in its own directory, with the watchdog on, and records the
build id, hashes, the kernel messages of exactly its time span (from
`/dev/kmsg`) and `summary.txt`: result counts, MMU errors, GPU resets and the
case each kernel event happened in. **A run is clean only when the summary
shows no MMU error and no reset**: dEQP cases can all pass while the kernel
logs faults.

Focused lists in [`gpu-test/cases/`](gpu-test/cases), one per fixed fault:
`gles2-cl-prefetch-seq200.txt`, `gles2-mmu-flush.txt`, `gles3-tlb-vec4.txt`,
`gles3-32bit-max-lod.txt`, `gles3-d24s8-msaa-zs.txt`, `gles3-shadow-32bit.txt`. `kstb-layer-test cube|array|3d f|ui [size]
[interleave] [srctex]` renders into every face/layer of an RGBA32F or
RGBA32UI texture and checks each texel by readback and by sampling.
Full runs take about 1 h (GLES3, 44,768 cases), 13 min (GLES2) and 3 min
(EGL).

## The fixes

Kernel ([`patches/`](patches)):

| Patch | Fault | Tested |
| --- | --- | --- |
| 0001 reset only existing queues | Oops in `drm_sched_stop` on every GPU reset | README "A GPU hang" |
| 0003 no cache flush without CSD | Oops per draw with TMU writes | README "A cache-flush job oopsed" |
| 0004 MMU flush before each job | a freshly mapped BO read as unmapped, then a GPU reset | `gles2-mmu-flush.txt`: 0 of 12 runs failing with it, 2 of 6 without |

Mesa ([`mesa/patches/`](mesa/patches)):

| Patch | What it fixes |
| --- | --- |
| 0001 | V3D 3.3 compiler and driver fixes 1–19 (README "GPU", numbered list) |
| 0002 | `EGL_ANDROID_native_fence_sync` (100 export/import chains exact) |
| 0003 | MSAA UIF XOR addressing (52 EGL MSAA cases) |
| 0004 | no buffer-texture acceleration on 3.x: correct CPU PBO path |
| 0005 | TMU FIFO overflow in cube/array shadow lookups (6 resets) |
| 0006 | RCL dummy store cleared loaded data (its separate stencil for D24S8 is undone by 0015) |
| 0007 | transform feedback on 3.3 (opcode, CPU counts, headers) |
| 0008 | VPM reads past 32 inputs |
| 0009 | dynamic uniform offsets out of the uniform BO (MMU errors) |
| 0010 | centroid varyings (70 TF image failures) |
| 0011 | CLE prefetch past the end of a CL BO (MMU errors, client 0x20) |
| 0012 | TLB colour writes of fewer than 4 components hung the GPU |
| 0013 | kmsro entry for `kstb-drm` (scanout) |
| 0014 | max LOD of 32-bit-return textures clamped to the last level (a minified lookup read missing mip levels: MMU errors, 32f `fbo.color` cube/array/3D) |
| 0015 | D24S8 is one buffer (stencil in the Z buffer's low byte, as the hardware writes it; separate S8 only for Z32F_S8); MSAA Z/S stored and loaded through the Z/S config with decimate mode all samples, one sample per texel, sampled with its depth format (was read as RGBA16F/RGBA8): MSAA depth/stencil MMU errors and wrong data, `gles3-d24s8-msaa-zs.txt` |
| 0016 | Z32F shadow lookups: a comparing sampler gets `DEPTH_COMP32F` (Z32F is R32F on 3.3, which the TMU doesn't compare; Broadcom's driver switches the same way): `texture.shadow` depth_component32f |
| 0017 | a no-mipmap sampler with a 32-bit return stays on the base level (0014 let it reach the last level of a mipmapped texture): 28 `texture.shadow` cases per depth format |

## Where it stands

Build `23.3.6-60757ba5` (`v3d_dri.so` `03615472…`, all 17 patches,
`/opt/mesa/current` since 2026-10-22) on kernel `#28` from `KRN` (patches
0001–0004), 2026-10-22, with `kstb-gpu-run` (results, kernel log and
summary in [`../docs/evidence/gpu/runs/`](../docs/evidence/gpu/runs),
`2026102*-6075-*`):

| Run | Pass | NotSupported | QualityWarning | Fail | Crash | MMU errors / resets |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| GLES2, all 17,165 | 17,041 | 119 | 5 | 0 | 0 | 0 / 0 |
| EGL, all 3,789 | 833 | 2,955 | 1 | 0 | 0 | 0 / 0 |
| GLES3, all 44,768 | 43,892 | 580 | 277 | 19 | 0 | 0 / 0 |
| the six focused lists | 1,272 | | 118 | 0 | 0 | 0 / 0 |

Against `949e0192` (15 patches), case by case: GLES2 and EGL identical;
in GLES3 202 cases change, none for the worse: the 200 of
`gles3-shadow-32bit.txt` (`texture.shadow` Fail → 82 Pass, 118
QualityWarning; all 432 shadow cases now Pass or QualityWarning, the
three depth formats alike) and the two
`shaders.texture_functions.*sampler2dshadow_bias_fragment` cases. With
0016 alone (scratch, `*-d32shadow-on`/`-off`) depth_component32f went
from 144 Fail to the 28 Fail that every format had before 0017.

Before, `949e0192`: GLES3 43,808 Pass, 221 Fail, 0 MMU errors; 0015
against `efbb2e70` turned 33 MSAA/D24S8 cases (`gles3-d24s8-msaa-zs.txt`)
and 86 depth24_stencil8 shadow cases better, and removed the last 6 MMU
errors.

## Open

- GLES3's 19 failures: `ubo.random` 6, `texture.units` 3 (cube),
  `multisample.fbo_*` 6 (sample coverage), `fbo.msaa.4_samples`
  r32f/rg32f/rgba32f, `texture.mipmap` 1 (cube).
- TFU jobs get no MMU flush (untested).
- Threaded shader scratch spilling still hangs; 0001 falls back to one
  thread.
- The system default (`/usr/local`) is still the older build.
