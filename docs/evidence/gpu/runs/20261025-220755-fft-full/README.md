# Post-reboot FFT sizes and batches

Seven complex FP32 FFT cases, lengths128–1024 and batches1–64. Each case
executes six forward/inverse rounds, including one cold round. PASS, followed
by native fences and graphics checks. See the [combined report](../20261025-220836-fft-boundary/README.md)
for correctness, timing scope and existing scheduler warnings. No GPU reset
observed. This source accepted N<=1024; the later boundary gate only raised
the accepted maximum to2048. Raw logs, run script, state and source retained.
