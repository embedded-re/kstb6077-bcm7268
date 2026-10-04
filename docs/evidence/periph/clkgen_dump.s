// AArch64 probe (go -64, EL2, MMU off): read-only dump of the clock generator
// 0xf04e0000-0xf04e7fff, one abort-safe 32-bit read per word.
// Output: one line per 32 bytes, "addr w0 .. w7"; a word that aborts prints as
// xxxxxxxx; lines where all 8 words read 0 are skipped. Ends with a count of
// aborted words. Ends in wfe; use --watchdog.
        .equ START, 0xf04e0000
        .equ END,   0xf04e8000
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
        ldr     x26, =START
        ldr     x27, =END
        mov     x28, #0                        // aborted words
        adr     x21, line                      // 8 values + 8 flags
row:    mov     x9, #0                         // word index
        mov     x10, #0                        // "anything non-zero or aborted"
1:      add     x22, x26, x9, lsl #2
        mov     x19, #0
        ldr     w23, [x22]
        dsb     sy
        isb
        msr     DAIFClr, #4
        isb
        nop
        msr     DAIFSet, #4
        str     w23, [x21, x9, lsl #2]
        add     x11, x21, #32
        strb    w19, [x11, x9]
        orr     x10, x10, x23
        orr     x10, x10, x19
        cbz     x19, 2f
        add     x28, x28, #1
2:      add     x9, x9, #1
        cmp     x9, #8
        b.lo    1b
        cbz     x10, 5f                        // all zero: skip the line
        mov     x0, x26;    bl puthex8
        mov     x9, #0
3:      add     x11, x21, #32
        ldrb    w12, [x11, x9]
        cbz     w12, 4f
        adr     x0, s_x;    bl puts
        b       6f
4:      ldr     w0, [x21, x9, lsl #2]
        bl      puthex8
6:      add     x9, x9, #1
        cmp     x9, #8
        b.lo    3b
        adr     x0, s_crlf; bl puts
5:      add     x26, x26, #32
        cmp     x26, x27
        b.lo    row
        adr     x0, s_cnt;  bl puts
        mov     x0, x28;    bl puthex8
        adr     x0, s_crlf; bl puts
        adr     x0, s_done; bl puts
9:      wfe
        b       9b

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
sync_h: mov     x19, #1
        mrs     x25, ELR_EL2
        add     x25, x25, #4
        msr     ELR_EL2, x25
        mov     w23, #0
        eret
serr_h: mov     x19, #2
        mov     w23, #0
        eret
hang:   adr     x0, s_hang; bl puts
        mrs     x0, ESR_EL2; bl puthex8
        mrs     x0, ELR_EL2; bl puthex8
1:      wfe
        b       1b

// puts / puthex8 do not touch x9-x12, x19-x28.
puts:   ldrb    w1, [x0], #1
        cbz     w1, 9f
8:      ldr     w2, [x20, #0x14]
        tbz     w2, #5, 8b
        str     w1, [x20]
        b       puts
9:      ret
puthex8:
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

        .ltorg
        .balign 8
line:   .space  48
s_hdr:  .asciz "\r\nclkgen dump 0xf04e0000-0xf04e7fff (zero lines skipped)\r\n"
s_x:    .asciz "xxxxxxxx "
s_cnt:  .asciz "aborted words: "
s_hang: .asciz "\r\nUNEXPECTED EXCEPTION esr/elr: "
s_crlf: .asciz "\r\n"
s_done: .asciz "dump done\r\n"
