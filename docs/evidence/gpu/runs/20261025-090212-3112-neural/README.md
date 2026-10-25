# Two-layer neural network on V3D 3.3

Numbered Mesa `23.3.6-3112d69b`, kernel #29, `V3D_COMPUTE33=1`, no GLES
version override, shader cache disabled, Linux watchdog active. The test
asserts a V3D renderer and dispatches both network layers as compute shaders.

The dense 32 -> 16 ReLU -> 8 network uses deterministic generated weights.
All hidden values, output values and predicted classes match the CPU
reference exactly for batches 1, 65 and 257. Output-buffer prefix/suffix
guards remain intact. There are zero MMU errors, GPU resets or kernel oopses.
[Full result summary](summary.txt).

| Batch | Hidden/output values checked | CPU reference ms | GPU dispatch + synchronization ms |
| ---: | ---: | ---: | ---: |
| 1 | 16 / 8 | 0.004 | 12.679 |
| 65 | 1,040 / 520 | 0.102 | 43.108 |
| 257 | 4,112 / 2,056 | 0.410 | 113.884 |

Maximum absolute error is zero in both layers for every batch. This proves
the compute workload's correctness; the GPU path is substantially slower
than the CPU for this small model. GPU timings include dispatch and
synchronization overhead and do not isolate shader execution time. There
is no trained-model accuracy claim.

`run.py` invokes the neural test directly as a stage in the new candidate's
validation chain. Its `state.json` predecessor label is inherited from the
older wrapper and is not a prerequisite checked by this standalone run.
