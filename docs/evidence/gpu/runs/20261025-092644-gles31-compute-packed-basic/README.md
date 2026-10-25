# Rejected workgroup-packing experiment

The experimental patch in this directory tries packing independent
workgroups into additional tile columns. It is **reverted and absent from
the numbered packages**. Its 195-case run produces 185 Pass, six Crash and
four Fail, with five GPU resets, zero MMU errors and zero kernel oopses.
It is not a passing compute implementation.

The established one-column/16-workgroup layout is restored for the retained
indirect-limits fix. `/tmp/kstb-compute-packed/v3d_dri.so` was renamed with an
`.UNSAFE` suffix to prevent accidental reuse. Validate the restored numbered
driver after these resets before attributing any further result to it.
