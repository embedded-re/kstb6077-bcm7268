// AArch64 probe (go -64, EL2, MMU off), address-map items 3.1 + 3.3:
// SDHCI 0/1 host + cfg blocks (DTB sdhci@f0200100/300), 0xf0200400, and the
// GENET 0 blocks (Linux bcmgenet v5 layout: SYS 0x0, INTRL2 0x200, RBUF
// 0x300, TBUF 0x600, UMAC 0x800, MDIO 0xe14). Read-only; the SDHCI buffer
// data ports (+0x20) are skipped (shown as 0). Reads go through read32
// (abort-safe, see ../periph/periph_probe.s). Ends in wfe.
        .global _start
_start:
        mov     x0, #0x01080000
        mov     sp, x0
        mov     x20, #0xc000
        movk    x20, #0xf040, lsl #16          // UART0
        adr     x0, vectors
        msr     VBAR_EL2, x0
        isb

        adr     x0, s_r0; bl puts
        mov     x28, #0x20
        movk    x28, #0xf020, lsl #16
        mov     x0, #0x0
        movk    x0, #0xf020, lsl #16
        mov     x1, #0x100
        bl      dump
        adr     x0, s_r1; bl puts
        mov     x28, #0x0
        movk    x28, #0x0, lsl #16
        mov     x0, #0x100
        movk    x0, #0xf020, lsl #16
        mov     x1, #0x100
        bl      dump
        adr     x0, s_r2; bl puts
        mov     x28, #0x220
        movk    x28, #0xf020, lsl #16
        mov     x0, #0x200
        movk    x0, #0xf020, lsl #16
        mov     x1, #0x100
        bl      dump
        adr     x0, s_r3; bl puts
        mov     x28, #0x0
        movk    x28, #0x0, lsl #16
        mov     x0, #0x300
        movk    x0, #0xf020, lsl #16
        mov     x1, #0x100
        bl      dump
        adr     x0, s_r4; bl puts
        mov     x28, #0x0
        movk    x28, #0x0, lsl #16
        mov     x0, #0x400
        movk    x0, #0xf020, lsl #16
        mov     x1, #0x40
        bl      dump
        adr     x0, s_r5; bl puts
        mov     x28, #0x0
        movk    x28, #0x0, lsl #16
        mov     x0, #0x0
        movk    x0, #0xf048, lsl #16
        mov     x1, #0x100
        bl      dump
        adr     x0, s_r6; bl puts
        mov     x28, #0x0
        movk    x28, #0x0, lsl #16
        mov     x0, #0x200
        movk    x0, #0xf048, lsl #16
        mov     x1, #0x80
        bl      dump
        adr     x0, s_r7; bl puts
        mov     x28, #0x0
        movk    x28, #0x0, lsl #16
        mov     x0, #0x300
        movk    x0, #0xf048, lsl #16
        mov     x1, #0x100
        bl      dump
        adr     x0, s_r8; bl puts
        mov     x28, #0x0
        movk    x28, #0x0, lsl #16
        mov     x0, #0x600
        movk    x0, #0xf048, lsl #16
        mov     x1, #0x40
        bl      dump
        adr     x0, s_r9; bl puts
        mov     x28, #0x0
        movk    x28, #0x0, lsl #16
        mov     x0, #0x800
        movk    x0, #0xf048, lsl #16
        mov     x1, #0x100
        bl      dump
        adr     x0, s_r10; bl puts
        mov     x28, #0x0
        movk    x28, #0x0, lsl #16
        mov     x0, #0xe00
        movk    x0, #0xf048, lsl #16
        mov     x1, #0x40
        bl      dump

        adr     x0, s_done; bl puts
8:      wfe
        b       8b

// dump: x0 = start, x1 = length (multiple of 32). 8 words per line, each
// read once; an aborted word prints as "--------"; all-zero lines print "*".
dump:   stp     x29, x30, [sp, #-16]!
        mov     x21, x0
        add     x22, x0, x1
        mov     x29, #0                        // 1 = last line was all-zero
        adr     x9, vals
        adr     x13, flags
1:      cmp     x21, x22
        b.hs    9f
        mov     x10, #0                        // OR of values and flags
        mov     x12, #0
2:      add     x0, x21, x12, lsl #2
        cmp     x0, x28
        b.ne    21f
        mov     w0, #0                         // skipped word: shown as 0
        mov     x19, #0
        b       22f
21:     bl      read32                         // keeps x9-x13
22:
        str     w0, [x9, x12, lsl #2]
        strb    w19, [x13, x12]
        orr     x10, x10, x0
        orr     x10, x10, x19
        add     x12, x12, #1
        cmp     x12, #8
        b.ne    2b
        cbnz    x10, 3f
        cbnz    x29, 7f
        mov     x29, #1
        adr     x0, s_star; bl puts
        b       7f
3:      mov     x29, #0
        mov     x0, x21;    bl puthex8
        mov     x12, #0
4:      ldrb    w0, [x13, x12]
        cbz     w0, 5f
        adr     x0, s_ab;   bl puts
        b       6f
5:      ldr     w0, [x9, x12, lsl #2]
        bl      puthex8
6:      add     x12, x12, #1
        cmp     x12, #8
        b.ne    4b
        adr     x0, s_crlf; bl puts
7:      add     x21, x21, #32
        b       1b
9:      ldp     x29, x30, [sp], #16
        ret

// read32: x0 = address -> w0 = value; x19 = 0 ok, 1 sync abort, 2 SError
read32: mov     x19, #0
        mov     w23, #0
        ldr     w23, [x0]                      // the sync handler skips this
        dsb     sy
        isb
        msr     DAIFClr, #4
        isb
        nop
        msr     DAIFSet, #4
        mov     w0, w23
        ret

        .balign 2048
vectors:
        .rept 4
        b       hang
        .balign 128
        .endr
        b       sync_h
        .balign 128
        b       hang
        .balign 128
        b       hang
        .balign 128
        b       serr_h
        .balign 128
        .rept 8
        b       hang
        .balign 128
        .endr

sync_h: mrs     x24, ESR_EL2
        mov     x19, #1
        mrs     x25, ELR_EL2
        add     x25, x25, #4
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

s_r0:  .asciz "SDHCI0 host 0xf0200000 (+0x20 not read):\r\n"
s_r1:  .asciz "SDHCI0 cfg 0xf0200100:\r\n"
s_r2:  .asciz "SDHCI1 host 0xf0200200 (eMMC; +0x20 not read):\r\n"
s_r3:  .asciz "SDHCI1 cfg 0xf0200300:\r\n"
s_r4:  .asciz "0xf0200400:\r\n"
s_r5:  .asciz "GENET SYS/GR_BRIDGE/EXT 0xf0480000:\r\n"
s_r6:  .asciz "GENET INTRL2 0xf0480200:\r\n"
s_r7:  .asciz "GENET RBUF 0xf0480300:\r\n"
s_r8:  .asciz "GENET TBUF 0xf0480600:\r\n"
s_r9:  .asciz "GENET UMAC 0xf0480800:\r\n"
s_r10:  .asciz "GENET UMAC 0xf0480e00 (MDIO +0x14):\r\n"
s_star: .asciz "*\r\n"
s_ab:   .asciz "-------- "
s_hang: .asciz "\r\nUNEXPECTED EXCEPTION esr/elr: "
s_crlf: .asciz "\r\n"
s_done: .asciz "probe done\r\n"
        .balign 8
vals:   .space 32
flags:  .space 8
