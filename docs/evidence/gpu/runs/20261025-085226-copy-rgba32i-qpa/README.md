# RGBA32I copy diagnostic before the fix

Numbered Mesa `23.3.6-b1e71af4`, no GLES version override. The official
renderbuffer-to-renderbuffer RGBA32I case fails reproducibly without GPU
faults. [result.qpa](result.qpa) contains the detailed comparison and images.

The first phase, which separates image creation from copying with test
iterations, passes. The second creates/clears the images and copies in the
same iteration. Its source matches the reference, but its destination does
not. Both renderbuffers are 65x63; the sequence includes a full copy followed
by three partial copies.

The fixed resource-map path submits pending writers before replacing the
destination BO. The [isolated scratch rerun](../20261025-085348-gles31-copy-discard-writer/summary.txt)
passes, as does the [2,011-case renderbuffer sweep](../20261025-085423-gles31-copy-renderbuffer-scratch/summary.txt).
