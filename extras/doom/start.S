// start.S - entry point of the Doom port. BOLT `go -64` jumps here at EL2,
// MMU and caches off (docs/03-running-code.md).
//   1. stack, exception vectors (faults print ESR/ELR/FAR instead of hanging)
//   2. FP/SIMD on: clear CPTR_EL2.TFP
//   3. zero .bss
//   4. identity map + MMU + caches on (as docs/evidence/display/vsync_probe.s):
//        0x00000000-0x3fffffff  Normal write-back, except
//        0x00600000-0x007fffff  Normal non-cacheable (DMA_BASE: USB, ../usb)
//        0x40000000-0x7fffffff  Normal write-back, except
//        0x7d600000-0x7dffffff  Normal non-cacheable (both framebuffers)
//        0xc0000000-0xffffffff  Device-nGnRE, execute-never
//   5. main()
// Started by the monitor (mon/), x1 = MON_MAGIC and x2 = the monitor's resume
// address; both are kept in mon_magic / mon_resume (dg_kstb.c returns there).

        .section .text.start, "ax"
        .global _start
_start:
        ldr     x0, =mon_magic                  // .data: not cleared below
        str     x1, [x0]
        ldr     x0, =mon_resume
        str     x2, [x0]
        ldr     x0, =stack_top
        mov     sp, x0
        adr     x0, vectors
        msr     vbar_el2, x0

        mrs     x0, cptr_el2
        bic     x0, x0, #(1 << 10)              // TFP = 0: FP/SIMD not trapped
        msr     cptr_el2, x0
        isb

        ldr     x0, =__bss_start
        ldr     x1, =__bss_end
1:      cmp     x0, x1
        b.hs    2f
        stp     xzr, xzr, [x0], #16
        b       1b
2:
        bl      mmu_on
        bl      main
3:      wfe
        b       3b

mmu_on:
        ldr     x0, =pt_l0g                     // 0x00000000-0x3fffffff in 2 MB blocks: WB,
        mov     x2, #0                          // except DMA_BASE 0x00600000 (index 3): NC
1:      cmp     x2, #3
        mov     x3, #0x705                      // Normal WB (attr 1), inner shareable, AF
        mov     x4, #0x709                      // Normal non-cacheable (attr 2)
        csel    x3, x4, x3, eq
        orr     x3, x3, x2, lsl #21
        str     x3, [x0, x2, lsl #3]
        add     x2, x2, #1
        cmp     x2, #512
        b.lo    1b
        ldr     x0, =pt_l1
        ldr     x1, =pt_l0g
        orr     x1, x1, #3
        str     x1, [x0]                        // 0x00000000-0x3fffffff: table
        ldr     x1, =pt_l2
        orr     x1, x1, #3
        str     x1, [x0, #8]                    // 0x40000000-0x7fffffff: 2 MB blocks in L2
        str     xzr, [x0, #16]                  // 0x80000000-0xbfffffff: unmapped
        ldr     x1, =(0xc0000000 | 0x0040000000000405 | (3 << 2))
        str     x1, [x0, #24]                   // 0xc0000000-0xffffffff: Device-nGnRE (attr 3), XN
        ldr     x0, =pt_l2
        ldr     x1, =(0x40000000 | 0x705)
        mov     x2, #0
1:      ldr     x3, =0x709                      // Normal non-cacheable (attr 2)
        cmp     x2, #491                        // 0x7d600000 ...
        b.lo    2f
        cmp     x2, #495                        // ... 0x7dffffff
        b.hi    2f
        bic     x4, x1, #0xfff
        orr     x4, x4, x3
        b       3f
2:      mov     x4, x1
3:      str     x4, [x0, x2, lsl #3]
        add     x1, x1, #0x200000
        add     x2, x2, #1
        cmp     x2, #512
        b.lo    1b
        ldr     x0, =0x0444ff00                 // MAIR: 0 Device-nGnRnE, 1 Normal WB, 2 Normal NC, 3 Device-nGnRE
        msr     mair_el2, x0
        ldr     x0, =0x80800020                 // TCR: T0SZ 32 (4 GB), 4 KB pages, non-cacheable walks
        msr     tcr_el2, x0
        ldr     x0, =pt_l1
        msr     ttbr0_el2, x0
        dsb     sy
        tlbi    alle2
        dsb     sy
        ic      iallu
        dsb     sy
        isb
        mrs     x0, sctlr_el2
        orr     x0, x0, #(1 << 0)               // M
        orr     x0, x0, #(1 << 2)               // C
        orr     x0, x0, #(1 << 12)              // I
        msr     sctlr_el2, x0
        isb
        ret

// Any exception: report it from C (it only uses the UART), then stop.
        .balign 2048
vectors:
        .rept 16
        ldr     x0, =stack_top                  // fresh stack: the fault may be a bad sp
        mov     sp, x0
        mrs     x0, esr_el2
        mrs     x1, elr_el2
        mrs     x2, far_el2
        bl      exception
        .balign 128
        .endr

        .ltorg

        .data
        .balign 8
        .global mon_magic, mon_resume
mon_magic:  .quad 0
mon_resume: .quad 0

        .bss
        .balign 4096
pt_l1:  .space  4096
pt_l2:  .space  4096
pt_l0g: .space  4096
        .balign 16
        .space  0x40000                         // 256 KB stack
stack_top:
