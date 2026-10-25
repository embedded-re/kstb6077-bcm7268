# Sway GPU composition and tested `tv --gpu` (2026-10-25)

Sway now has a tested GPU mode on the physical HDMI output. Installed
`/usr/bin/tv --gpu [command]` selects per-session Mesa 23.3.6-85a951cd and
`WLR_RENDERER=gles2`. The normal `tv` command retains its CPU default;
`tv --cpu [command]` selects that fallback explicitly. No system Mesa,
selected `/opt/mesa/current`, kernel or display-driver change was made.

```
tv --gpu        # interactive terminal with GPU composition
tv --gpu btop   # a command in that terminal
tv --cpu btop   # explicit CPU fallback
```

Exit the command or shell, or press Ctrl+Alt+Backspace, to return to the
console. The old launcher is preserved on the board at
`/root/gpu-backups/sway-gpu-launcher/tv.before` and here as `tv-before`.
The installed new launcher matches `tv-tested` and the tracked rootfs file.
`KSTB_TV_MESA` optionally selects another installed Mesa package; this gate
validates only 23.3.6-85a951cd.

## Renderer and shared-buffer evidence

[The successful debug probe](../20261025-153236-sway-gles-probe/) records:

- Sway 1.12-rc3 / wlroots 0.20.0, GLES renderer **V3D 3.3**.
- GBM allocator on `/dev/dri/card1` (`kstb-drm`), LINEAR RGB565 buffers,
  1920×1080 with pitch 3840. The Mesa kmsro path opens V3D renderD128.
- Display-allocated DMA-BUF objects attached to `f1200000.gpu`, with render
  fences; compositor fdinfo records V3D bin/render jobs increasing.
- A correctly rendered animated foot terminal in
  [capture.png](../20261025-153236-sway-gles-probe/capture.png), inspected
  visually. Direct scanout is disabled in this diagnostic to exercise
  composition; the installed launcher leaves normal scanout policy intact.
- Clean compositor exit and no kernel faults.

The successful path allocates buffers through the display device and
imports them into V3D. It did not require forcing a separate render node,
disabling modifiers, CPU-renderer fallback, or a new display driver. This
is evidence of working cross-device sharing, not a blanket assertion that
no CPU copies occur anywhere: foot rasterizes text into shared-memory
client buffers, and the diagnostic screenshot performs a readback.

## Installed-launcher gate

`run.py` tests the real installed launcher as root, which switches to the
unprivileged kstb user. It checks renderer environment, loaded Mesa maps,
V3D fdinfo, changing HDMI CRC at the documented read-only 0xf06f8014 address,
normal process exit and return to tty1. The watchdog and kernel monitor
cover each session. No GPU workloads overlap.

| Session | Selected path | V3D render-job increase | Result |
|---|---|---:|---|
| gpu-first | `tv --gpu` | 30 | Pass |
| gpu-long | `tv --gpu` | 300 | Pass |
| cpu-rollback | `tv --cpu` | 0 | Pass |
| default-cpu | ordinary `tv` | 0 | Pass |
| gpu-restart | `tv --gpu` | 30 | Pass |

All five sessions complete: **three GPU sessions / 360 measured render
jobs**, two CPU fallback/default sessions, **zero resets, MMU errors or
oopses**. The long session runs about 33 seconds including startup and
exit; the client deliberately updates at about 10 Hz. This is not a GPU
maximum-FPS benchmark or a long-duration stress claim. All sessions restore
the console; final debugfs has **zero DMA-BUF objects**, no Sway/foot or
userspace watchdog remains running, and fbcon owns the scanout again.
See `results.json`, per-session files and `final-board-state.txt`.

The existing missing-CSD scheduler warning remains visible in kernel logs;
it does not prevent these rendering tests. User-space logs also report
missing Xwayland/swaybg and unsupported client protocols. The tested app is
native Wayland foot. X11 applications, browser/WebGL clients and hardware
video decode are not validated by this gate. Output remains RGB565;
XRGB8888, overlays and the other display-neighbour tasks remain open.

## Exploratory failures retained

No exploratory run produced a GPU fault. Incomplete runs are retained:

- [153121](../20261025-153121-sway-gles-probe/): GLES composition starts;
  screenshot cannot write to its root-owned destination. Destination fixed.
- [153148](../20261025-153148-sway-gles-probe/): screenshot and GPU evidence
  succeed; harness incorrectly requires an IPC response after requesting
  compositor exit. Later gate checks the compositor's normal exit instead.
- [153611](../20261025-153611-tv-gpu-gate/): new launcher cannot find
  kstb-mesa in the login user's PATH, before starting Sway. Fixed by using
  its absolute installed path.
- [153631](../20261025-153631-tv-gpu-gate/): real GPU session exits normally;
  harness incorrectly expects info-level renderer messages from non-debug
  Sway. Final launcher gate verifies environment/maps/render-job counters;
  the separate debug probe supplies the explicit GL renderer identity.

`analyze.py` verifies the final five-session gate and writes `summary.json`.
Shell syntax and installed launcher hash checks pass. All stages are done;
work stops at this rendering milestone. Compute optimization remains parked.
