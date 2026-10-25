# Numbered patch-0031 compute fence/reset gate

Mesa `23.3.6-3112d69b`, kernel #29, no version override. The same
[compute synchronization/reset test](../20261025-083310-compute-sync-recovery/README.md)
passes 350 chains: 200 normal plus three 50-chain runs containing a deliberate
CL stall/reset. Every pre-reset compute fence is confirmed pending before
its corresponding kernel reset. Both 4,097-word buffers and their guards
match throughout. There are three expected resets, zero MMU errors and
zero kernel oopses. [Summary and timestamps](summary.txt).
