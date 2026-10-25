# Desktop application launcher (2026-10-25)

Win+D now opens a centred Wofi application grid with search, large icons,
readable labels and mouse selection, replacing the single-line executable
list. The two-column layout and dark colours match the terminal palette.
[Actual menu screenshot](launcher.png) is cropped to the launcher so the
user's other desktop contents are not archived.

The menu uses desktop entries, not every executable in PATH. Local desktop
entry overrides name the four installed applications Terminal, Text Editor,
System Monitor and Task Manager. Foot client/server helper entries are hidden.
The original Exec, Terminal and application metadata are retained. No filler
applications were added. Search and launch behaviours come from Wofi's
standard drun mode; `term=foot` handles terminal applications.

Wofi 1.5.3, GTK3, Adwaita and hicolor icon-theme support are installed normally
through APK. The first preview exposed missing icons because the hicolor
theme index was absent; installing hicolor-icon-theme resolves that. The
final screenshot shows the actual app icons. `kstb-setup` now includes the
launcher, wallpaper, icon and font dependencies for future provisioning.

Configuration lives in `linux/rootfs/etc/wofi/{config,style.css}`. The Sway
drop-in sets `$menu` and explicitly rebinds Win+D after the default config's
binding. The current user-owned Sway session was reloaded successfully and
kept its original PID **5251**. This work did not close/restart the user's
desktop or applications. The preview opened our launcher briefly, captured
its appearance, and terminated only that launcher. Watchdog/kernel monitoring
covered both previews; neither contains GPU reset/MMU/oops events. No test
launcher or userspace watchdog remains running. The user's Sway remains open.

The installed Wofi man pages were used for drun, columns, icon, terminal and
CSS options. A standalone `sway -C` attempt while the user owned the seat
could not open that busy seat; the successful in-session reload is the
configuration check used here. No second compositor was started.

This milestone verifies layout, icon loading and live config reload; it did
not inject typing or mouse clicks into the user's active session. It does
not claim an automated search/launch test. The earlier preview's log is
preserved at `../20261025-172442-launcher-preview/`.

To restore the previous menu, replace `/etc/sway/config.d/50-kstb.conf` with
`/root/gpu-backups/desktop-launcher/sway-before.conf`, then reload Sway.
Mesa, the kernel and manual desktop startup are unchanged.
