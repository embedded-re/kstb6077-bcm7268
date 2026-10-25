# Firefox: V3D 3.3 threaded vertex scratch-spill workaround

The original Firefox demonstration produced 24 GPU resets and visibly incomplete
redraws. Fresh-profile isolation places the reproducible hang in WebRender's
multi-threaded vertex shaders that spill registers through the TMU. WebGL is
not required to trigger it.

## Isolation on kernel #30, native Mesa r33

| Run | Change | Result |
| --- | --- | --- |
| [CPU](runs/20261025-195813-firefox-cpu/) | Software WebRender, WebGL disabled | DOM button and Wikipedia pass; zero GPU resets |
| [WebRender](runs/20261025-195859-firefox-wr/) | GPU WebRender, WebGL disabled | Independent guard aborts Firefox at first observed reset; 4 queued-job resets in captured log |
| [All shaders single-threaded](runs/20261025-200137-firefox-wr-single/) | Temporary diagnostic driver | No reset; test stopped on missing button because the user had navigated away. Not a completed test |
| [Vertex shaders single-threaded](runs/20261025-200324-firefox-wr-single-vs/) | Fragment threading unchanged | Hardware WebRender, DOM button and Wikipedia pass; zero resets |
| [Binning shaders single-threaded](runs/20261025-200415-firefox-wr-single-bin/) | Render-pass vertex threading unchanged | Guard aborts; 2 queued-job resets |
| [Targeted fallback](runs/20261025-200706-firefox-wr-candidate/) | Only vertex shaders needing threaded TMU spills retry at one thread | Hardware WebRender, DOM button and Wikipedia pass; zero resets |
| [Targeted fallback + WebGL](runs/20261025-200815-firefox-webgl-candidate/) | GPU WebRender and default-enabled WebGL | Red WebGL pixel reads exactly 255,0,0,255; button and Wikipedia pass; zero resets |

The failing render vertex shader GLSL13 reports 2 threads, 46 spills and 61
fills; GLSL19 reports 2 threads, 54 spills and 91 fills. With the narrow
fallback GLSL13 uses 1 thread, 7 spills and 8 fills, while GLSL19 needs no
spills at one thread. A lower-pressure vertex shader remains at 2 threads.
Exact shader statistics are in each run's `firefox.txt`. The temporary
[diagnostic compiler patch](firefox-single-thread-diagnostic.patch) is retained
for reproduction only and is absent from the installed release.

Patch 0034 refuses TMU spilling at more than one thread for vertex shaders on
V3D 3.3. Register allocation retries with the larger single-thread register
file, still allowing single-thread spills if necessary. Non-spilling vertex
shaders, fragment shaders, other GPU versions and compute retain their
existing policy. This bypasses the unsafe threaded vertex spill path; it does
not establish the underlying hardware/scheduling cause or fix every scratch
I/O path. High-pressure vertex shaders may lose some throughput.

## Focused graphics regression

[First batch](runs/20261025-201145-firefox-fix-regression/) passes 225 cases
before its 180-second outer harness limit interrupts the suite. The kernel
log has no GPU fault. [Remaining batch](runs/20261025-201455-firefox-fix-regression/)
passes the remaining 10 cases with a 600-second limit. Combined: **235/235
Pass**, comprising 200 multi-texture pressure cases and 35 vertex control-flow
and indexing cases. The latter batch also passes 100 native-fence chains with
exact pixels, MSAA/depth/stencil and GLES1 smoke checks. No GPU fault occurs.
This is a focused regression, not a rerun of the full conformance suite.

The independent fault guard runs while Marionette blocks; it signals only the
test's own process group. It leaves the user's Sway and other apps alone.
Already queued bad jobs can still time out after the first abort. The shader
capture trees remain in the board's run directories; the archived evidence
retains statistics, settings, exact runners, maps, graphics reports and logs.

## Native system installation and original demo

[Candidate original demo](runs/20261025-201535-firefox-demo-candidate/) passes
animation, pause/resume and colour controls, WebGL pixel checks, Wikipedia,
reload and scrolling. The JavaScript renderer label is privacy-sanitized to
`Generic Renderer`; that label does not indicate software rendering.

[Installed release](runs/20261025-201709-mesa-r34-install/): six signed native
APKs **23.3.6-r34**, source **8b05308**, patches 0001–0034. DRI SHA256:
`ab8e3f40c4d368bc46661342bde624de3051a249aeed1b02f220e39b6e8bcb8a`.
Package verification, dependency transaction and exact staged/installed hash
comparison pass. World pins now select r34. Prior signed r33 packages remain
in `/var/cache/kstb/packages/mesa-23.3.6-r33`; the r34 install evidence retains
the previous world and hash. Downgrade dependency resolution was checked with
`apk add --simulate` plus all six r33 files and exact package pins; the
transaction was not executed, since it would restore the unsafe browser path.
The earlier system migration exercised the full pre-native rollback.

[Native browser gate](runs/20261025-201713-firefox-demo-native/) repeats the
original demo on the installed `/usr` driver with no library/DRI overrides or
software-rendering preferences. Firefox reports **WebRender**, **1/1
accelerated window**, adapter **V3D 3.3**, and WebGL1/2 renderer **Broadcom --
V3D 3.3**. Browser process maps show `/usr/lib/dri/v3d_dri.so`. The demo
passes pause/resume, colour changes, Wikipedia, three reloads with scrolling
and exact red-pixel checks; all snapshots have zero failures, animation
progresses, and the kernel log contains no GPU reset/MMU fault. Sampled FPS
is about 36–44; this establishes working rendering, not a 60 FPS performance
guarantee. The complete conformance suite and a long browser soak were not
repeated for this patch.

The user's existing Sway PID 5251 remains running throughout; only test
browser process groups were closed. That Sway process keeps its previously
loaded Mesa until its next normal restart. New applications load r34.

## Normal user profile and launcher path

The [first normal invocation](runs/20261025-201916-firefox-normal/) reached the
profile wizard rather than the demo: the earlier test setup had created the
`.mozilla` and `firefox` parent directories as root. The user requested that
setup be completed. Ownership of those two directories was corrected to
kstb:kstb; a normal default profile was created through Firefox's profile
command. The [intermediate attempt](runs/20261025-202203-firefox-normal/) was
intentionally stopped to correct permissions, with no GPU fault.

The [final normal launch](runs/20261025-202231-firefox-normal/) passes a
60-second guarded observation with zero GPU faults and leaves Firefox open.
It uses `firefox file:///usr/share/kstb/browser-demo/index.html`
with the desktop runtime and session bus. It has no explicit profile,
automation flags or rendering overrides. Firefox registers and selects its
normal `default-release` profile. The screenshot shows a complete demo page,
45 FPS at capture and seven successful pixel checks. This is a bounded
normal-launch check, not a long browser stability guarantee. Test profiles
remain separate from this normal profile.

Archived process-map files are excerpts retaining Firefox, GPU device and GL/GBM
library mappings. Full original process maps remain in the board run directories.
