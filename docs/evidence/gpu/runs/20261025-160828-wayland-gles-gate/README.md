# Native GPU Wayland client and fullscreen pacing workaround (2026-10-25)

A native GLES Wayland application now renders on V3D inside GPU-composited
Sway, including resize/fullscreen/restart checks. The final three runs
report **59.6–59.8 FPS**, with exact colour-marker checks and DMA-BUF
protocol evidence. The display remains RGB565.

Run the installed demo:

```
tv --gpu /opt/gpu-test/kstb-wayland-gles-test 40
```

It opens a rotating RGB triangle, moving gold bar and checked corner markers.
The title reports dimensions and frame rate. With the app focused,
Ctrl+Alt+F toggles fullscreen; closing it returns through foot to the console.
The demo accepts a duration from 2 to 120 seconds. `tv --cpu` remains the
terminal fallback. Normal `tv` still defaults to CPU composition.

## Wayland client platform

The prior Mesa package supported GBM/EGL for the compositor but was built
with an empty platform list. Enabled `-Dplatforms=wayland`, preserving the
33-patch V3D series, in separately installed **23.3.6-40ea9bc5**. The V3D DRI
binary is byte-identical to 85a951cd:
`18969788882886e842a68acba6818561eb9da09345e3586381c35d3f841f04e1`.
The changed EGL build supplies the native Wayland client platform.

`linux/mesa/Makefile` now hashes the patch series **and Meson options** into
the package ID, so changing window-system support cannot silently overwrite
a same-series package. The recipe applies those options to an existing build
directory before Ninja. `build-verification.txt` records the identity check;
`meson-build-options.json` and `BUILD-ID` record this actual build. Build and
series checks pass; no Mesa source/compiler/kernel patch changes occur.

`tv --gpu` now selects 40ea9bc5 per session. The root-to-kstb re-exec preserves
`KSTB_TV_MESA`; an actual older-package override is tested below. System Mesa,
`/usr/local`, selected `/opt/mesa/current` fe7dda5e and kernel #30 stay intact.
The previous launcher is preserved as `tv-before` and on the board at
`/root/gpu-backups/wayland-client-platform/tv-before`; it matches the previous
verified gate. Existing 85a951cd is retained.

## Direct scanout pacing problem

[Initial controlled display test](../20261025-155723-wayland-gles-visible/)
uses forced composition and completes **2,687 frames in 45.015 seconds**,
59.692 FPS, with 135 colour checks and eight configure events. It exercises
floating 960×640, resized 640×480, fullscreen 1920×1080 and windowed again.
[Fullscreen screenshot](../20261025-155723-wayland-gles-visible/fullscreen.png)
is visually inspected. Its three marker pixels are also checked exactly
after client rendering, compositor import and composition.

The first ordinary-launcher test correctly rendered its pixels but failed
our pacing gate during fullscreen transitions (41.744 average FPS, intervals
down to about 3 FPS). No GPU reset/MMU error occurred. That failed gate is
[preserved here](../20261025-160343-wayland-gles-gate/).

A [controlled comparison](../20261025-160642-wayland-pacing-ab/) repeats the
same native client and resize/fullscreen sequence:

| Path | Average FPS | Lowest one-second report | Sway CPU, one core |
|---|---:|---:|---:|
| Direct-scanout policy enabled | 41.708 | 3.328 | 32.0% |
| Forced composition | 59.701 | 56.982 | 5.0% |

`tv --gpu` therefore sets `WLR_SCENE_DISABLE_DIRECT_SCANOUT=1`. GPU composition
is retained for fullscreen clients. This is a tested workaround; the deeper
cause in the direct-scanout/feedback path is not isolated or repaired here.
The first controlled test already used forced composition, explaining why
it did not exhibit the ordinary-launcher stall.

## Final installed-launcher gate

The final gate checks the actual installed launcher and client, with the
workaround active. It uses three native-app sessions, then CPU fallback and
an older Mesa override, and finally 100 GBM native-fence chains.

| Session | Frames | Average FPS | Lowest report | Sway CPU | Client CPU |
|---|---:|---:|---:|---:|---:|
| Resize/fullscreen/windowed | 718 | 59.779 | 57.933 | 4.9% | 3.5% |
| Protocol trace | 358 | 59.556 | 57.591 | 6.1% | 4.8% |
| Restart | 717 | 59.702 | 57.074 | 5.1% | 3.2% |

The trace session includes logging overhead. CPU percentages are charges to
the named process divided by the sampled wall interval, relative to one
core; they exclude kernel-worker time and the mostly idle foot wrapper.
Frame rate measures the client render loop paced by Wayland frame callbacks;
it is not an independent presentation-feedback measurement of every frame
on the HDMI cable. HDMI CRC changes confirm changing physical output.

All **1,793 final client frames**, **90 exact RGBA marker checks**, resizing,
fullscreen/return, three starts/exits and **100 exact native-fence chains**
pass. The protocol log records **linux-dmabuf** buffer creation. Client and
compositor both have active V3D DRM job counters; CPU renderer fallback is
rejected. Every session releases all DMA-BUF objects and restores tty1.
There are zero GPU resets, MMU errors or oopses across all four archived runs.
The comparison additionally completes another 100 native-fence chains.

The expected missing-CSD scheduler warning remains. Missing Xwayland/swaybg
warnings are unchanged. These are native Wayland GLES tests; browser/WebGL,
X11 applications and video decode are not validated here. RGB565 causes
visible gradient quantization. No output-format/kernel change was made.

## Test and evidence

`linux/gpu-test/kstb-wayland-gles-test.c` binds xdg-shell, creates an EGL
Wayland window and a GLES2 context, verifies the V3D renderer, animates a
triangle/bar, resizes the EGL native window on configure events and checks
red/green/blue marker pixels every sixty frames. It uses frame callbacks
and swap interval 1, logs CPU/frame timing and destroys GPU/Wayland objects
before printing its successful result. Protocol bindings are generated by
host `wayland-scanner` from installed `wayland-protocols`; target headers
and libraries come from the Alpine sysroot.

`run.py` contains the final monitored hardware gate; each directory has
client scripts/logs, maps, fdinfo, CPU measurements, CRC samples and post-exit
DMA-BUF state. `analyze.py` verifies final success and preserves the failing
versus passing pacing comparison in `summary.json`. Source/build metadata,
installed launcher snapshot and final board state are included. The earlier
visible test preserves its exact earlier source (its success message was
printed before cleanup); the final client prints success after cleanup.

No compositor, client or userspace watchdog remains active. Compute work
stays parked. This rendering milestone is complete and stopped.
