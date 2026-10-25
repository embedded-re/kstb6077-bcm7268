# Timer A/B/A rollback: original kernel #29 restored and tested

After the [RAM-only timer candidate](../20261025-104958-hrtimer-ab-gpu/README.md),
a clean reboot and kmon's `linux` command start the unchanged KRN/DTB slots.
`meta.json` confirms original kernel #29 and the selected Mesa remains
`fe7dda5e`; tests explicitly use `5a3fd8dd`, matching both earlier phases.
High-resolution timers are again disabled.

Five stages pass: 1,032 timing rounds, 22 neural inferences with exact
CPU agreement and intact guards, and 50 normal cross-context compute/native-
fence chains. Zero GPU resets, MMU errors or oopses.

Warm medians return to their pre-change values: empty-shader wall 2.102 ms,
64-job wall 253.207 ms, neural batch 257 wall 26.362 ms and batch 4096 wall
188.805 ms. Requested 10 µs sleeps again take median 3.998 ms. The original
kernel and all baseline module files remain unchanged, byte-verified before
and after the experiment. This tests the actual rollback rather than only
checking that backup files exist. No validation processes remain running.

The combined analyzer, timing comparison and checksummed build/boot records
are in the candidate directory. Raw logs and exact rollback runner are here.
