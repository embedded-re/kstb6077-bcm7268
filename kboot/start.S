// start.S - kboot entry (BOLT `go -64`: EL2, MMU and caches off).

        .section .text.start, "ax"
        .global _start, jump_to
_start:
        msr     daifset, #0xf
        ldr     x0, =__bss_start
        ldr     x1, =__bss_end
1:      cmp     x0, x1
        b.hs    2f
        str     xzr, [x0], #8
        b       1b
2:      ldr     x0, =stack_top
        mov     sp, x0
        bl      kboot_main
3:      wfe
        b       3b

// jump_to(addr): start an image the way BOLT's go -64 does (MMU and caches
// still off, as at entry). The image was written with data stores, so the
// instruction cache is invalidated first.
jump_to:
        mov     x4, x0
        ic      iallu
        dsb     sy
        isb
        mov     x0, #0
        mov     x1, #0
        mov     x2, #0
        mov     x3, #0
        br      x4
