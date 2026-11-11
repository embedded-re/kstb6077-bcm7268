# Native system Mesa and a normal manual desktop (2026-10-25)

The active GPU driver is now installed under **`/usr`**, owned by signed
Alpine packages **mesa, mesa-egl, mesa-gbm, mesa-gles, mesa-gl and mesa-dev
23.3.6-r33**. Applications use the normal loader and `/usr/lib/dri` without
Mesa environment overrides. The standard Sway desktop starts manually and
stays alive when applications close. Boot remains at the console, as chosen
by the user.

From the box's root console:

```
su -l kstb
# In the resulting interactive user shell:
dbus-run-session sway
```

Win+Enter opens a terminal; Win+D opens the application launcher;
Win+Shift+E logs out after confirmation. `kstb-wayland-gles-test 40` runs the
native GLES demo from inside the desktop. This session uses the standard
Sway configuration, swaybar, wallpaper through swaybg, and wmenu-run. One
terminal opens initially, and its lifetime does not control the compositor.
The old `tv` command remains an optional terminal kiosk.

## System driver integration

The source is the same 33-patch Mesa 23.3.6 series, commit `3263da5`.
The separate native build enables Wayland and X11/GLX, prefix `/usr`, libdir
`lib`, and v3d/kmsro. Its DRI SHA256 is
`3d83fd90a44513ca7554afd5c56bb60a4d63fe4188dcc4d00ca213c437b9325d`.
`BUILD-ID`, configure options, incremental build verification, installed
package metadata, public signing key, package hashes and library ownership
are preserved here. The private signing key is outside git.

`make -C port/mesa system` reproduces the native payload;
`port/mesa/system/package.py` creates the six APKs with dependency and
SONAME/pkg-config metadata. DRI aliases retain their hardlinks. The bundled
Wayland protocol XML/pkg-config file is excluded so Alpine's own protocol
package remains installed. Development dependencies are retained.

The six packages replace Alpine's unsupported Mesa 26 installation and are
pinned in `/etc/apk/world`. Signed archives are retained at
`/var/cache/kstb/packages/mesa-23.3.6-r33`, with the public key in
`/etc/apk/keys/kstb-mesa.rsa.pub`. Original Mesa 26 APKs and their dependencies
are cached for rollback. The unused Rusticl package and its orphaned compiler
components were removed; no OpenCL support is claimed.

Old `/usr/local` Mesa libraries, DRI aliases, headers and pkg-config files
were moved into `/root/gpu-backups/system-mesa/local-files`, preserving the
originals. `/opt/mesa` remains available for explicit debug comparisons;
it is not the normal driver's source. `kstb-mesa` now documents that role,
and `tv --gpu` uses system Mesa unless an explicit `KSTB_TV_MESA` override
selects a debug build. Archived tests may still contain explicit `/opt`
rpaths; the native validation clients were rebuilt without them.

## Validation

Before promotion, an [isolated native preview](../20261025-164959-native-system-mesa-preview/)
binds the candidate libraries at `/usr/lib` in a private mount namespace,
hides `/usr/local/lib` and `/opt/mesa`, and completes 2,672 frames with
46,215 exact checked pixels. Sway remains alive after the app exits.

The [migration test](../20261025-165747-system-mesa-migration/) then actually
installs the signed packages, parks the old local files, and passes GLES
2/3 context queries, the GLES1 smoke test, MSAA/depth/stencil pixel checks,
and 100 exact native-fence chains. It restores the original packages and
local files, verifies their original hashes, restores the original world
file and checks package consistency. It then reinstalls the native packages
and checks ownership and GLES3 again. Thus the package/local-driver rollback
was exercised, not only written down.

The final normal desktop gate launches `dbus-run-session sway -d` with the
ordinary configuration. The noninteractive test shell explicitly sources
`/etc/profile`; an actual interactive `su -l kstb` shell was separately
checked for the runtime/display settings and passed `sway -C`.

The desktop selects the V3D renderer automatically: no `WLR_RENDERER`, Mesa
library path, DRI path or GBM backend override is supplied. Its hardware
profile sets the display DRM device and retains the previously tested
forced-composition workaround for fullscreen stalls. ARGB8888 scanout is
confirmed. The app renders **711 frames at 59.241 FPS**, passing **12,324
exact client pixel checks** across resize/fullscreen/windowed transitions.
The fullscreen screenshot also contains all 1,024 exact ramp pixels.
Process maps confirm `/usr` drivers and exclude `/opt/mesa` and `/usr/local`
Mesa. `analyze.py` verifies these archived results.

Closing the app and then all terminals leaves the desktop running. A new
terminal and application launcher open; the [launcher screenshot](launcher.png)
is visually inspected. A second normal desktop start and explicit CPU
fallback both pass. All sessions restore tty1/fbcon and release all DMA-BUF
objects. No GPU reset/MMU/oops occurs across the four archived runs.

The [first desktop gate](../20261025-170151-normal-desktop-gate/) stopped
because its launcher PID assertion searched only for `wmenu`, while the
installed executable runs as `wmenu-run`. The launcher screenshot and
passing app results remain preserved. The corrected gate checks both names.
This was a harness assertion failure, with no GPU fault.

## Recovery and limits

With the desktop stopped, check the retained backup using:

```
python3 /root/gpu-backups/system-mesa/rollback.py /root/gpu-backups/system-mesa
```

Adding `--restore` restores the previous packages/local files, previous kiosk
launcher and world file, and removes the two new desktop profile/config
files. Package and local-file restoration was exercised during migration;
the later desktop-config removal is simple cleanup and was not itself
executed after installing the final desktop. The backup preflight passes.
Kernel #30, boot partitions and boot startup remain unchanged.

Xwayland is absent and its warning remains; X11 applications, browsers,
video playback and larger desktop environments have not been validated.
GLX is built but has no runtime validation in this milestone. Full dEQP
conformance was not rerun for the native-prefix/X11 frontend build. This is
a tested Sway/Wayland desktop with focused graphics and lifecycle checks,
not a claim that every desktop application works. Compute work remains
parked. No compositor, client or userspace watchdog is left running.
