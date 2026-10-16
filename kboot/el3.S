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
//   PSCI_VERSION  (0x84000000) -> 0x00000002 (v0.2, as smm64)
//   CPU_ON        (0xc4000003) x1 = MPIDR (core 1-3), x2 = entry, x3 = context
//                              -> 0, -2 bad core, -4 already on, -5 on pending,
//                                 -6 power-up timed out
//   AFFINITY_INFO (0xc4000004) x1 = MPIDR, x2 = 0 -> 0 on, 1 off, 2 on pending
//   SYSTEM_RESET  (0x84000009) -> software reset (SUN_TOP_CTRL)
//   0x8600ff00 (OEM range)     -> 0x4b423302 ('KB3', 2): "kboot's monitor is EL3"
//   anything else              -> -1 (NOT_SUPPORTED)
// x4-x30 are kept. Any other exception taken to EL3 prints "kboot EL3:
// exception" and stops.
//
// CPU_ON does what smm64's does (docs/hardware/cpu-cores.md): power the core
// up (CPU_PWR n), write el3_warm to its boot address, take it out of reset
// (CPU_RESET bit n). The core starts at EL3 at el3_warm, sets itself up
// like core 0 (el3_core) and drops to EL2 at the entry, x0 = context id.

#define EL3_MON     0x06400000
#define EL3_DATA    (EL3_MON + 0xe000)      // per core: entry, context, state (32 bytes)
#define EL3_STACK   (EL3_MON + 0x10000)     // core n: 1 KB below EL3_STACK - n * 1 KB
#define CPU_RESET   0xf020508c              // bit n set: core n held in reset
#define CPU_PWR0    0xf02050c4              // + 4n: core n power switch
#define CPU_BOOT0   0xf0452000              // + 8n: core n boot address, high, low

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
        mov     x0, #0xe000
        movk    x0, #0x0640, lsl #16            // EL3_DATA: per-core data, all off
        mov     x1, #128
2:      str     xzr, [x0], #8
        subs    x1, x1, #8
        b.ne    2b
        dsb     sy
        ic      iallu
        dsb     sy
        isb
        mov     x0, #EL3_MON
        msr     vbar_el3, x0
        msr     spsel, #1
        mov     x0, #EL3_STACK
        mov     sp, x0                          // SP_EL3 for core 0
        isb

        // GIC-400 distributor, secure side (smm64 0x06402b10, core 0 only):
        // on, all interrupts group 1 (non-secure)
        mrs     x4, S3_1_C15_C3_0               // CBAR: peripheral base
        and     x4, x4, #0xffffffffffffff00
        add     x1, x4, #0x1000                 // GICD
        mov     w0, #3
        str     w0, [x1]                        // GICD_CTLR: group 0 and 1 on
        add     x1, x1, #0x80                   // GICD_IGROUPR0 (el3_core sets it)
        mov     w0, #-1
        add     x2, x4, #0x1000
        ldr     w3, [x2, #4]                    // GICD_TYPER
        ands    w3, w3, #0x1f                   // ITLinesNumber = IGROUPR words after the first
        b.eq    4f
3:      str     w0, [x1, #4]!
        subs    w3, w3, #1
        b.ne    3b
4:
        bl      el3_core

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

// el3_core: what every core needs before it runs at EL2 (smm64 0x064028b0,
// 0x06400c98, 0x06402b10, 0x0640251c, 0x064026f0, 0x064024a4). Uses x0-x4.
el3_core:
        // lower ELs may access CPUECTLR; SMP coherency on
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
        isb

        // counter frequency (only EL3 can set it) and no virtual offset
        mov     x1, #0x2020
        movk    x1, #0xf041, lsl #16
        ldr     w0, [x1]                        // 27,000,000
        msr     cntfrq_el0, x0
        msr     cntvoff_el2, xzr

        // this core's GIC registers (banked): SGIs/PPIs group 1, CPU interface on
        mrs     x4, S3_1_C15_C3_0               // CBAR
        and     x4, x4, #0xffffffffffffff00
        add     x1, x4, #0x1000
        mov     w0, #-1
        str     w0, [x1, #0x80]                 // GICD_IGROUPR0
        add     x1, x4, #0x2000                 // GICC
        mov     w0, #3
        str     w0, [x1]                        // GICC_CTLR
        mov     w0, #0x80
        str     w0, [x1, #4]                    // GICC_PMR

        // lower ELs: non-secure, HVC on, AArch64
        mov     x0, #0x531
        msr     scr_el3, x0
        isb

        // EL2 registers (smm64 calls it with HCR_EL2 = 0x80000000)
        mov     x0, #0x300000
        msr     cpacr_el1, x0
        mov     x0, #0x80000000
        msr     hcr_el2, x0
        mov     x0, #0x0830
        movk    x0, #0x30c5, lsl #16            // 0x30c50830
        msr     sctlr_el2, x0
        mrs     x0, cptr_el2
        mov     x1, #0x400
        movk    x1, #0x8010, lsl #16            // TCPAC (31), TTA (20), TFP (10)
        bic     x0, x0, x1
        msr     cptr_el2, x0
        msr     cptr_el3, xzr                   // no FP/SIMD traps to EL3
        msr     hstr_el2, xzr
        mov     x0, #0x1f
        msr     mdcr_el2, x0
        msr     mdscr_el1, xzr

        // timers
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
        ret

// el3_warm: cores 1-3 start here at EL3 after CPU_ON (their boot address).
el3_warm:
        msr     daifset, #0xf
        mrs     x0, sctlr_el3
        mov     x1, #0x1007                     // MMU, alignment check, caches off
        bic     x0, x0, x1
        msr     sctlr_el3, x0
        isb
        mov     x0, #EL3_MON
        msr     vbar_el3, x0
        mrs     x5, mpidr_el1
        and     x5, x5, #0xff                   // n
        msr     spsel, #1
        mov     x0, #EL3_STACK
        sub     x0, x0, x5, lsl #10
        mov     sp, x0                          // SP_EL3 for core n
        bl      el3_core
        mov     x6, #0xe000
        movk    x6, #0x0640, lsl #16            // EL3_DATA
        add     x6, x6, x5, lsl #5
        ldp     x1, x0, [x6]                    // entry, context id
        mov     x2, #2
        str     x2, [x6, #16]                   // state 2: running
        dsb     sy
        msr     elr_el3, x1
        mov     x1, #0x3c9                      // EL2h, interrupts masked
        msr     spsr_el3, x1
        mov     x1, #0
        mov     x2, #0
        mov     x3, #0
        mov     x5, #0
        mov     x6, #0
        eret

el3_sync_lower:
        stp     x4, x5, [sp, #-48]!
        stp     x6, x7, [sp, #16]
        stp     x8, x30, [sp, #32]
        mrs     x4, esr_el3
        lsr     x4, x4, #26
        cmp     x4, #0x17                       // SMC from AArch64
        b.ne    el3_bad
        mov     w4, #0x84000000
        cmp     w0, w4
        b.ne    1f
        mov     x0, #2                          // PSCI_VERSION: 0.2
        b       el3_ret
1:      add     w5, w4, #9
        cmp     w0, w5
        b.ne    2f
        mov     x1, #0x4304                     // SYSTEM_RESET: SUN_TOP_CTRL software reset
        movk    x1, #0xf040, lsl #16
        mov     w2, #1
        str     w2, [x1]
        str     w2, [x1, #4]
3:      wfi
        b       3b
2:      mov     w5, #3
        movk    w5, #0xc400, lsl #16            // CPU_ON (SMC64)
        cmp     w0, w5
        b.eq    psci_cpu_on
        add     w5, w5, #1                      // AFFINITY_INFO (SMC64)
        cmp     w0, w5
        b.eq    psci_affinity
        mov     w5, #0xff00
        movk    w5, #0x8600, lsl #16
        cmp     w0, w5
        b.ne    4f
        mov     x0, #0x3302                     // 'KB3', version 2 (CPU_ON)
        movk    x0, #0x4b42, lsl #16
        b       el3_ret
4:      mov     x0, #-1                         // NOT_SUPPORTED
el3_ret:
        ldp     x8, x30, [sp, #32]
        ldp     x6, x7, [sp, #16]
        ldp     x4, x5, [sp], #48
        eret

// core_state: x1 = MPIDR -> x4 = n (or -2 if not a core), x0 = PSCI state
// (0 on, 1 off, 2 on pending). Uses x5, x6.
core_state:
        mov     x4, #-2
        and     x0, x1, #0xff
        cmp     x0, #3
        b.hi    9f                              // aff0 > 3
        tst     x1, #0xffff00
        b.ne    9f                              // aff1/aff2 not 0
        mov     x4, x0
        mov     x0, #0                          // core 0 is on (it is calling)
        cbz     x4, 9f
        mov     x5, #0x508c
        movk    x5, #0xf020, lsl #16
        ldr     w5, [x5]
        lsr     w5, w5, w4
        mov     x0, #1                          // in reset: off
        tbnz    w5, #0, 9f
        mov     x5, #0xe000
        movk    x5, #0x0640, lsl #16            // EL3_DATA
        add     x5, x5, x4, lsl #5
        ldr     x5, [x5, #16]
        mov     x0, #0                          // out of reset and running: on
        cmp     x5, #2
        b.eq    9f
        mov     x0, #2                          // out of reset, not yet running: pending
9:      ret

psci_affinity:
        cbnz    x2, 1f                          // only level 0 (this core)
        bl      core_state
        tbz     x4, #63, el3_ret
1:      mov     x0, #-2                         // INVALID_PARAMETERS
        b       el3_ret

psci_cpu_on:
        bl      core_state
        tbnz    x4, #63, 1f
        cmp     x0, #0
        b.eq    2f
        cmp     x0, #2
        b.eq    3f

        // entry and context for el3_warm
        mov     x6, #0xe000
        movk    x6, #0x0640, lsl #16            // EL3_DATA
        add     x6, x6, x4, lsl #5
        stp     x2, x3, [x6]
        mov     x5, #1
        str     x5, [x6, #16]                   // state 1: starting
        dsb     sy

        // power up (smm64 0x06401ea0): only if the switch says off (bit 31)
        mov     x6, #0x50c4
        movk    x6, #0xf020, lsl #16
        add     x6, x6, x4, lsl #2              // CPU_PWR n
        ldr     w5, [x6]
        tbz     w5, #31, 5f
        mov     w7, #0x40
        bl      pwr_set
        mov     w7, #0x80
        bl      pwr_set
        mov     w7, #0x20
        bl      pwr_set
        mov     w7, #0x10
        bl      pwr_set
        mov     w7, #29                         // wait for bit 29
        bl      pwr_wait
        cbnz    x0, 4f
        mov     w7, #1
        bl      pwr_set
        mov     w7, #28                         // wait for bit 28
        bl      pwr_wait
        cbnz    x0, 4f
        ldr     w5, [x6]
        bic     w5, w5, #0x40
        str     w5, [x6]
        dsb     sy
        mov     w7, #2
        bl      pwr_set

        // boot address (smm64 0x06401ce8): high byte, then the low 32 bits
5:      mov     x6, #0x2000
        movk    x6, #0xf045, lsl #16
        add     x6, x6, x4, lsl #3              // CPU_BOOT n
        adr     x5, el3_warm
        lsr     x7, x5, #32
        and     w7, w7, #0xff
        str     w7, [x6]
        str     w5, [x6, #4]
        dsb     sy
        isb

        // out of reset (smm64 0x06401d9c): clear bit n
        mov     x6, #0x508c
        movk    x6, #0xf020, lsl #16
        ldr     w5, [x6]
        mov     w7, #1
        lsl     w7, w7, w4
        bic     w5, w5, w7
        str     w5, [x6]
        dsb     sy
        isb
        mov     x0, #0                          // SUCCESS
        b       el3_ret
1:      mov     x0, #-2                         // INVALID_PARAMETERS
        b       el3_ret
2:      mov     x0, #-4                         // ALREADY_ON
        b       el3_ret
3:      mov     x0, #-5                         // ON_PENDING
        b       el3_ret
4:      mov     x6, #0xe000
        movk    x6, #0x0640, lsl #16            // power-up timed out: still off
        add     x6, x6, x4, lsl #5
        str     xzr, [x6, #16]
        mov     x0, #-6                         // INTERNAL_FAILURE
        b       el3_ret

// pwr_set: [x6] |= w7. Uses w5.
pwr_set:
        ldr     w5, [x6]
        orr     w5, w5, w7
        str     w5, [x6]
        dsb     sy
        isb
        ret

// pwr_wait: wait up to 0.5 s for bit w7 of [x6] -> x0 = 0, or 1 on timeout. Uses x5, x8.
pwr_wait:
        mrs     x8, cntpct_el0
        mrs     x0, cntfrq_el0
        add     x8, x8, x0, lsr #1
1:      ldr     w5, [x6]
        lsr     w5, w5, w7
        mov     x0, #0
        tbnz    w5, #0, 2f
        mrs     x5, cntpct_el0
        mov     x0, #1
        cmp     x5, x8
        b.lo    1b
2:      ret

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
