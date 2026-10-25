# Incomplete indirect-draw sweep with the limits fix

This run was intentionally stopped for review after large compute-generated
draws were killed. It records 107 cases: 99 Pass, six NotSupported due to
SSBO size limits, and two Timeout results. No GPU reset, MMU error or kernel
oops occurred. The remaining cases were not completed.

Both timed-out cases generate a 500x500 grid using one-invocation workgroups.
The runner labels exit `-9` as Timeout. CTS also has a 30-second watchdog
interval (`framework/common/tcuApp.hpp`); the precise termination source
needs a standalone diagnostic retaining the process output. Do not classify
this as a proven hardware limit or a successful conformance run.

The separate 157-case sweep excludes the 50 `compute_interop.large` cases
and passes completely. That narrower result establishes the native packet
fix; the large-workload problem remains open.
