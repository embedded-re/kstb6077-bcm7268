# Post-reboot FFT smoke gate

128-point complex FP32 GPU FFT/inverse versus FFTW double reference, with
FFTW FP32 one-/four-thread baselines. Six rounds including one cold round.
PASS, followed by native fences and graphics checks. Retained kernel warnings
are described in the [combined report](../20261025-220836-fft-boundary/README.md).
No GPU reset observed. This source accepted N<=1024; the later boundary gate
only raised the accepted maximum to2048. Raw logs, run script, state and source
are retained here.
