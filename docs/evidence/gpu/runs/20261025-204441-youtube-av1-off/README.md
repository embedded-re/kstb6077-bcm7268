# YouTube 480p: AV1-off playback comparison

User reported stutter at 480p and authorized a Firefox restart to test a codec
change. Kernel #30 and native Mesa 23.3.6-r34 stay installed. Sway PID 5251
is left running. Only the normal Firefox profile gets
`user_pref("media.av1.enabled", false)` in `user.js`; no GPU rendering
preferences change. Firefox is restarted normally without automation flags
or Mesa overrides and reopens the same video near the prior playback position.
It remains open after the 180-second independent fault guard finishes.

| Steady interval | AV1 baseline | VP9 after restart |
| --- | ---: | ---: |
| Resolution / frame rate | 854×480 / 25 fps | 854×480 / 25 fps |
| Viewport / output | 1920×1080 / 59.94 Hz | 1920×1080 / 59.94 Hz |
| Dropped counter, start → end | 32 → 42 | 467 → 472 |
| Total counter, start → end | 1853 → 2402 | 2371 → 2929 |
| New dropped / new frames | 10 / 549 (1.82%) | 5 / 558 (0.90%) |
| Buffer at endpoints | 85.65–110.77 s | 107.76–120.65 s |

Counts are transcribed from YouTube Stats for nerds; full video screenshots
remain private on the board and under ignored `linux/build/browser/`.
The earlier cumulative counters included startup drops and sometimes reset;
they must not be used as steady playback rates. The two selected intervals
have stable counters but different scenes and occur sequentially. This is a
useful live comparison, not a repeatable claim of precisely halving the drop
rate for all content. Screenshot capture itself adds some load.

Active baseline decoder process samples were roughly 60–80% of one CPU.
Post-switch steady samples are 38.5%, 27.6%, 34.5% (`cpu-steady.json`).
`cpu-warmup-with-ad.json` includes an ad/initialization transition and must
not be used for an equivalent-content performance claim. Decoder CPU cost
fell, but Firefox still uses considerable CPU and GPU render time. An
8.03-second deduplicated DRM-client sample records browser render-engine
time at 56.3% of elapsed and Sway at 20.8% (`engine-deltas.json`). This is
scheduled engine-time accounting, not a measurement of QPU occupancy.

The 180-second kernel log has **zero GPU faults**. Process-map excerpts and
`driver.sha256` confirm system Mesa. The initial restart attempt in
[204359](../20261025-204359-youtube-av1-off/) stopped at its startup assertion
while the previous browser's children were still exiting; it launched no new
GPU workload. A later normal launch succeeds.

This improves the measured playback interval and decoder CPU use; **smooth
video playback is not yet established**. Missing hardware video decode,
render/upload/composition cost, frame deadlines and 25 fps on a 59.94 Hz
output remain separate considerations. Network starvation does not explain
the measured intervals because both have ample buffered video. Hardware
video decoding remains unavailable (see the prior native browser gate's
`graphics.json`); V3D drawing support does not supply a video decoder.

The AV1-off preference is kept only in this user's normal profile.
Rollback: remove that entry from `user.js`, then reset `media.av1.enabled`
in `about:config` and reload the video. Removing `user.js` alone does not
clear a value already persisted in `prefs.js`. AV1-only media will not play
while the preference is disabled. The staged previous-state record is at
`/root/gpu-system-prep/youtube-av1-off/before.json` (there was no `user.js`).
No Mesa/kernel/package changes were needed for this comparison.

Run `python3 analyze.py` for the recorded interval arithmetic and fault check.
