# Eight-bit RGB output for GPU Sway (2026-10-25)

Installed `tv --gpu` now uses **ARGB8888**, eight bits per RGB channel,
instead of RGB565. The existing Sway/wlroots stack falls back from unsupported
XRGB8888 to ARGB8888 when configured with `render_bit_depth 8`. No Mesa,
kernel or display-driver patch was needed. GPU composition remains forced
because of the previously measured direct-scanout fullscreen stalls.

`/etc/kstb-tv/sway-gpu.conf` includes the normal configuration and overrides
its output depth. The launcher selects that profile only for GPU mode.
Normal `tv` and explicit `tv --cpu` retain the RGB565 profile. Kernel #30,
system libraries and selected Mesa fe7dda5e are unchanged; GPU mode still
selects 23.3.6-40ea9bc5 per session.

## Colour correctness

The [45-second ramp test](../20261025-163337-wayland-colour8-ramps/)
adds deterministic 256-level grey, red, green and blue strips to the native
GLES triangle client. Its 2,672 frames pass 46,215 client pixel checks through
floating, resizing, fullscreen and return to windowed mode. The fullscreen
[screenshot](../20261025-163337-wayland-colour8-ramps/fullscreen.png) contains
all 1,024 expected ramp pixels and the three exact RGB corner markers.

A root read-only mapping of the active DRM framebuffer through GETFB2 and
MAP_DUMB confirms **1920×1080 ARGB8888, pitch 7680**, all 1,024 exact ramp
pixels, and **alpha 255 for all 2,073,600 pixels**. This matters because the
display feeder always applies the fourth byte as alpha; advertising XRGB
would still be incorrect. The compressed raw framebuffer and reader source
are preserved alongside the screenshot. This verifies the scanout buffer;
it is not a measurement of the television panel's colour response. Changing
HDMI CRCs in the final gate confirm changing physical output.

## Installed-launcher reliability gate

| Session | Frames | Average FPS | Lowest one-second report |
|---|---:|---:|---:|
| Resize/fullscreen/windowed | 718 | 59.785 | 58.106 |
| Protocol trace | 358 | 59.662 | 58.281 |
| Restart | 717 | 59.721 | 57.287 |

The three native sessions pass **1,793 frames and 30,810 exact client pixel
checks**, including colour ramps. Protocol tracing confirms linux-dmabuf
buffer creation; DRM state confirms ARGB8888 scanout in every GPU session.
The client uses V3D, and the compositor and client submit hardware render
jobs. Ordinary runs charge about 5% of one CPU core to Sway and 4% to the
client; trace logging adds CPU work. These are client frame-loop timings,
not independent HDMI presentation measurements.

Explicit CPU fallback passes with RGB565 and zero GPU render jobs. An actual
older Mesa 85a951cd override passes with ARGB8888 and the selected libraries
visible in process maps. All five sessions exit normally, restore tty1 and
release all DMA-BUF objects. The following 100 native-fence
export/import/server-wait chains pass exact pixel checks. There are zero GPU
reset/MMU/oops events across all four archived runs.

The [first gate](../20261025-163431-wayland-colour8-gate/) stopped after four
passing sessions: its new format assertion sampled fbcon before the older
package had presented its first frame. This was a test-readiness race.
The final runner waits for Sway-owned scanout before asserting the format.
The earlier [initial 8-bit probe](../20261025-163144-wayland-colour8-probe/)
also completes. The interrupted gate and its logs remain preserved.

## Reproduce and roll back

```
tv --gpu /opt/gpu-test/kstb-wayland-gles-test 40
python3 docs/evidence/gpu/runs/20261025-163524-wayland-colour8-gate/analyze.py
```

The analyzer requires Pillow and verifies archived raw scanout bytes,
screenshot ramps, logs, selected formats, configuration hashes and cleanup.
The demo accepts 2–120 seconds; Ctrl+Alt+F toggles fullscreen.

`tv --cpu` retains the tested fallback. To restore the preceding GPU launcher
on the board, copy `/root/gpu-backups/wayland-colour8/tv-before` to
`/usr/bin/tv`; it uses the original RGB565 configuration. The normal profile
was not modified. This milestone changes output colour precision only;
Xwayland/browser/video workloads remain unvalidated, and compute work stays
parked. No compositor, client or userspace watchdog remains active.
