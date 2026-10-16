// AArch64 probe for EL3 and EL2 (MMU off): the same binary runs at both levels
// so the two outputs diff line by line.
//   EL3: boot -64 -el3 -raw -tftp -addr=0x03000000 <PC>:el3_read_probe.bin
//   EL2: boot -64 -raw -tftp -addr=0x03000000 <PC>:el3_read_probe.bin
// Prints CurrentEL, then
//   system registers:  "NAME value"     (64 bits)
//                      "NAME SYNC esr"  (the mrs trapped: UNDEFINED at this EL)
//                      "NAME skip"      (not tried at EL2: may trap to EL3)
//   memory words:      "addr value" / "addr SYNC esr" / "addr SERR esr"
// Exceptions: own vectors (VBAR_EL3 or VBAR_EL2); SError and external aborts
// routed to the current EL (SCR_EL3.EA at EL3, HCR_EL2.AMO at EL2).
// Reads only. Ends in wfe; arm the watchdog first.
        .global _start
        .equ    LINK, 0x03000000
_start:
        msr     DAIFSet, #0xf
        mov     x0, #LINK
        sub     sp, x0, #0x1000                // stack below the program
        mov     x20, #0xc000
        movk    x20, #0xf040, lsl #16          // UART0
        mrs     x27, CurrentEL
        ubfx    x27, x27, #2, #2               // x27 = EL
        cmp     x27, #3
        b.ne    1f
        mrs     x26, VBAR_EL3                  // entry values, printed later
        mrs     x25, SCR_EL3
        adr     x0, vectors3
        msr     VBAR_EL3, x0
        orr     x0, x25, #8                    // SCR_EL3.EA
        msr     SCR_EL3, x0
        b       2f
1:      mrs     x26, VBAR_EL2
        mrs     x25, HCR_EL2
        adr     x0, vectors2
        msr     VBAR_EL2, x0
        orr     x0, x25, #(1 << 5)             // HCR_EL2.AMO
        msr     HCR_EL2, x0
2:      isb
        adr     x0, s_hdr;  bl puts
        mov     x0, x27;    bl puthex8
        adr     x0, s_crlf; bl puts
        adr     x0, s_vbar; bl puts
        mov     x0, x26;    bl puthex16
        adr     x0, s_crlf; bl puts
        adr     x0, s_ctl;  bl puts
        mov     x0, x25;    bl puthex16
        adr     x0, s_crlf; bl puts

// ---- system registers
        .macro  SREG name, reg, el3only=0
        adr     x0, 91f
        bl      puts
        .if     \el3only
        cmp     x27, #3
        b.ne    93f
        .endif
        mov     x19, #0
        mrs     x23, \reg                      // sync handler skips it
        bl      report64
        b       92f
        .if     \el3only
93:     adr     x0, s_skip
        bl      puts
        b       92f
        .endif
91:     .asciz  "\name "
        .balign 4
92:
        .endm

        SREG    MIDR_EL1, MIDR_EL1
        SREG    MPIDR_EL1, MPIDR_EL1
        SREG    REVIDR_EL1, REVIDR_EL1
        SREG    ID_AA64PFR0_EL1, ID_AA64PFR0_EL1
        SREG    ID_AA64DFR0_EL1, ID_AA64DFR0_EL1
        SREG    ID_AA64ISAR0_EL1, ID_AA64ISAR0_EL1
        SREG    ID_AA64MMFR0_EL1, ID_AA64MMFR0_EL1
        SREG    CNTFRQ_EL0, CNTFRQ_EL0
        // EL3 registers (UNDEFINED below EL3)
        SREG    SCTLR_EL3, SCTLR_EL3
        SREG    ACTLR_EL3, ACTLR_EL3
        SREG    MDCR_EL3, MDCR_EL3
        SREG    CPTR_EL3, CPTR_EL3
        SREG    RVBAR_EL3, RVBAR_EL3
        SREG    TCR_EL3, TCR_EL3
        SREG    TTBR0_EL3, TTBR0_EL3
        SREG    MAIR_EL3, MAIR_EL3
        SREG    ELR_EL3, ELR_EL3
        SREG    SPSR_EL3, SPSR_EL3
        SREG    CNTPS_CTL_EL1, CNTPS_CTL_EL1
        SREG    CNTPS_CVAL_EL1, CNTPS_CVAL_EL1
        // EL2 registers (readable at EL2 and EL3)
        SREG    SCTLR_EL2, SCTLR_EL2
        SREG    ACTLR_EL2, ACTLR_EL2
        SREG    CPTR_EL2, CPTR_EL2
        SREG    MDCR_EL2, MDCR_EL2
        SREG    CNTHCTL_EL2, CNTHCTL_EL2
        SREG    ACTLR_EL1, ACTLR_EL1
        // Cortex-A53 implementation-defined registers. Below EL3 they are
        // gated by ACTLR_EL3/ACTLR_EL2 and may trap to EL3 (smm64), so EL2
        // only reads CPUECTLR (smm64 sets ACTLR_EL3 bit 1).
        SREG    CPUECTLR_EL1, S3_1_C15_C2_1
        SREG    CPUACTLR_EL1, S3_1_C15_C2_0, 1
        SREG    CPUMERRSR_EL1, S3_1_C15_C2_2, 1
        SREG    L2MERRSR_EL1, S3_1_C15_C2_3, 1
        SREG    L2CTLR_EL1, S3_1_C11_C0_2, 1
        SREG    L2ECTLR_EL1, S3_1_C11_C0_3, 1
        SREG    L2ACTLR_EL1, S3_1_C15_C0_0, 1
        SREG    CBAR_EL1, S3_1_C15_C3_0, 1

// ---- memory: list of (address, number of words)
        adr     x21, list
1:      ldp     x22, x24, [x21], #16
        cbz     x22, 9f
3:      mov     x0, x22;    bl puthex8
        mov     x19, #0                        // fault flag: 0 none, 1 sync, 2 SError
        ldr     w23, [x22]                     // THE read (sync handler skips it)
        dsb     sy
        isb
        msr     DAIFClr, #4                    // unmask SError: a pending one is taken now
        isb
        nop
        msr     DAIFSet, #4
        bl      report32
        add     x22, x22, #4
        subs    x24, x24, #1
        b.hi    3b
        b       1b
9:      adr     x0, s_done; bl puts
8:      wfe
        b       8b

// report32/report64: value in x23 (or the fault noted by the handler), CRLF
report32:
        mov     x9, #0
        b       4f
report64:
        mov     x9, #1
4:      stp     x29, x30, [sp, #-16]!
        cbz     x19, 2f
        cmp     x19, #1
        adr     x0, s_sync
        adr     x1, s_serr
        csel    x0, x0, x1, eq
        bl      puts
        mov     x0, x28                        // ESR saved by the handler
        bl      puthex8
        b       3f
2:      mov     x0, x23
        cbz     x9, 5f
        bl      puthex16
        b       3f
5:      bl      puthex8
3:      adr     x0, s_crlf; bl puts
        ldp     x29, x30, [sp], #16
        ret

// ---- exceptions: sync -> skip the instruction; SError -> just note it
        .macro  VECTORS el
        .balign 2048
vectors\el:
        .rept 4                                // current EL with SP_EL0 (unused)
        b       hang\el
        .balign 128
        .endr
        b       sync\el                        // 0x200 current EL, SP_ELx: synchronous
        .balign 128
        b       hang\el                        // 0x280 IRQ
        .balign 128
        b       hang\el                        // 0x300 FIQ
        .balign 128
        b       serr\el                        // 0x380 SError
        .balign 128
        .rept 8                                // lower ELs (unused)
        b       hang\el
        .balign 128
        .endr
sync\el:
        mrs     x28, ESR_EL\el
        mov     x19, #1
        mrs     x10, ELR_EL\el
        add     x10, x10, #4                   // skip the faulting instruction
        msr     ELR_EL\el, x10
        mov     x23, #0
        eret
serr\el:
        mrs     x28, ESR_EL\el
        mov     x19, #2
        eret
hang\el:
        adr     x0, s_hang; bl puts
        mrs     x0, ESR_EL\el; bl puthex8
        mrs     x0, ELR_EL\el; bl puthex16
1:      wfe
        b       1b
        .endm
        VECTORS 3
        VECTORS 2

puts:   ldrb    w1, [x0], #1                   // clobbers x0-x2
        cbz     w1, 9f
8:      ldr     w2, [x20, #0x14]
        tbz     w2, #5, 8b
        str     w1, [x20]
        b       puts
9:      ret
puthex16:                                      // x0 (16 digits) + space. Clobbers x0-x4.
        mov     x3, x0
        mov     x4, #60
        b       1f
puthex8:                                       // w0 (8 digits) + space. Clobbers x0-x4.
        mov     w3, w0
        mov     x4, #28
1:      lsr     x1, x3, x4
        and     x1, x1, #0xf
        cmp     x1, #10
        add     x2, x1, #'0'
        add     x1, x1, #('a' - 10)
        csel    x1, x2, x1, lo
2:      ldr     w2, [x20, #0x14]
        tbz     w2, #5, 2b
        str     w1, [x20]
        subs    x4, x4, #4
        b.pl    1b
        mov     w1, #' '
3:      ldr     w2, [x20, #0x14]
        tbz     w2, #5, 3b
        str     w1, [x20]
        ret

        .balign 8
list:   // periph_probe.s list (single words)
        .quad 0xf0200000,1, 0xf02000fc,1, 0xf0200100,1, 0xf0200200,1, 0xf02002fc,1, 0xf0200300,1
        .quad 0xf0200154,1, 0xf0200354,1, 0xf0200400,1
        .quad 0xf0201000,1, 0xf0201a00,1, 0xf0203000,1, 0xf0203a00,1, 0xf0203c00,1, 0xf0200920,1
        .quad 0xf0205000,1, 0xf032c800,1, 0xf0400000,1, 0xf0403000,1
        .quad 0xf0408000,1, 0xf0409000,1, 0xf040a600,1, 0xf040a640,1
        .quad 0xf0410640,1, 0xf0418000,1, 0xf0419000,1, 0xf0419c40,1
        .quad 0xf0452000,1, 0xf0460000,1, 0xf0480000,1, 0xf04a0000,1
        .quad 0xf04c4000,1, 0xf04d1100,1, 0xf04d1200,1, 0xf04e0000,1
        .quad 0xf0b00200,1, 0xf0b02000,1, 0xf0b10100,1, 0xf0b12000,1, 0xf0b10040,1
        .quad 0xf1100000,1, 0xf1101000,1, 0xf1102000,1, 0xf1108000,1, 0xf1109000,1, 0xf1110000,1, 0xf1120000,1
        .quad 0xf1201000,1, 0xffe00000,1
        .quad 0xf0380088,1, 0xf0402800,1, 0xf0a000b8,1, 0xf0a0021c,1
        .quad 0xf0a68314,1, 0xf0a69000,1, 0xf0a6a008,1, 0xf0a70800,1
        .quad 0xf0ca0048,1, 0xf0cb0200,1
        .quad 0xf0400008,1, 0xf04007e4,1, 0xf04007ec,1, 0xf04007f0,1, 0xf04007f8,1
        // EL2 abort holes: AON, GFD, USB control block
        .quad 0xf041002c,1, 0xf041006c,1, 0xf064105c,1
        .quad 0xf0b00280,4, 0xf0b0029c,1, 0xf0b002b8,14
        // HDMI TX holes (first word of each range; 0xf06fa000 reads at EL2)
        .quad 0xf06fa000,1, 0xf06fa27c,1, 0xf06fa300,1, 0xf06fa6f4,1, 0xf06fa700,1
        .quad 0xf06fa878,1, 0xf06fa8bc,1, 0xf06fa930,1, 0xf06fa9b0,1, 0xf06faa30,1
        .quad 0xf06faab0,1, 0xf06fae00,1
        // clock generator: first word of each aborting run
        .quad 0xf04e0528,1, 0xf04e101c,1, 0xf04e283c,1, 0xf04e28bc,1
        .quad 0xf04e3014,1, 0xf04e4848,1, 0xf04e6010,1, 0xf04e6160,1
        // OTP
        .quad 0xf0404030,2, 0xf0404520,1
        // GIC-400: distributor (CTLR, TYPER, IIDR, IGROUPR0-7, ISENABLER0,
        // IPRIORITYR0-7, ITARGETSR0-7, ICFGR0-1, NSACR0-1), CPU interface
        // (CTLR, PMR, BPR, HPPIR, ABPR, AHPPIR, APR0, NSAPR0, IIDR), GICH
        .quad 0xffd01000,3, 0xffd01080,8, 0xffd01100,1, 0xffd01400,8, 0xffd01800,8
        .quad 0xffd01c00,2, 0xffd01e00,2
        .quad 0xffd02000,3, 0xffd02018,2, 0xffd02028,1, 0xffd020d0,1, 0xffd020e0,1, 0xffd020fc,1
        .quad 0xffd04000,2
        .quad 0, 0

s_hdr:  .asciz "\r\nEL3/EL2 read probe (aborts caught)\r\nCurrentEL "
s_vbar: .asciz "VBAR_at_entry "
s_ctl:  .asciz "SCR_or_HCR_at_entry "
s_skip: .asciz "skip\r\n"
s_sync: .asciz "SYNC "
s_serr: .asciz "SERR "
s_hang: .asciz "\r\nUNEXPECTED EXCEPTION esr/elr: "
s_crlf: .asciz "\r\n"
s_done: .asciz "probe done\r\n"
