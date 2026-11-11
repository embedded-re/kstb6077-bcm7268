// start.S - entry points, caches/MMU, exceptions and safe memory access for
// the monitor. Runs at EL2 (BOLT `go -64`).
//
// Entry points:
//   _start     from BOLT: MMU off. Clears .bss.
//   resume     a program started with `go` jumps here to come back, x0 = status,
//              any MMU/cache state. Its address is passed in x2 at the
//              program's entry, with x1 = MON_MAGIC (kstb.h).
// Both turn caches and MMU off (cleaning the data cache), set up the stack,
// vectors, FP/SIMD, then the same identity map as extras/doom/start.S, and call
// mon_main(how, status): how = 0 start, 1 program returned, 2 exception.
//
// IRQs (only after `irq on`, irq.c) go to irq_entry and return.
// Exceptions: while `fault_armed` is set (safe_read / safe_write), a data
// abort is recorded in `fault_esr` and the faulting load/store is skipped,
// as in docs/evidence/periph/periph_probe.s (all aborts seen on this board
// were synchronous, ESR 96000210). Any other exception saves the registers
// in `exc_frame` and restarts the monitor with how = 2, so a crash in the
// monitor or in a program ends at the prompt instead of hanging.

        .section .text.start, "ax"
        .global _start, resume
_start:
        mov     x19, #0
        mov     x20, #0
        b       common
resume:
        mov     x19, #1
        mov     x20, x0
        b       common
exc_restart:
        mov     x19, #2
        mov     x20, #0

common:
        msr     daifset, #0xf
        bl      mmu_off
        msr     spsel, #1                       // use SP_EL2, the stack exceptions switch to
        ldr     x0, =stack_top
        mov     sp, x0
        adr     x0, vectors
        msr     vbar_el2, x0
        mrs     x0, cptr_el2
        bic     x0, x0, #(1 << 10)              // TFP = 0: FP/SIMD not trapped
        msr     cptr_el2, x0
        isb
        cbnz    x19, 2f                         // .bss only on a fresh start
        ldr     x0, =__bss_start
        ldr     x1, =__bss_end
1:      cmp     x0, x1
        b.hs    2f
        stp     xzr, xzr, [x0], #16
        b       1b
2:      bl      mmu_on
        mov     x0, x19
        mov     x1, x20
        bl      mon_main
3:      wfe
        b       3b

// ---------------- caches and MMU ----------------

// Clean and invalidate the whole data cache by set/way (all levels up to LoC).
// Clobbers x0-x11.
        .global dcache_flush_all
dcache_flush_all:
        mrs     x0, clidr_el1
        and     w3, w0, #0x07000000
        lsr     w3, w3, #23                     // LoC * 2
        cbz     w3, 5f
        mov     w10, #0                         // level * 2
1:      add     w2, w10, w10, lsr #1            // level * 3
        lsr     w1, w0, w2
        and     w1, w1, #7                      // cache type at this level
        cmp     w1, #2
        b.lt    4f                              // no data cache
        msr     csselr_el1, x10
        isb
        mrs     x1, ccsidr_el1
        and     w2, w1, #7
        add     w2, w2, #4                      // log2(line length)
        ubfx    w4, w1, #3, #10                 // ways - 1
        clz     w5, w4                          // way shift
        ubfx    w7, w1, #13, #15                // sets - 1
2:      mov     w9, w4
3:      lsl     w6, w9, w5
        orr     w11, w10, w6
        lsl     w6, w7, w2
        orr     w11, w11, w6
        dc      cisw, x11
        subs    w9, w9, #1
        b.ge    3b
        subs    w7, w7, #1
        b.ge    2b
4:      add     w10, w10, #2
        cmp     w3, w10
        b.gt    1b
5:      dsb     sy
        isb
        ret

// MMU, data and instruction caches off, everything written back to DRAM.
// Uses no memory (the stack may be stale). Clobbers x0-x12.
mmu_off:
        mov     x12, x30
        mrs     x0, sctlr_el2
        bic     x0, x0, #(1 << 0)               // M
        bic     x0, x0, #(1 << 2)               // C
        bic     x0, x0, #(1 << 12)              // I
        msr     sctlr_el2, x0
        isb
        bl      dcache_flush_all
        ic      iallu
        tlbi    alle2
        dsb     sy
        isb
        ret     x12

// Identity map (as extras/doom/start.S, plus the DMA block):
//   0x00000000-0x3fffffff  Normal write-back, except
//   0x00600000-0x007fffff  Normal non-cacheable (DMA_BASE: USB descriptors)
//   0x40000000-0x7fffffff  Normal write-back, except
//   0x7d600000-0x7dffffff  Normal non-cacheable (framebuffers, audio ring)
//   0x80000000-0xbfffffff  unmapped (a read gives a translation fault, caught)
//   0xc0000000-0xffffffff  Device-nGnRE, execute-never
mmu_on:
        ldr     x0, =pt_l0g                     // first GB in 2 MB blocks: WB, except
        mov     x1, #0x705                      // the DMA block 0x00600000 (index 3): NC
        mov     x2, #0
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
        orr     x1, x1, #3                      // table
        str     x1, [x0]
        ldr     x1, =pt_l2
        orr     x1, x1, #3
        str     x1, [x0, #8]
        str     xzr, [x0, #16]
        ldr     x1, =(0xc0000000 | 0x0040000000000405 | (3 << 2))
        str     x1, [x0, #24]
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

// ---------------- running programs ----------------

// void run_program(uint64_t entry): like BOLT's `go -64`: MMU and caches off,
// DAIF masked, x0 = 0, x1 = MON_MAGIC, x2 = resume. Doesn't return (the
// program comes back through `resume`, if it knows how).
        .global run_program
run_program:
        msr     daifset, #0xf
        mov     x19, x0
        bl      mmu_off
        mov     x0, #0
        ldr     x1, =0x4b4d4f4e
        adr     x2, resume
        mov     x3, #0
        br      x19

// void run_linux(uint64_t entry, uint64_t dtb): the arm64 boot protocol
// (Documentation/arch/arm64/booting.rst): MMU and caches off as for `go`,
// x0 = the DTB, x1-x3 = 0. Linux doesn't come back.
        .global run_linux
run_linux:
        msr     daifset, #0xf
        mov     x19, x0
        mov     x20, x1
        bl      mmu_off
        mov     x0, x20
        mov     x1, #0
        mov     x2, #0
        mov     x3, #0
        br      x19

// ---------------- safe memory access ----------------

// int safe_read(uint64_t addr, int size, uint64_t *val)
// int safe_write(uint64_t addr, int size, uint64_t val)
// size = 1, 2, 4 or 8. Return 0, or the ESR of the abort the access caused.
        .global safe_read, safe_write
safe_read:
        adr     x9, fault_esr
        str     xzr, [x9]
        adr     x10, fault_armed
        mov     w11, #1
        str     w11, [x10]
        mov     x3, #0
        cmp     w1, #1
        b.eq    1f
        cmp     w1, #2
        b.eq    2f
        cmp     w1, #4
        b.eq    4f
        ldr     x3, [x0]
        b       9f
1:      ldrb    w3, [x0]
        b       9f
2:      ldrh    w3, [x0]
        b       9f
4:      ldr     w3, [x0]
9:      dsb     sy
        isb
        msr     daifclr, #4                     // a pending SError is taken here
        isb
        msr     daifset, #4
        str     wzr, [x10]
        str     x3, [x2]
        ldr     x0, [x9]
        ret

safe_write:
        adr     x9, fault_esr
        str     xzr, [x9]
        adr     x10, fault_armed
        mov     w11, #1
        str     w11, [x10]
        cmp     w1, #1
        b.eq    1f
        cmp     w1, #2
        b.eq    2f
        cmp     w1, #4
        b.eq    4f
        str     x2, [x0]
        b       9f
1:      strb    w2, [x0]
        b       9f
2:      strh    w2, [x0]
        b       9f
4:      str     w2, [x0]
9:      dsb     sy
        isb
        msr     daifclr, #4
        isb
        msr     daifset, #4
        str     wzr, [x10]
        ldr     x0, [x9]
        ret

// ---------------- exception vectors ----------------

        .macro  vec type
        .balign 128
        msr     tpidr_el2, x0                   // free x0 without touching the stack
        adr     x0, exc_scratch
        stp     x1, x2, [x0]
        mov     x1, #\type
        b       exc_common
        .endm

        .macro  vec_irq
        .balign 128
        b       irq_entry
        .endm

        .balign 2048
vectors:
        vec 0                                   // current EL, SP_EL0: sync
        vec 1                                   //   IRQ
        vec 2                                   //   FIQ
        vec 3                                   //   SError
        vec 0                                   // current EL, SP_ELx: sync
        vec_irq                                 //   IRQ: irq.c (the monitor runs here)
        vec 2
        vec 3
        vec 0                                   // lower EL, AArch64
        vec 1
        vec 2
        vec 3
        vec 0                                   // lower EL, AArch32
        vec 1
        vec 2
        vec 3

// x0 = exc_scratch (x1, x2 saved there), x1 = type, the original x0 in TPIDR_EL2
exc_common:
        adr     x2, fault_armed
        ldr     w2, [x2]
        cbz     w2, fatal
        cmp     x1, #0
        b.eq    1f
        cmp     x1, #3
        b.ne    fatal                           // IRQ/FIQ while armed: not expected
1:      mrs     x2, esr_el2
        str     x2, [x0, #16]                   // exc_scratch + 16 = fault_esr
        cbnz    x1, 2f                          // SError: ELR is already past the access
        mrs     x2, elr_el2
        add     x2, x2, #4                      // sync abort: skip the load/store
        msr     elr_el2, x2
2:      ldp     x1, x2, [x0]
        mrs     x0, tpidr_el2
        eret

// IRQ at EL2 on SP_EL2: save what a C call may clobber (x0-x18, x29, x30,
// ELR, SPSR) on the interrupted code's stack, call irq_handler (irq.c, no
// FP/SIMD), return to where it was. It doesn't touch exc_scratch, so an IRQ
// during safe_read/safe_write is harmless.
irq_entry:
        sub     sp, sp, #192
        stp     x0, x1, [sp, #0]
        stp     x2, x3, [sp, #16]
        stp     x4, x5, [sp, #32]
        stp     x6, x7, [sp, #48]
        stp     x8, x9, [sp, #64]
        stp     x10, x11, [sp, #80]
        stp     x12, x13, [sp, #96]
        stp     x14, x15, [sp, #112]
        stp     x16, x17, [sp, #128]
        stp     x18, x29, [sp, #144]
        mrs     x0, elr_el2
        stp     x30, x0, [sp, #160]
        mrs     x0, spsr_el2
        str     x0, [sp, #176]
        bl      irq_handler
        ldr     x0, [sp, #176]
        msr     spsr_el2, x0
        ldp     x30, x0, [sp, #160]
        msr     elr_el2, x0
        ldp     x18, x29, [sp, #144]
        ldp     x16, x17, [sp, #128]
        ldp     x14, x15, [sp, #112]
        ldp     x12, x13, [sp, #96]
        ldp     x10, x11, [sp, #80]
        ldp     x8, x9, [sp, #64]
        ldp     x6, x7, [sp, #48]
        ldp     x4, x5, [sp, #32]
        ldp     x2, x3, [sp, #16]
        ldp     x0, x1, [sp, #0]
        add     sp, sp, #192
        eret

// Unexpected: save everything in exc_frame, restart the monitor.
fatal:
        adr     x2, exc_frame
        str     x1, [x2, #(34 * 8)]             // type
        ldp     x0, x1, [x0]                    // original x1, x2
        stp     x0, x1, [x2, #8]
        mrs     x0, tpidr_el2
        str     x0, [x2]                        // x0
        stp     x3, x4, [x2, #24]
        stp     x5, x6, [x2, #40]
        stp     x7, x8, [x2, #56]
        stp     x9, x10, [x2, #72]
        stp     x11, x12, [x2, #88]
        stp     x13, x14, [x2, #104]
        stp     x15, x16, [x2, #120]
        stp     x17, x18, [x2, #136]
        stp     x19, x20, [x2, #152]
        stp     x21, x22, [x2, #168]
        stp     x23, x24, [x2, #184]
        stp     x25, x26, [x2, #200]
        stp     x27, x28, [x2, #216]
        stp     x29, x30, [x2, #232]
        mov     x0, sp
        str     x0, [x2, #248]                  // sp
        mrs     x0, elr_el2
        mrs     x1, esr_el2
        stp     x0, x1, [x2, #256]              // elr, esr
        mrs     x0, far_el2
        mrs     x1, spsr_el2
        stp     x0, x1, [x2, #(35 * 8)]         // far, spsr
        b       exc_restart

        .ltorg

        .data
        .balign 8
exc_scratch:    .quad 0, 0                      // x1, x2 of the interrupted code
fault_esr:      .quad 0                         // must follow exc_scratch (+16)
fault_armed:    .word 0
        .balign 8
        .global exc_frame
exc_frame:      .space 37 * 8                   // x0-x30, sp, elr, esr, type, far, spsr

        .bss
        .balign 4096
pt_l1:  .space  4096
pt_l2:  .space  4096
pt_l0g: .space  4096
        .set    stack_top, 0x00850000           // 64 KB stack 0x00840000-0x0084ffff, outside the
                                                // monitor (kstb.h STACK)
