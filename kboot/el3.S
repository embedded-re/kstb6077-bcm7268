// el3.S - kboot's EL3 entry and its EL3 monitor.
//
// BOLT's `boot -64 -el3` starts kboot at EL3 (MMU and caches off, VBAR_EL3
// still at smm64, SCR_EL3 = 0x131). el3_entry then does what smm64 does
// before it starts an AArch64 program at EL2 (`go -64`), read from its code
// at 0x06400000 (docs/booting.md, "Our own EL3"): core set-up, the secure
// GIC set-up, SCR_EL3, the EL2 registers, the timers, and drops to EL2 at
// el2_entry. Our monitor replaces smm64 in its reserved region:
// el3_mon is copied to EL3_MON and VBAR_EL3 points at it.
//
// The monitor answers SMC calls from EL2 (SMCCC, x0 = function ID):
//   PSCI_VERSION (0x84000000)  -> 0x00000002 (v0.2, as smm64)
//   SYSTEM_RESET (0x84000009)  -> software reset (SUN_TOP_CTRL)
//   0x8600ff00 (OEM range)     -> 0x4b423301 ('KB3', 1): "kboot's monitor is EL3"
//   anything else              -> -1 (NOT_SUPPORTED)
// Any other exception taken to EL3 prints "kboot EL3: exception" and stops.

#define EL3_MON     0x06400000

        .section .text.start, "ax"
        .global el3_entry
el3_entry:
        // our monitor into smm64's place
        adr     x0, el3_mon
        adr     x1, el3_mon_end
        mov     x2, #EL3_MON
1:      ldr     x3, [x0], #8
        str     x3, [x2], #8
        cmp     x0, x1
        b.lo    1b
        dsb     sy
        ic      iallu
        dsb     sy
        isb
        mov     x0, #EL3_MON
        msr     vbar_el3, x0
        isb

        // core: lower ELs may access CPUECTLR; SMP coherency on (smm64 0x064028b0)
        mrs     x0, actlr_el3
        orr     x0, x0, #2
        msr     actlr_el3, x0
        mrs     x0, actlr_el2
        orr     x0, x0, #2
        msr     actlr_el2, x0
        mrs     x0, actlr_el1
        orr     x0, x0, #2
        msr     actlr_el1, x0
        mrs     x0, S3_1_C15_C2_1               // CPUECTLR_EL1
        orr     x0, x0, #0x40                   // SMPEN
        msr     S3_1_C15_C2_1, x0
        msr     cntvoff_el2, xzr
        isb

        // GIC-400, secure side (smm64 0x06402b10): all interrupts group 1 (non-secure)
        mrs     x4, S3_1_C15_C3_0               // CBAR: peripheral base
        and     x4, x4, #0xffffffffffffff00
        add     x1, x4, #0x1000                 // GICD
        mov     w0, #3
        str     w0, [x1]                        // GICD_CTLR: group 0 and 1 on (core 0)
        add     x1, x1, #0x80                   // GICD_IGROUPR0
        mov     w0, #-1
        str     w0, [x1]
        add     x2, x4, #0x1000
        ldr     w3, [x2, #4]                    // GICD_TYPER
        ands    w3, w3, #0x1f                   // ITLinesNumber = IGROUPR words after the first
        b.eq    3f
2:      str     w0, [x1, #4]!
        subs    w3, w3, #1
        b.ne    2b
3:      add     x1, x4, #0x2000                 // GICC
        mov     w0, #3
        str     w0, [x1]                        // GICC_CTLR
        mov     w0, #0x80
        str     w0, [x1, #4]                    // GICC_PMR

        // lower ELs: non-secure, HVC on, AArch64 (smm64 0x0640251c)
        mov     x0, #0x531
        msr     scr_el3, x0
        isb

        // EL2 registers (smm64 0x064026f0, called with HCR_EL2 = 0x80000000)
        mov     x0, #0x300000
        msr     cpacr_el1, x0
        mov     x0, #0x80000000
        msr     hcr_el2, x0
        ldr     x0, =0x30c50830
        msr     sctlr_el2, x0
        mrs     x0, cptr_el2
        ldr     x1, =0xffffffff7feffbff         // TCPAC, TTA, TFP off
        and     x0, x0, x1
        msr     cptr_el2, x0
        msr     cptr_el3, xzr                   // no FP/SIMD traps to EL3
        msr     hstr_el2, xzr
        mov     x0, #0x1f
        msr     mdcr_el2, x0
        msr     mdscr_el1, xzr

        // timers (smm64 0x064024a4)
        mrs     x0, cnthctl_el2
        orr     x0, x0, #3
        msr     cnthctl_el2, x0
        mrs     x0, cnthp_ctl_el2
        and     x0, x0, #~3
        orr     x0, x0, #1
        msr     cnthp_ctl_el2, x0
        mrs     x0, cntp_ctl_el0
        and     x0, x0, #~3
        orr     x0, x0, #1
        msr     cntp_ctl_el0, x0
        mrs     x0, cntv_ctl_el0
        and     x0, x0, #~3
        orr     x0, x0, #1
        msr     cntv_ctl_el0, x0
        mrs     x0, midr_el1
        msr     vpidr_el2, x0
        mrs     x0, mpidr_el1
        msr     vmpidr_el2, x0
        isb

        // to EL2h, interrupts masked, like smm64's SPSR 0x3c9
        adr     x0, el2_entry
        msr     elr_el3, x0
        mov     x0, #0x3c9
        msr     spsr_el3, x0
        mov     x0, #1                          // x0 = 1: kboot came through EL3
        eret

// ---------------- the EL3 monitor (runs at EL3_MON, position-independent) ----------------

        .balign 2048
el3_mon:
        .rept   8                               // current EL with SP0 and SPx: 8 entries
        b       el3_bad
        .balign 128
        .endr
        b       el3_sync_lower                  // lower EL, AArch64: synchronous
        .balign 128
        .rept   3
        b       el3_bad
        .balign 128
        .endr
        .rept   4                               // lower EL, AArch32
        b       el3_bad
        .balign 128
        .endr

el3_sync_lower:
        mrs     x1, esr_el3
        lsr     x1, x1, #26
        cmp     x1, #0x17                       // SMC from AArch64
        b.ne    el3_bad
        mov     w1, #0x84000000
        cmp     w0, w1
        b.ne    4f
        mov     x0, #2                          // PSCI_VERSION: 0.2
        eret
4:      add     w1, w1, #9
        cmp     w0, w1
        b.ne    5f
        mov     x1, #0x4304                     // SYSTEM_RESET: SUN_TOP_CTRL software reset
        movk    x1, #0xf040, lsl #16
        mov     w2, #1
        str     w2, [x1]
        str     w2, [x1, #4]
6:      wfi
        b       6b
5:      mov     w1, #0xff00
        movk    w1, #0x8600, lsl #16
        cmp     w0, w1
        b.ne    10f
        mov     x0, #0x3301                     // 'KB3', version 1
        movk    x0, #0x4b42, lsl #16
        eret
10:     mov     x0, #-1                         // NOT_SUPPORTED
        eret

el3_bad:
        adr     x1, el3_msg
        mov     x2, #0xc000
        movk    x2, #0xf040, lsl #16            // UART0
7:      ldrb    w3, [x1], #1
        cbz     w3, 9f
8:      ldr     w4, [x2, #0x14]
        tbz     w4, #5, 8b
        str     w3, [x2]
        b       7b
9:      wfe
        b       9b
el3_msg:
        .asciz  "\r\nkboot EL3: exception, stopped\r\n"
        .balign 8
el3_mon_end:
