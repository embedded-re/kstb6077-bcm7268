// AArch64 probe for EL3 and EL2 (MMU off): single words of secure DRAM
// (BL31 0x7df00000, SRR 0x7e000000), then the GISB error capture registers.
// Same binary at both levels:
//   EL3: boot -64 -el3 -raw -tftp -addr=0x03000000 <PC>:el3_sdram_probe.bin
//   EL2: boot -64 -raw -tftp -addr=0x03000000 <PC>:el3_sdram_probe.bin
// Prints CurrentEL, then "addr value" / "addr SYNC esr" / "addr SERR esr".
// The address is printed before the read, so a hang shows which word did it.
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
list:   // control: normal DRAM just below BL31 (free RAM)
        .quad 0x7dee0000,1
        // BL31 0x7df00000-0x7dffffff
        .quad 0x7df00000,4, 0x7df01000,1, 0x7df10000,1, 0x7df80000,1
        .quad 0x7dff0000,1, 0x7dfffffc,1
        // SRR 0x7e000000-0x7fffffff
        .quad 0x7e000000,4, 0x7e100000,1, 0x7f000000,1, 0x7ffffffc,1
        // GISB arbiter error capture, after the reads
        .quad 0xf0400008,1, 0xf04007e4,1, 0xf04007ec,1, 0xf04007f0,1, 0xf04007f8,1
        .quad 0, 0

s_hdr:  .asciz "\r\nEL3/EL2 secure DRAM probe (aborts caught)\r\nCurrentEL "
s_vbar: .asciz "VBAR_at_entry "
s_ctl:  .asciz "SCR_or_HCR_at_entry "
s_skip: .asciz "skip\r\n"
s_sync: .asciz "SYNC "
s_serr: .asciz "SERR "
s_hang: .asciz "\r\nUNEXPECTED EXCEPTION esr/elr: "
s_crlf: .asciz "\r\n"
s_done: .asciz "probe done\r\n"
