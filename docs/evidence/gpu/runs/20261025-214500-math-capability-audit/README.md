# CPU/GPU mathematical capability audit — 2026-10-25

Current installed native Mesa **23.3.6-r34**, inner `8b05308`, kernel #30.
The [report](../../../../../linux/compute-limits.md) interprets these results.
CPU execution plus GPU queries and shader front-end/link checks only:
**zero new draws/dispatches**, no new reset test, no installs or driver edits.
The user's Sway/Firefox/game sessions were preserved. Existing kernel
`drm_sched_entity_init: called with uninitialized scheduler` warnings were
observed; these checks do not establish a warning-free kernel.

## Results and boundaries

- CPU `0x42/0x100` Brahma B53, four online cores; NEON FP32/FP64 FMA PASS.
- No advertised FP16 arithmetic, BF16, integer dot, I8MM, SVE or LSE.
  C64 atomics nevertheless PASS, lock-free, via exclusive loads/stores.
- Software binary128, compiler integer128, subnormals, complex FP64 FFT and
  Cholesky solution/residual checks PASS. New timings are live-desktop,
  unoptimized correctness probes, not maximum CPU throughput measurements.
- Python Decimal precision-210 sqrt and 4096-bit modular power PASS. Decimal
  squared residual rounds to zero at that context; it is not an independent
  verification of every digit. Modulus is not asserted prime.
- Default GLES3.0; `V3D_COMPUTE33=1` gives GLES3.1 without a version override.
- Workgroup128, shared16384, SSBO block134217728; compute blocks16/images8.
- Eleven expected shader acceptance/rejection checks PASS: FP32/int32,
  local128 and shared16384 accepted; FP64, int64, explicit FP16, float atomic
  add, subgroups, local129, shared16388 and recursion rejected.
- Shader compile/link acceptance does not prove backend/runtime correctness.
  Dispatched-GPU coverage and isolated performance belong to earlier runs,
  especially [the GEMM decision](../20261025-151108-gemm-coprocessor/README.md)
  on the older 33-patch `85a951cd` build. They were not rerun on r34 here.

## Reproduction

Archived C sources match the tracked `linux/gpu-test/` versions at this
milestone. Native-system GL binaries have no `/opt` RPATH. From repo root:

```sh
make -C linux/gpu-test cpu-math
clang --target=aarch64-alpine-linux-musl \
  --sysroot="$PWD/linux/build/sysroot" -fuse-ld=lld -O2 -Wall \
  -o linux/build/math-audit/kstb-gl-caps linux/gpu-test/kstb-gl-caps.c \
  -Llinux/build/mesa-system-prep/stage/usr/lib -lEGL -lGLESv2 -lgbm
clang --target=aarch64-alpine-linux-musl \
  --sysroot="$PWD/linux/build/sysroot" -fuse-ld=lld -O2 -Wall \
  -o linux/build/math-audit/kstb-math-shader-check linux/gpu-test/kstb-math-shader-check.c \
  -Llinux/build/mesa-system-prep/stage/usr/lib -lEGL -lGLESv2 -lgbm
```

CPU rule uses `-O2 -Wall -Wextra -mcpu=cortex-a53 -ffp-contract=off -lm`;
the target is a compiler selection, not CPU identification. Transfer binaries
to `/root/math-audit/`, then on the board:

```sh
cd /root/math-audit
./kstb-cpu-math-test > cpu-math.txt
./kstb-gl-caps 3 > gpu-default-caps.txt
V3D_COMPUTE33=1 ./kstb-gl-caps 3.1 > gpu-optin-caps.txt
V3D_COMPUTE33=1 ./kstb-math-shader-check > shader-check.txt
python3 python-check.py > python-math.json
```

`state.json` records executable hashes; `driver.sha256` records the native
DRI module. Identity/kernel/RAM snapshots are retained. Disassembly excerpts
confirm actual FMA, binary128 helper and atomic exclusive instructions.
The final Python result was regenerated from the archived script after the
initial exploratory check. The run name identifies this audit's beginning,
not the timestamp of every file. No benchmark remains running.

Offline validation: `python3 analyze.py` in this directory checks recorded
results and source hashes; it does not execute a GPU workload.
