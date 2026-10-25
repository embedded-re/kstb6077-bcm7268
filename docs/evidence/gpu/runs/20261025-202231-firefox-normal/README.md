# Normal Firefox: default user profile, native system driver

User requested completion of the first-use profile setup. Corrected ownership
of `.mozilla` and its `firefox` parent from root to kstb, created a profile using
Firefox's profile command, and launched normally. Firefox registered and chose
`default-release` for the installed release (`profiles.ini`).

`launch.json` has no explicit profile, automation or Mesa/GPU overrides.
Native DRI maps and hash match Mesa 23.3.6-r34. The 60-second independent fault
guard sees no GPU fault. `normal.png` shows the fully drawn demo, 45 FPS and
seven passing pixel checks. This normal-profile check uses screenshot/maps;
the separate [automation gate](../20261025-201713-firefox-demo-native/) records
hardware WebRender, scripted interactions and three reload/scroll cycles.
Firefox is left open for the user, Sway PID 5251 is unchanged, and the test
watchdog/guard are stopped after observation.
