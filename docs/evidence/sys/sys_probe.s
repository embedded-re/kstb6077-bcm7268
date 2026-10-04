// AArch64 probe (go -64, EL2, MMU off): reset history, straps, OTP and
// SUN_TOP general control registers. Read-only; aborts caught (see
// ../periph/periph_probe.s for how). Ends in wfe; use --watchdog.
//   0xf041006c          AON reset history (BOLT 0x07012110 reads it, then clears it)
//   0xf040401c / 20     straps (banner "strap=%08x,%08x")
//   0xf0404030/34, 520  OTP (BOLT fuse-name table 0x070472a8)
//   0xf0404084, 0a4     DTB general-ctrl-1, general-ctrl-no-scan-0
//   0xf04e6134          bond option (banner, low byte)
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
list:   .quad 0xf041006c
        .quad 0xf040401c, 0xf0404020
        .quad 0xf0404030, 0xf0404034, 0xf0404520
        .quad 0xf0404084, 0xf04040a4
        .quad 0xf04e6134
        .quad 0

s_hdr:  .asciz "\r\nsys probe (EL2, aborts caught)\r\n"
s_sync: .asciz "SYNC "
s_serr: .asciz "SERR "
s_hang: .asciz "\r\nUNEXPECTED EXCEPTION esr/elr: "
s_crlf: .asciz "\r\n"
s_done: .asciz "probe done\r\n"
