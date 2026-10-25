# Initial compute fence/recovery check

Kernel #29, numbered Mesa `23.3.6-b1e71af4`, `V3D_COMPUTE33=1`, no version
override. 200 normal cross-context compute/native-fence chains pass, then
50 more pass with one deliberately stalled CL job and one kernel reset.
Every chain checks both 4,097-word SSBOs and surrounding 4 KiB guards.
No MMU error or oops occurred.

The [repeat run](../20261025-083310-compute-sync-recovery/README.md) adds an
explicit zero-time fence poll and timestamp before recovery; use that run
for the stronger claim that a compute fence was pending before each reset.
