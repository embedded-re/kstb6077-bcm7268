// AArch64 probe (go -64, EL2, MMU off): 2x horizontal zoom in the graphics
// feeder GFD0 (0xf0641000). Register roles from nexus.ko
// BVDC_P_GfxFeeder_BuildRul_isr:
//   +0x01c horizontal step: (step & 0x3ffff) << 3, 1.0 = 0x00100000
//   +0x170 scaler control:  bit 3 set by Nexus when the step is below 1.0
//   +0x044 source width (pixels)
// Saves the three registers, writes step 0.5 / bit 3 / width 960, reads them
// back after 1 s and 10 s (to see whether BOLT's display lists overwrite
// them), then restores the saved values in reverse order. Ends in wfe.
        .equ GFD, 0xf0641000
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
        adr     x0, s_hdr;  bl puts
        ldr     w25, [x24, #0x01c]             // saved step
        ldr     w26, [x24, #0x170]             // saved control
        ldr     w27, [x24, #0x044]             // saved width
        adr     x0, s_old;  bl puts
        bl      show
        // zoom: Nexus order is step, (vertical regs), control, then width
        mov     w0, #0x00080000
        str     w0, [x24, #0x01c]
        orr     w0, w26, #8
        str     w0, [x24, #0x170]
        mov     w0, #960
        str     w0, [x24, #0x044]
        adr     x0, s_zoom; bl puts
        mov     x0, #1;     bl wait
        adr     x0, s_1s;   bl puts
        bl      show
        mov     x0, #9;     bl wait
        adr     x0, s_10s;  bl puts
        bl      show
        // restore in reverse order
        str     w27, [x24, #0x044]
        str     w26, [x24, #0x170]
        str     w25, [x24, #0x01c]
        mov     x0, #1;     bl wait
        adr     x0, s_rest; bl puts
        bl      show
        adr     x0, s_done; bl puts
9:      wfe
        b       9b

show:   mov     x28, x30
        ldr     w0, [x24, #0x01c]; bl puthex8
        ldr     w0, [x24, #0x170]; bl puthex8
        ldr     w0, [x24, #0x044]; bl puthex8
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
s_hdr:  .asciz "\r\nGFD 2x horizontal zoom probe. cols: step(+01c) ctrl(+170) width(+044)\r\n"
s_old:  .asciz "before:   "
s_zoom: .asciz "ZOOM ON for 10 s\r\n"
s_1s:   .asciz "after 1s: "
s_10s:  .asciz "after 10s:"
s_rest: .asciz "restored: "
s_hang: .asciz "\r\nUNEXPECTED EXCEPTION esr/elr: "
s_crlf: .asciz "\r\n"
s_done: .asciz "probe done\r\n"
