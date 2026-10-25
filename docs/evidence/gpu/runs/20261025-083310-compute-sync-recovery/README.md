# Pending compute fences across three resets

Kernel #29, numbered Mesa `23.3.6-b1e71af4`, `V3D_COMPUTE33=1`, no GLES
version override. The Linux watchdog is active throughout the run.

`kstb-compute-sync-test` alternates two shared GLES 3.1 contexts. One compute
shader writes an iteration-dependent integer pattern, exports a native
fence, and the other context imports/server-waits on it before a second
compute shader transforms the data. A second exported fence must signal.
Both 4,097-word buffers and their 4 KiB prefix/suffix guards are checked
every round. There is no producer CPU wait in a normal round.

The reset helper submits a render-only CL that waits on semaphores without
signaling them. During that stall, the test submits compute and exports its
fence. A zero-time poll verifies that fence is unsignaled; its monotonic
timestamp precedes the kernel's reset timestamp. The scheduler retires the
guilty helper job; the same live compute contexts and buffers continue.

- 200 normal chains: exact, no reset.
- Three runs of 50 chains each: exact, one deliberate reset per run.
- Three pending-fence timestamps precede their corresponding resets; see
  [summary.txt](summary.txt).
- Total: **350 exact chains, three expected resets, zero MMU errors or
  kernel oopses**. Kernel `ERR_STAT=0x1000` accompanies each intentional hang.

This exercises queued compute, native-fence dependencies, buffer reuse and
continued use of existing contexts after reset. It does not cover every
possible reset point inside an executing compute shader or hours-long load.
The helper's comment was simplified during the run; executable statements
were unchanged.

The [195 official compute cases](../20261025-083501-gles31-post-reset-compute/summary.txt)
subsequently all pass, with zero MMU errors, resets or oopses.
