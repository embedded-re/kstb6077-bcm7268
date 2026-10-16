// start.S - kboot entry: BOLT `go -64` (EL2) or `boot -64 -el3` (EL3, el3.S
// sets up EL3 and comes back here at EL2). MMU and caches off.

        .section .text.start, "ax"
        .global _start, el2_entry, jump_to
_start:
        msr     daifset, #0xf
        mrs     x0, CurrentEL
        cmp     x0, #0xc
        b.eq    el3_entry
        mov     x0, #0                          // x0 = 0: started at EL2
el2_entry:
        mov     x19, x0
        ldr     x0, =__bss_start
        ldr     x1, =__bss_end
1:      cmp     x0, x1
        b.hs    2f
        str     xzr, [x0], #8
        b       1b
2:      ldr     x0, =stack_top
        mov     sp, x0
        mov     x0, x19
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
