# Packaged desktop apps and games on V3D 3.3

Installed from Alpine as ordinary `/usr` packages: Thunar, Mousepad, mpv,
SuperTux 2, LBreakoutHD and OpenTTD, including game data and OpenTTD's open
base graphics/music/sounds. Xwayland is also installed, but the existing Sway
session remains untouched and these checks use native Wayland. `packages.txt`
records versions; `poc-apps-install.txt` records all dependencies. Mesa r34
pins and the installed driver hash are unchanged.

Launch any app with Win+D and its name. `~/POC/Welcome.txt` is a sample text
file used in the editor/file-manager checks. These packages are optional;
they are not added to the minimal base-system setup.

| Check | Evidence | Result / scope |
| --- | --- | --- |
| Thunar | [211247](../20261025-211247-poc-files/) | Native window shows the POC folder |
| Mousepad | [211304](../20261025-211304-poc-editor/) | Native window opens the sample text |
| mpv | [211322](../20261025-211322-poc-video/) | Generated 640×360/25 fps pattern plays via `VO: [gpu]`, with native DRI maps and submitted render jobs |
| SuperTux default | [211359](../20261025-211359-poc-supertux/) | Exits before window: SDL2-compat forces X11 because GLEW is linked; no active X server in this desktop session, no GPU fault |
| LBreakoutHD | [211614](../20261025-211614-poc-breakout/) | Native game menu opens; 1882 additional render jobs in observation interval |
| OpenTTD | [211637](../20261025-211637-poc-transport/) | Native intro/menu opens; 500 additional render jobs; game simulation/blitter still runs on CPU |
| SuperTux Wayland auto | [211713](../20261025-211713-poc-supertux/) | Native window opens; direct GL backends fail and game falls back to SDL, with additional GPU render jobs |
| SuperTux Wayland SDL | [211835](../20261025-211835-poc-supertux/) | Explicit SDL renderer avoids failed GL backend attempts |
| SuperTux longer observation | [211958](../20261025-211958-poc-supertux/) | Verbose log confirms SDL renderer `opengl`, accelerated/target-texture flags, texture limit 2048; level introduction shown; game left open |
| User gameplay | [212204](../20261025-212204-poc-gameplay/) | User dismisses introduction and plays the level; two screenshots show actual level rendering; 60 seconds with no GPU fault; game left open |

All completed GPU probes have zero resets/MMU faults. The initial unsupported
startup is preserved as a failed application launch, not a GPU failure.
These are bounded launch/render/gameplay checks, not complete game testing.
No automated text-edit/save, file-copy, breakout round or transport-map
construction test was performed. Game frame counters are internal and are
not a measured display presentation rate.

The SuperTux launcher overlay keeps the package's metadata and uses
`env SDL_VIDEODRIVER=wayland supertux2 --renderer sdl`. This selects a game
frontend/backend; there are no Mesa library or driver path overrides. It is
installed at `/usr/local/share/applications/supertux2.desktop` and tracked in
`linux/rootfs` under the same path. `TryExec` hides the entry if the optional
game package is absent. Desktop-file validation passes. The normal package
file under `/usr/share/applications` is untouched. Remove the local override
to restore the package's default launcher.

The initial short SuperTux screenshots show the level introduction, which
waits for Enter/Space. They do not show a slow asset load or failed level
rendering. The user supplies gameplay input; the harness does not inject
keyboard or mouse events into the active desktop.

mpv's default legacy-GL path disables some advanced scalers, as recorded in
its log. The synthetic pattern check does not establish hardware video
decoding or fix YouTube playback. Thunar's optional thumbnailer service is
not installed. Neither limitation prevents these basic launch checks.

Before installation, removed only the obsolete, unused
`/root/gpu-system-prep/view-lib` native-Mesa test clone, freeing about 460 MiB
of uniquely held blocks. It had no active mappings or mounts. Rollback
packages/backups and source files remain intact. About 640 MiB remains free
after installing the apps (`disk.txt`).

Sway PID 5251 is unchanged. Only each probe's own process group is closed;
SuperTux PID 10728 is left open intentionally. All test watchdogs and guards
are stopped after the final observation. Run `python3 analyze.py` for the
recorded fault/driver/package checks.

Archived process maps retain graphics-library mappings and one representative
entry per DRM device path; repeated GEM mappings are omitted. Full originals
remain in the board run directories.
