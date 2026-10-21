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
make -C linux/gpu-test install         # C tests (cross-built) and scripts -> /opt/gpu-test
# on the box:
/opt/gpu-test/kstb-gpu-run [--mesa <id>] [--name x] gles2|gles3|egl [--cases FILE]
```

`kstb-gpu-run` runs dEQP (GBM window 256×256, `rgba8888d24s8ms0`) on one
Mesa build in its own directory, with the watchdog on, and records the
build id, hashes, the kernel messages of exactly its time span (from
`/dev/kmsg`) and `summary.txt`: result counts, MMU errors, GPU resets and the
case each kernel event happened in. **A run is clean only when the summary
shows no MMU error and no reset**: dEQP cases can all pass while the kernel
logs faults.

Focused lists in [`gpu-test/cases/`](gpu-test/cases), one per fixed fault:
`gles2-cl-prefetch-seq200.txt`, `gles2-mmu-flush.txt`, `gles3-tlb-vec4.txt`.
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
| 0006 | RCL dummy store cleared loaded data; separate stencil |
| 0007 | transform feedback on 3.3 (opcode, CPU counts, headers) |
| 0008 | VPM reads past 32 inputs |
| 0009 | dynamic uniform offsets out of the uniform BO (MMU errors) |
| 0010 | centroid varyings (70 TF image failures) |
| 0011 | CLE prefetch past the end of a CL BO (MMU errors, client 0x20) |
| 0012 | TLB colour writes of fewer than 4 components hung the GPU |
| 0013 | kmsro entry for `kstb-drm` (scanout) |

## Where it stands

Build `23.3.6-88b9f694` (`v3d_dri.so` `a1aa9cd4…`, all 13 patches) on
kernel `#28` from `KRN` (patches 0001–0004), 2026-10-21, with
`kstb-gpu-run` (results, kernel log and summary in
[`../docs/evidence/gpu/runs/`](../docs/evidence/gpu/runs)):

| Run | Pass | NotSupported | QualityWarning | Fail | Crash | MMU errors / resets |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| GLES2, all 17,165 | 17,041 | 119 | 5 | 0 | 0 | 0 / 0 |
| EGL, all 3,789 | 833 | 2,955 | 1 | 0 | 0 | 0 / 0 |
| GLES3, all 44,768 | 43,758 | 580 | 83 | 340 | 7 | 22 / 0 |
| the three focused lists | 1,145 | | | 0 | 0 | 0 / 0 |

Against the same series built the old way (`6e5707b2…`), case by case,
only `texture.shadow` moved, where the SIGSEGVs land on different cases
every run (11 cases between Fail, Crash and Pass). GLES2 and EGL have no
MMU error and no reset; GLES3's 22 MMU errors are all in the `fbo` cases
below.

## Open

- `fbo.color` with `rg32f`/`rgba32f` on 2D array, 3D and cube textures,
  `fbo.invalidate` and `fbo.msaa` MSAA blits: MMU errors (texture clients
  `0x2`–`0xf`), the cases fail.
- `texture.shadow`: 274 Fail, 76 QualityWarning, 7 SIGSEGVs that move
  between cube and 2D-array cases from run to run.
- Smaller GLES3 groups not looked at: `fbo.msaa`, `fbo.blit`, `ubo.random`,
  `texture.specification` (one SIGSEGV), `multisample.fbo_*`,
  `texture.units`, `texture.mipmap`, two `sampler2dshadow_bias` image cases.
- TFU jobs get no MMU flush (untested).
- Threaded shader scratch spilling still hangs; 0001 falls back to one
  thread.
- The system default (`/usr/local`) is still the older build.
