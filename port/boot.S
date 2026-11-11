// boot.S - jump from kmon into the arm64 kernel (Documentation/arch/arm64/booting.rst).
//
// kmon's `go` leaves the MMU and caches off with the data cache cleaned, but
// passes x1 = 'KMON' and x2 = its resume address. Linux wants x0 = the DTB
// and x1-x3 = 0.

#include "layout.h"

        .global _start
_start:
        ldr     x0, =DTB_ADDR
        mov     x1, xzr
        mov     x2, xzr
        mov     x3, xzr
        ldr     x4, =KERNEL_ADDR
        br      x4
        .ltorg
