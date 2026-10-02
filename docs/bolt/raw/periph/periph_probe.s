// AArch64 probe (go -64, EL2, MMU off): read one word from each address in a
// list and survive aborts. Prints per address:
//   "addr value"            the read worked
//   "addr SYNC esr"         synchronous external abort (handler skips the ldr)
//   "addr SERR esr"         asynchronous SError, taken when unmasked right after the read
// Addresses: the start of every block the stock/original DTB names, the
// registers BOLT's code uses in blocks the DTB doesn't name, and finally the
// GISB arbiter's error-capture registers (BOLT: "GISB Address/Data/Master").
// Read-only. Ends in wfe; use --watchdog as a backup.
        .global _start
_start:
        mov     x0, #0x01080000
        mov     sp, x0
        mov     x20, #0xc000
        movk    x20, #0xf040, lsl #16
        adr     x0, vectors
        msr     VBAR_EL2, x0
        isb
        adr     x0, s_hdr;  bl puts
        adr     x21, list
1:      ldr     x22, [x21], #8
        cbz     x22, 9f
        mov     x0, x22;    bl puthex8
        mov     x19, #0                        // fault flag: 0 none, 1 sync, 2 SError
        ldr     w23, [x22]                     // THE read (sync handler skips it)
        dsb     sy
        isb
        msr     DAIFClr, #4                    // unmask SError: a pending one is taken now
        isb
        nop
        msr     DAIFSet, #4
        cbz     x19, 2f
        cmp     x19, #1
        adr     x0, s_sync
        adr     x1, s_serr
        csel    x0, x0, x1, eq
        bl      puts
        mov     x0, x24                        // ESR saved by the handler
        bl      puthex8
        b       3f
2:      mov     x0, x23;    bl puthex8
3:      adr     x0, s_crlf; bl puts
        b       1b
9:      adr     x0, s_done; bl puts
8:      wfe
        b       8b

// ---- exceptions: sync at EL2 -> skip the instruction; SError -> just note it
        .balign 2048
vectors:
        .rept 4                                // current EL with SP_EL0 (unused)
        b       hang
        .balign 128
        .endr
        b       sync_h                         // 0x200 current EL, SP_ELx: synchronous
        .balign 128
        b       hang                           // 0x280 IRQ
        .balign 128
        b       hang                           // 0x300 FIQ
        .balign 128
        b       serr_h                         // 0x380 SError
        .balign 128
        .rept 8                                // lower ELs (unused)
        b       hang
        .balign 128
        .endr

sync_h: mrs     x24, ESR_EL2
        mov     x19, #1
        mrs     x25, ELR_EL2
        add     x25, x25, #4                   // skip the faulting ldr
        msr     ELR_EL2, x25
        mov     w23, #0
        eret
serr_h: mrs     x24, ESR_EL2
        mov     x19, #2
        eret
hang:   adr     x0, s_hang; bl puts
        mrs     x0, ESR_EL2; bl puthex8
        mrs     x0, ELR_EL2; bl puthex8
1:      wfe
        b       1b

puts:   ldrb    w1, [x0], #1
        cbz     w1, 9f
8:      ldr     w2, [x20, #0x14]
        tbz     w2, #5, 8b
        str     w1, [x20]
        b       puts
9:      ret
puthex8:                                       // w0 + space. Clobbers x0-x4.
        mov     w3, w0
        mov     w4, #28
1:      lsr     w1, w3, w4
        and     w1, w1, #0xf
        cmp     w1, #10
        add     w2, w1, #'0'
        add     w1, w1, #('a' - 10)
        csel    w1, w2, w1, lo
2:      ldr     w2, [x20, #0x14]
        tbz     w2, #5, 2b
        str     w1, [x20]
        subs    w4, w4, #4
        b.pl    1b
        mov     w1, #' '
3:      ldr     w2, [x20, #0x14]
        tbz     w2, #5, 3b
        str     w1, [x20]
        ret

        .balign 8
list:   // DTB blocks (start of each reg range, plus SDHCI version registers)
        .quad 0xf0200000, 0xf02000fc, 0xf0200100, 0xf0200200, 0xf02002fc, 0xf0200300
        .quad 0xf0200154, 0xf0200354, 0xf0200400
        .quad 0xf0201000, 0xf0201a00, 0xf0203000, 0xf0203a00, 0xf0203c00, 0xf0200920
        .quad 0xf0205000, 0xf032c800, 0xf0400000, 0xf0403000
        .quad 0xf0408000, 0xf0409000, 0xf040a600, 0xf040a640
        .quad 0xf0410640, 0xf0418000, 0xf0419000, 0xf0419c40
        .quad 0xf0452000, 0xf0460000, 0xf0480000, 0xf04a0000
        .quad 0xf04c4000, 0xf04d1100, 0xf04d1200, 0xf04e0000
        .quad 0xf0b00200, 0xf0b02000, 0xf0b10100, 0xf0b12000, 0xf0b10040
        .quad 0xf1100000, 0xf1101000, 0xf1102000, 0xf1108000, 0xf1109000, 0xf1110000, 0xf1120000
        .quad 0xf1201000, 0xffe00000
        // blocks only BOLT's code names (exact registers it uses)
        .quad 0xf0380088, 0xf0402800, 0xf0a000b8, 0xf0a0021c
        .quad 0xf0a68314, 0xf0a69000, 0xf0a6a008, 0xf0a70800
        .quad 0xf0ca0048, 0xf0cb0200
        // GISB arbiter error capture (BOLT's gisb code)
        .quad 0xf0400008, 0xf04007e4, 0xf04007ec, 0xf04007f0, 0xf04007f8
        .quad 0

s_hdr:  .asciz "\r\nperipheral probe (EL2, aborts caught)\r\n"
s_sync: .asciz "SYNC "
s_serr: .asciz "SERR "
s_hang: .asciz "\r\nUNEXPECTED EXCEPTION esr/elr: "
s_crlf: .asciz "\r\n"
s_done: .asciz "probe done\r\n"
