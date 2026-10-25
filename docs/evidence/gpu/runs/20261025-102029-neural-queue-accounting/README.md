# Companion neural queue-accounting measurements

Part of the [compute latency milestone](../20261025-101822-compute-latency-milestone/README.md).
The same kernel #29 and Mesa patch-0032 driver are unchanged. The neural
harness now optionally dumps per-file queue counters when
`KSTB_NEURAL_STATS=1`, before the first round and after each measured round,
outside timing. Its new executable hash is recorded in `meta.json`.

Eight stages / 88 inferences pass, with changed inputs every round, every
layer exactly equal to CPU and all classifications/guards correct. Zero
resets, MMU errors or oopses. `sleep-calibration.json` also retains 101 raw
samples each for requested userspace sleeps of 0/10/100/1,000 microseconds.

`run.py` is the exact board runner. Its original inherited top docstring
says "neural-compute measurement/reliability milestone"; this invocation
only does the eight queue-accounting stages and sleep calibration, with
no deliberate reset. See `results.json` and `state.json` for actual scope.
The analyzer and combined summary are in the main milestone directory.
