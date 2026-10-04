// AArch64 probe (go -64, EL2, MMU off): GFD0 source width 960, then the
// compositor CMP0 background colour 0xf0645810 (Nexus
// BVDC_Compositor_SetBackgroundColor -> Update_Canvas_Background ->
// BuildSyncSlipRul) set to 0x00515af0 (red if the bytes are Y Cb Cr).
// Saves and restores both registers. Ends in wfe; use --watchdog.
        .equ GFD, 0xf0641000
        .equ CMP, 0xf0645800
        .equ SEC, 27000000
        .global _start
_start:
        mov     x0, #0x01080000
        mov     sp, x0
        mov     x20, #0xc000
        movk    x20, #0xf040, lsl #16
        adr     x0, vectors
        msr     VBAR_EL2, x0
        isb
        ldr     x24, =GFD
        ldr     x23, =CMP
        adr     x0, s_hdr;  bl puts
        ldr     w27, [x24, #0x044]             // saved width
        ldr     w26, [x23, #0x010]             // saved background
        adr     x0, s_old;  bl puts
        bl      show
        mov     w0, #960
        str     w0, [x24, #0x044]
        adr     x0, s_w;    bl puts
        mov     x0, #1;     bl wait
        bl      show
        mov     x0, #4;     bl wait
        ldr     w0, =0x00515af0
        str     w0, [x23, #0x010]
        adr     x0, s_bg;   bl puts
        mov     x0, #1;     bl wait
        bl      show
        mov     x0, #4;     bl wait
        str     w26, [x23, #0x010]
        str     w27, [x24, #0x044]
        mov     x0, #1;     bl wait
        adr     x0, s_rest; bl puts
        bl      show
        adr     x0, s_done; bl puts
9:      wfe
        b       9b

show:   mov     x28, x30
        ldr     w0, [x24, #0x044]; bl puthex8
        ldr     w0, [x23, #0x010]; bl puthex8
        adr     x0, s_crlf; bl puts
        ret     x28
wait:   ldr     x1, =SEC                       // x0 seconds
        mul     x1, x1, x0
        mrs     x2, CNTPCT_EL0
        add     x2, x2, x1
1:      mrs     x3, CNTPCT_EL0
        cmp     x3, x2
        b.lo    1b
        ret

        .balign 2048
vectors:
        .rept 16
        b       hang
        .balign 128
        .endr
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
s_hdr:  .asciz "\r\nGFD width + CMP background probe. cols: width(f0641044) bg(f0645810)\r\n"
s_old:  .asciz "before:   "
s_w:    .asciz "WIDTH 960 (5 s), after 1s: "
s_bg:   .asciz "BACKGROUND 00515af0 (5 s), after 1s: "
s_rest: .asciz "restored: "
s_hang: .asciz "\r\nUNEXPECTED EXCEPTION esr/elr: "
s_crlf: .asciz "\r\n"
s_done: .asciz "probe done\r\n"
