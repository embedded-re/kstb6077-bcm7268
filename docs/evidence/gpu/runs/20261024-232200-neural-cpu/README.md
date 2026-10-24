# Neural-network reference preparation

`kstb-neural-test.c` cross-builds for the board. Its deterministic dense
32-input -> 16-hidden ReLU -> 8-output model runs the CPU reference for
batches 1, 65 and 257; output and binary hash are in [cpu.txt](cpu.txt).
These runs use `--cpu-only` and do not create a GPU context. They establish
the reference outputs, **not GPU correctness or performance**.

The GPU path uses two GLES 3.1 compute dispatches, checks every hidden and
output value against that reference, checks argmax classifications and
checks 4 KiB guards around each output SSBO. It requires a V3D renderer.
The model uses fixed generated weights; this is a compute validation
workload, with no trained-model accuracy claim.

The generated compute sources also pass `glslangValidator -S comp` syntax
checks. Hardware execution remains pending behind the full GLES 3.1 run.
