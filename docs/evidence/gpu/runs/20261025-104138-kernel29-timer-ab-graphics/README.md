# Immediate kernel-29 graphics baseline for timer A/B

On unchanged kernel #29 and Mesa `23.3.6-5a3fd8dd`, the two focused suites
complete: GLES2 205 Pass; GLES3 369 Pass/29 NotSupported. Zero GPU resets,
MMU errors or oopses. The test runner clears compute opt-in for these graphics
stages. The cases cover the existing CL/MMU regressions, texture-unit spills,
UBOs, framebuffer/MSAA, mipmaps and sample coverage.

The subsequent [RAM candidate](../20261025-104958-hrtimer-ab-gpu/README.md)
repeats all 603 cases with exactly the same per-case statuses. The original
`run.py`, output logs, results, environment/hashes and kernel span are
retained. CTS data-directory symlinks are excluded from the archive.
