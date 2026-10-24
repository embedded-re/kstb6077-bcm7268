Incomplete diagnostic run, intentionally interrupted after atomic-counter failures; not a conformance total.
The counter mask-order defect is reproduced and corrected in the later counter33-msf-order run.
The ResourceError followed an incorrect decrement underflow; CTS attempted a 16 GB CPU allocation.
The old runner labeled unexecuted cases after ResourceError as Missing. Those labels do not prove missing tests. The runner now retries such cases.
Any KeyboardInterrupt from SIGINT records the deliberate stop, not a spontaneous GPU failure.
