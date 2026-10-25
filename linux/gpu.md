# GPU stack: V3D 3.3 under Linux

The KSTB6077's GPU is a Broadcom V3D 3.3 (`brcm,7268-v3d`). Linux drives it
with mainline `v3d` plus our kernel patches; userspace is Mesa 23.3.6, the
last Mesa release with V3D 3.3, plus our patch series. Upstream will never
carry these fixes, so this repo is their home. This page is the entry point:
what is where, how to change it, how to test it, and where it stands. The
history of how each piece was found is in [`README.md`](README.md) "GPU".

Current installed state (2026-10-25): kernel **#30**, release
`6.18.57-kstb-hrtimer-ab`. The system driver is now **Mesa 23.3.6-r33**, six
signed APK packages installed under **`/usr`**, using the existing 33-patch
series (inner Mesa commit `3263da5`). Old `/usr/local` graphics files are
parked in the rollback backup. `/opt/mesa` is retained for explicit debug
comparisons and is not the normal desktop's driver source.

Start the normal desktop manually from the box's root console:

```
su -l kstb
# In the resulting user shell:
dbus-run-session sway
```

Sway uses the standard desktop configuration, panel, wallpaper and `wmenu-run`
launcher. Win+Enter opens a terminal, Win+D opens the launcher, and
Win+Shift+E logs out after confirmation. Closing applications or all terminals
leaves the desktop running. Boot remains at the console. Interactive login
sets the runtime directory and display-device/direct-scanout workaround;
apps need no Mesa library or driver path overrides. The normal renderer
selects V3D GLES automatically and scans out opaque ARGB8888 at 1080p.

The installed native GLES demo is `kstb-wayland-gles-test 40` when launched
inside the desktop. The final normal-desktop gate passes 711 frames at
59.241 FPS and 12,324 checked pixels through resize/fullscreen/windowed
transitions. Closing all terminals, opening a new terminal and launcher,
restart and CPU fallback pass. Package migration additionally passes GLES
1/2/3 setup, MSAA/depth/stencil and 100 native-fence chains, with an actual
restore of the previous installation followed by native reinstallation.
Zero GPU faults occur in these runs. Full conformance was not repeated for
the native-prefix/X11 frontend build. Xwayland and larger GUI apps remain
untested; compute optimization stays parked after its value gate.
[System installation, desktop and rollback evidence](../docs/evidence/gpu/runs/20261025-170246-normal-desktop-gate/README.md).
[Earlier colour-depth evidence](../docs/evidence/gpu/runs/20261025-163524-wayland-colour8-gate/README.md).
[Compute value decision](../docs/evidence/gpu/runs/20261025-151108-gemm-coprocessor/README.md).
[Kernel installation and rollback](../docs/evidence/gpu/runs/20261025-111823-hrtimer-installed/README.md).

## Where things are

| What | In the repo | On the box |
| --- | --- | --- |
| Kernel patches (`v3d`) | [`patches/`](patches) 0001, 0003–0005 (0002 is kstb-drm) | in the kernel on `KRN` |
| Mesa patch series | [`mesa/patches/`](mesa/patches), one patch per fix | |
| System Mesa | [`mesa/system/`](mesa/system/) (cross build and native APK packaging) | `/usr/lib`, `/usr/lib/dri`; `mesa*-23.3.6-r33` |
| Archived debug builds | [`mesa/Makefile`](mesa/Makefile) | `/opt/mesa/<id>`, `/opt/mesa/current` selects a debug build |
| Debug Mesa installer | [`rootfs/usr/sbin/kstb-mesa`](rootfs/usr/sbin/kstb-mesa) | `/usr/sbin/kstb-mesa` |
| Test runner, tests, case lists | [`gpu-test/`](gpu-test) | `/opt/gpu-test` |
| dEQP binaries and mustpass lists | cross-built from VK-GL-CTS (README "dEQP") with [`gpu-test/deqp/`](gpu-test/deqp); hashes in `/opt/deqp/SHA256SUMS` | `/opt/deqp` |
| Run results | evidence copied to [`../docs/evidence/gpu/`](../docs/evidence/gpu) | `/root/gpu-runs/<date>-<module>-<name>` |

`linux/build/` (not tracked) holds the work trees: `mesa` (git, branch
`kstb`), `mesa-build`, `sysroot`, `out/mesa-<id>.tar`, `gpu-test`.

The native Mesa packages are pinned to `23.3.6-r33` in `/etc/apk/world`,
so a normal package upgrade preserves the V3D 3.3 driver. They replace the
distribution's Mesa 26 packages, which do not support this GPU. The unsupported
Rusticl package was removed. `tv` remains an optional terminal kiosk;
`tv --gpu` now uses system Mesa unless `KSTB_TV_MESA` explicitly selects a
debug build. `tv --cpu` retains the CPU fallback.

## Building system packages

```
make -C linux/mesa system
# -> linux/build/mesa-system/{stage,system-stage.tar,BUILD-ID}
```

The native build enables Wayland and X11/GLX frontends, prefix `/usr`, libdir
`lib`, and the same v3d/kmsro patch series. It is isolated from the archived
`mesa-build` directory. Transfer/extract `system-stage.tar` on the board,
then run `mesa/system/package.py STAGE OUTPUT --sign-key KEY` with apk-tools
3 and readelf. It splits Mesa, EGL, GBM, GLES, GL and development packages,
records SONAME/pkg-config dependencies, preserves DRI hardlinks and excludes
the bundled Wayland protocol definitions. Use a fresh output directory.
The signing private key remains outside git; the public key goes in
`/etc/apk/keys`. Installed signed packages are retained in
`/var/cache/kstb/packages/mesa-23.3.6-r33`.

The actual system rollback is documented with the evidence above. Its
package restore and original local-library hashes were tested before the
native packages were reinstalled. `mesa/system/rollback.py` provides a backup
preflight and explicit restoration command. No kernel or boot change occurs.

## Changing Mesa and building debug variants

```
make -C linux/mesa branch      # once: linux/build/mesa = Mesa 23.3.6 + the series, branch kstb
# edit, build and try in linux/build/mesa; commit each fix on kstb
make -C linux/mesa export      # the branch's commits -> linux/mesa/patches/
make -C linux/mesa             # -> linux/build/out/mesa-<id>.tar (about a minute)
```

`<id>` is the Mesa version and a hash of the series plus Meson options,
so patch or platform-option changes give a new build id. Wayland client
support is enabled; the host needs wayland-scanner and wayland-protocols,
and the Alpine sysroot needs Wayland development headers/libraries. `make` refuses to build when the branch and `patches/` differ.
`make sysroot` copies `/usr/include`, `/usr/lib` and `/lib` from ROOT once
(again after installing new `-dev` packages there).

For a quick experiment without a series change: `ninja -C
linux/build/mesa-build`, then copy `src/gallium/targets/dri/libgallium_dri.so`
to the box as `v3d_dri.so` in a scratch directory and point
`LIBGL_DRIVERS_PATH` at it. Never overwrite an installed build.

## Installing and switching archived debug builds

```
scp linux/build/out/mesa-<id>.tar root@kstb6077:/tmp/     # or make -C linux/mesa tftp
kstb-mesa install /tmp/mesa-<id>.tar   # -> /opt/mesa/<id> (or: kstb-mesa install, over TFTP)
kstb-mesa use <id>                     # debug selector only; not the system driver
kstb-mesa list | env [id] | remove <id>
```

Programs reach a build through `LD_LIBRARY_PATH`/`LIBGL_DRIVERS_PATH`
(`eval $(kstb-mesa env <id>)`); the tests in `/opt/gpu-test` have
`/opt/mesa/current/lib` as their rpath.

## Testing

```
make -C linux/gpu-test install         # C tests (cross-built), scripts, cases/ -> /opt/gpu-test
# on the box:
/opt/gpu-test/kstb-gpu-run [--mesa <id>] [--dri DIR] [--name x] gles2|gles3|gles31|egl [--cases FILE]
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
`gles3-32bit-max-lod.txt`, `gles3-d24s8-msaa-zs.txt`, `gles3-shadow-32bit.txt`,
`gles3-sample-coverage.txt`, `gles3-fbo-msaa.txt`,
`gles3-texture-units-spill.txt`, `gles3-ubo-vec-align.txt`,
`gles3-mipmap-cube.txt`. `kstb-layer-test cube|array|3d f|ui [size]
[interleave] [srctex]` renders into every face/layer of an RGBA32F or
RGBA32UI texture and checks each texel by readback and by sampling.
`kstb-cube-seam-test linear|lmn|lml|nmn|nearest [max_level] [lod]` samples
a cube face near its edge with `textureLod` and prints how much of the
neighbouring face each lookup blends in next to the seamless value;
`... max_level quad W H` draws the face with `texture()` and a NEAREST mag
filter, as dEQP `texture.mipmap.cube` does.
Full runs take about 1 h (GLES3, 44,768 cases), 13 min (GLES2) and 3 min
(EGL).

## The fixes

Kernel ([`patches/`](patches)):

| Patch | Fault | Tested |
| --- | --- | --- |
| 0001 reset only existing queues | Oops in `drm_sched_stop` on every GPU reset | README "A GPU hang" |
| 0003 no cache flush without CSD | Oops per draw with TMU writes | README "A cache-flush job oopsed" |
| 0005 cache clean after render jobs on 3.3 (replaces 0003's refusal) | TMU writes not visible after the job; the last 256 bytes landed after the buffer was freed | [`gpu-compute.md`](gpu-compute.md): `kstb-tmu-write-test` exact right after the draw; GLES2/GLES3/EGL unchanged case by case |
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
| 0018 | sample coverage: 3.3's Sample State (packet 91) holds only the coverage value as a float, sign bit = inverted (Broadcom's driver emits it per draw); Mesa emitted it only on 4.x, so `glSampleCoverage` did nothing: 6 `multisample.fbo_*` coverage cases, `gles3-sample-coverage.txt` |
| 0019 | no 4× MSAA for R32F/RG32F/RGB32F/RGBA32F (Broadcom's driver gives these 0 samples; the resolve came out near zero): 3 `fbo.msaa.4_samples` cases NotSupported, `gles3-fbo-msaa.txt` |
| 0020 | register allocation on 3.3 keeps spill candidates for the 1-thread retry (the 2-thread round cleared them, so big shaders failed to compile with any strategy and the draw was skipped): 3 `texture.units.all_units.only_cube` cases (24 cube lookups), `gles3-texture-units-spill.txt` |
| 0021 | vector TMU loads on 3.3 only at the vector's alignment (8 bytes vec2, 16 vec3/vec4); the vectorizer's vec2/vec3 loads at 4-byte offsets read wrong data: 6 `ubo.random.scalar_types` cases, `gles3-ubo-vec-align.txt` |
| 0022 | a sampler view of one level samples without a mip filter: `GL_TEXTURE_MAX_LEVEL` is the max LOD clamp on 3.3, at the base level that clamp is 0 and the TMU then used the magnification filter for minified lookups (a face edge without the neighbouring face): `texture.mipmap.cube.max_level.linear_nearest`, `gles3-mipmap-cube.txt` |
| 0023 | threaded TMU stores and register spills: a discarded read supplies a response before a store-only thread switch; postponed spills wait until the last TMU result has been copied from `r4`. Focused tests and full GLES2/GLES3/EGL pass on `23.3.6-cd573075`; see [`gpu-compute.md`](gpu-compute.md). |

## Where it stands

The selected build is `23.3.6-fe7dda5e` (`v3d_dri.so` `084ed6ac…`, patches
0001–0024), on kernel `#29` from `KRN` (patches 0001–0005). Its image
implementation passed all GLES3/GLES2/EGL regression cases; the tested scratch
code was verified against the numbered artifact. Details and evidence are in
[`gpu-compute.md`](gpu-compute.md).
| Run | Pass | NotSupported | QualityWarning | Fail | Crash | MMU errors / resets |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| GLES2, all 17,165 | 17,041 | 119 | 5 | 0 | 0 | 0 / 0 |
| EGL, all 3,789 | 833 | 2,955 | 1 | 0 | 0 | 0 / 0 |
| GLES3, all 44,768 | 43,908 | 583 | 277 | 0 | 0 | 0 / 0 |

No GLES2, GLES3 or EGL case fails. All 65,722 statuses match `848c0204`
on kernel `#29`, including warnings and unsupported cases. Fragment SSBO
stores, atomics and std140/std430 layout tests also pass with the explicit
GLES3.1 override; fragment image loads, stores and atomics also pass.

Build `23.3.6-f5f33af0` (patches 0001–0027) adds opt-in render-only compute:
195/195 official compute cases pass with no GPU faults. It is installed
alongside the selected build; its full graphics regression passes with
all 65,722 statuses identical to the kernel #29 reference.
`V3D_COMPUTE33=1` enables the experiment; default GLES remains 3.0. See
[`gpu-compute.md`](gpu-compute.md) for limits, tests and retained failures.

Build `23.3.6-f37c1f42` (patches 0001–0029) adds texture gather and fixes
low-half float-to-integer conversion packing. `V3D_COMPUTE33=1` creates a
GLES 3.1 context without a version override; the default remains GLES 3.0.
The gather subset has 654 Pass / 405 optional-extension NotSupported,
zero failures or GPU faults; the numbered build passes 448 CPU-checked
shadow gathers. Full GLES 3.1 validation is pending. See
[`gpu-compute.md`](gpu-compute.md) for evidence and the retained experiments.

Build `23.3.6-b1e71af4` adds patch 0030: compute atomics remain after
padding-mask initialization. All 298 atomic-counter and 195 compute cases
pass, as do the gather subset and 48 mixed-read checks, without a version
override or GPU faults. Its full GLES 3.1 run stopped on an RGBA32I
renderbuffer-copy mismatch, without GPU faults. Compute/native-fence stress
also passes across three deliberate resets with queued compute, followed by
another 195/195 official compute pass.

Build `23.3.6-3112d69b` adds patch 0031: submit pending resource writers before
replacing storage during a whole-resource map. The isolated copy failure
passes on the numbered build, and the scratch fix passes all 2,011
noncompressed renderbuffer-copy cases. Its focused compute, atomic-counter,
gather and compute fence/reset gates pass. The two-layer neural network
also runs on V3D compute with exact CPU agreement for three batch sizes;
its measured GPU path is slower than the CPU reference. Full conformance
and graphics regressions remain pending; see [`gpu-compute.md`](gpu-compute.md).

Build `23.3.6-5a3fd8dd` adds patch 0032: program the vendor's V3D 3.3
indirect-primitive limits before native indirect draws. All 157 normal-sized
indirect cases pass on the numbered build, including compute-to-draw
interoperability. Large compute-generated draw timeouts and full-suite
validation remain open; see [`gpu-compute.md`](gpu-compute.md).

The previous build `848c0204` (22 patches), against `518c06f4` (21 patches),
case by case: GLES2 and EGL identical, in GLES3 only
`texture.mipmap.cube.max_level.linear_nearest` changes (Fail → Pass).
0022 on and off on one build (`*-lvl-*`): of all 5,895 GLES3 and 807
GLES2 `texture.*` cases only that one changes, 0 MMU errors and 0 resets
either way. `kstb-cube-seam-test lmn 0 quad 17 128` (LINEAR_MIPMAP_NEAREST,
`MAX_LEVEL` 0): R 20 on both edge rows without 0022 (no part of the
neighbouring face), 40 and 50 with it, the same as plain LINEAR; with
`textureLod` the TMU blended the edge texels exactly as seamless filtering
should in every filter mode tried, before and after.

Before, `518c06f4` (0001–0021): GLES3 43,907 Pass, 1 Fail, 0 MMU errors.
0018–0021 against `60757ba5`, case by case: exactly 18 GLES3 cases change,
all from Fail: 15 to Pass (6 sample coverage, 3 `texture.units` cube, 6
`ubo.random`) and the 3 `fbo.msaa.4_samples.*32f` to NotSupported; each
patch was also run on and off on one build (`*-cov-*`, `*-ms32f-*`,
`*-ra-*`, `*-ubo-*`) and changed only its own cases, 0 MMU errors.

## Open

- TFU jobs get no MMU flush (untested).
- GLES 3.1 remains opt-in; texture gather and the full GLES 3.1 regression remain outstanding.
- RGBA32F texelFetch at a nonzero mip with GL_NEAREST returns zero; the tracked image tests use a mipmapped filter.
- Compiler strategy tuning: some shaders now use two threads with spills
  instead of one without spills and take longer ([`gpu-compute.md`](gpu-compute.md)).
- `drm_sched_entity_init` warns on context creation for an absent queue.
- The system default (`/usr/local`) is still the older build.

Kernel timer performance fix (2026-10-25): `CONFIG_HIGH_RES_TIMERS=y` is now
in the build configuration. A RAM-booted #30 candidate removes coarse
cache-clean polling delays without dropping any flush: matched neural
batch-257 time falls from 26.355 to 1.273 ms, and batch 4096 from 190.500
to 9.897 ms. Both remain slower than the CPU reference. The candidate passes
1,253 CTS cases with zero failures, 600 exact neural runs and 350 fence
chains including three deliberate resets, with zero unexpected GPU faults.
Rollback to unchanged saved #29 is tested and restores the old timings.
The board currently runs #29; no KRN/DTB flash or Mesa-default promotion
occurred. [Timer A/B/A evidence](../docs/evidence/gpu/runs/20261025-104958-hrtimer-ab-gpu/README.md).
