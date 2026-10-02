// AArch64 probe (go -64, EL2, MMU off): the M2MC 2D blitter's first scaled
// copy, 16x8 -> 32x16 RGB565 (2x), point sampling, no striping
// (both widths are under 128), in free RAM (not the framebuffer).
//   source 0x02400000, pitch 32: pixel (x,y) = y << 8 | x
//   output 0x02500000, pitch 64: 64 KB pre-filled with 0xdeadbeef
// Reset and list start as in m2mc_fill_probe.s. Source feeder words from
// nexus.ko BGRC_PACKET_P_ProcessSwPaket (source feeder case); scaler and
// blit words from the RE of BGRC_PACKET_P_SetScaler / the scaled-blit case;
// rectangles: position y << 16 | x, size width << 16 | height.
// Prints the whole output (32x16 pixels) and the changed words after it.
// Ends in wfe; use --watchdog.
        .global _start
_start:
        mov     x0, #0x01080000
        mov     sp, x0
        msr     DAIFSet, #3
        mov     x20, #0xc000
        movk    x20, #0xf040, lsl #16
        adr     x0, vectors
        msr     VBAR_EL2, x0
        isb
        adr     x0, s_hdr;  bl puts

        // reset (BGRC_P_ResetDevice)
        ldr     x0, =0xf09b1808
        mov     w1, #1
        bl      wr
        ldr     x0, =0xf09b1808
        mov     w1, #0
        bl      wr
        ldr     x0, =0xf09b02f0
        mov     w1, #1
        bl      wr
        ldr     x26, =0xf09b001c
        bl      pollnz
        ldr     x0, =0xf09b1808
        mov     w1, #1
        bl      wr
        ldr     x0, =0xf09b1808
        mov     w1, #0
        bl      wr
        adr     x21, init
1:      ldp     w0, w1, [x21], #8
        cbz     w0, 2f
        bl      wr
        b       1b
2:
        // source: pixel (x,y) = y << 8 | x
        ldr     x0, =0x02400000
        mov     w2, #0
3:      mov     w1, #0
4:      orr     w3, w1, w2, lsl #8
        strh    w3, [x0], #2
        add     w1, w1, #1
        cmp     w1, #16
        b.lo    4b
        add     w2, w2, #1
        cmp     w2, #8
        b.lo    3b
        // output area: 64 KB of 0xdeadbeef
        ldr     x0, =0x02500000
        ldr     w1, =0xdeadbeef
        mov     x2, #0x4000
5:      str     w1, [x0], #4
        subs    x2, x2, #1
        b.ne    5b
        dsb     sy

        // start the list
        ldr     x0, =0xf09b0014
        adr     x1, packet
        bl      wr
        ldr     x0, =0xf09b000c
        mov     w1, #6
        bl      wr
        ldr     x26, =0xf09b0010
        mov     x27, #0xffffffff
        mov     x28, #2
        bl      poll
        adr     x0, s_after; bl puts
        bl      show
        bl      dump
        bl      count

        adr     x0, s_done; bl puts
9:      wfe
        b       9b

// dump: the output, one row of 32 pixels per line (as 16 words)
dump:   stp     x29, x30, [sp, #-16]!
        stp     x21, x22, [sp, #-16]!
        ldr     x21, =0x02500000
        mov     x22, #0
1:      tst     x22, #15
        b.ne    2f
        adr     x0, s_crlf; bl puts
        adr     x0, s_ind;  bl puts
2:      ldr     w0, [x21, x22, lsl #2]
        bl      puthex8
        add     x22, x22, #1
        cmp     x22, #256
        b.lo    1b
        adr     x0, s_crlf; bl puts
        ldp     x21, x22, [sp], #16
        ldp     x29, x30, [sp], #16
        ret

// count: changed words in the 64 KB area after the output
count:  stp     x29, x30, [sp, #-16]!
        ldr     x0, =0x02500000
        ldr     w1, =0xdeadbeef
        mov     x2, #256
        mov     x3, #0
1:      ldr     w4, [x0, x2, lsl #2]
        cmp     w4, w1
        cinc    x3, x3, ne
        add     x2, x2, #1
        cmp     x2, #0x4000
        b.lo    1b
        str     x3, [sp, #-16]!
        adr     x0, s_cnt;  bl puts
        ldr     x0, [sp];   bl puthex8
        add     sp, sp, #16
        adr     x0, s_crlf; bl puts
        ldp     x29, x30, [sp], #16
        ret

// pollnz: wait until *x26 != 0, at most 100 ms; prints the result and value
pollnz: stp     x29, x30, [sp, #-16]!
        mrs     x21, CNTPCT_EL0
        ldr     x0, =2700000
        add     x22, x21, x0
1:      ldr     w25, [x26]
        cbnz    w25, 2f
        mrs     x1, CNTPCT_EL0
        cmp     x1, x22
        b.lo    1b
        adr     x0, s_tmo;  bl puts
        b       3f
2:      adr     x0, s_ok;   bl puts
3:      mrs     x0, CNTPCT_EL0
        sub     x0, x0, x21
        mov     x1, #27
        udiv    x0, x0, x1
        bl      puthex8
        adr     x0, s_last; bl puts
        mov     x0, x25;    bl puthex8
        adr     x0, s_crlf; bl puts
        ldp     x29, x30, [sp], #16
        ret

show:   stp     x29, x30, [sp, #-16]!
        stp     x21, x22, [sp, #-16]!
        adr     x21, regs
1:      ldr     w0, [x21], #4
        cbz     w0, 2f
        bl      sread
        b       1b
2:      ldp     x21, x22, [sp], #16
        ldp     x29, x30, [sp], #16
        ret

// wr(x0 = address, w1 = value): 32-bit write, printed as "  W addr value"
wr:     stp     x29, x30, [sp, #-16]!
        stp     x0, x1, [sp, #-16]!
        adr     x0, s_w;    bl puts
        ldr     x0, [sp];   bl puthex8
        ldr     x0, [sp, #8]; bl puthex8
        adr     x0, s_crlf; bl puts
        ldp     x0, x1, [sp], #16
        str     w1, [x0]
        dsb     sy
        ldp     x29, x30, [sp], #16
        ret

// poll: wait until (*x26 & x27) == x28, at most 100 ms. Prints the result,
// the time in us and the last value read.
poll:   stp     x29, x30, [sp, #-16]!
        mrs     x21, CNTPCT_EL0
        ldr     x0, =2700000
        add     x22, x21, x0
1:      ldr     w25, [x26]
        and     x1, x25, x27
        cmp     x1, x28
        b.eq    2f
        mrs     x1, CNTPCT_EL0
        cmp     x1, x22
        b.lo    1b
        adr     x0, s_tmo;  bl puts
        b       3f
2:      adr     x0, s_ok;   bl puts
3:      mrs     x0, CNTPCT_EL0
        sub     x0, x0, x21
        mov     x1, #27
        udiv    x0, x0, x1
        bl      puthex8
        adr     x0, s_last; bl puts
        mov     x0, x25;    bl puthex8
        adr     x0, s_crlf; bl puts
        ldp     x29, x30, [sp], #16
        ret

// sread(x0 = address): one abort-safe 32-bit read, printed as
// "  addr value" or "  addr SYNC esr" / "SERR esr".
sread:  stp     x29, x30, [sp, #-16]!
        stp     x21, x22, [sp, #-16]!
        mov     x22, x0
        adr     x0, s_ind;  bl puts
        mov     x0, x22;    bl puthex8
        mov     x19, #0
        ldr     w23, [x22]
        dsb     sy
        isb
        msr     DAIFClr, #4
        isb
        nop
        msr     DAIFSet, #4
        cbz     x19, 2f
        cmp     x19, #1
        adr     x0, s_sync
        adr     x1, s_serr
        csel    x0, x0, x1, eq
        bl      puts
        mov     x0, x24
        bl      puthex8
        b       3f
2:      mov     x0, x23;    bl puthex8
3:      adr     x0, s_crlf; bl puts
        ldp     x21, x22, [sp], #16
        ldp     x29, x30, [sp], #16
        ret


        .balign 2048
vectors:
        .rept 4
        b       hang
        .balign 128
        .endr
        b       sync_h                         // 0x200 synchronous, current EL
        .balign 128
        b       hang
        .balign 128
        b       hang
        .balign 128
        b       serr_h                         // 0x380 SError, current EL
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
        .balign 4
init:
        .word 0xf09b02f0, 0x00000100
        .word 0xf09b0060, 0x82409024
        .word 0xf09b0064, 0x00000024
        .word 0xf09b006c, 0x00100000
        .word 0xf09b0068, 0x00000111
        .word 0, 0
regs:
        .word 0xf09b000c, 0xf09b0010, 0xf09b0014, 0xf09b0018, 0xf09b001c
        .word 0
        .balign 32
// M2MC hardware packet (BGRC_PACKET_P_WriteHwPkt layout), 492 bytes
packet:
        .word 0x00000001, 0x00007ff0       // next = 1 (last packet), group mask bits 14-4
        // source feeder: 0x02400000, pitch 32, RGB565
        .word 0x00000001, 0x00000000, 0x02400000, 0x00000000
        .word 0x00000000, 0x00000020, 0x00000000, 0x00000000
        .word 0x00000000, 0x00000000, 0x00000000, 0x00000565
        .word 0x000b0500, 0x00211001, 0x00000000, 0x00000000
        .word 0x00211e01, 0x00000000, 0x00000000
        // destination feeder (none)
        .word 0x00000000, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000000, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000000, 0x00001e01, 0x00000000, 0x00000000
        // output feeder: 0x02500000, pitch 64, RGB565
        .word 0x00000001, 0x00000000, 0x02500000, 0x00000040
        .word 0x00000000, 0x00000000, 0x00000000, 0x00000565
        .word 0x000b0500, 0x00001000
        // blit: scaler on (bit 31), last packet (bit 14); source 16x8 at (0,0), output 32x16 at (0,0)
        .word 0x80004000, 0x00000000, 0x00100008, 0x00000010
        .word 0x00000000, 0x00100008, 0x00000000, 0x00000000
        .word 0x00000000, 0x00200010, 0x00000010, 0x00000000
        .word 0x00200010, 0x00000010, 0x00000000, 0x00000000
        .word 0x00000000, 0x00000000, 0x00000000, 0x00020000
        // scaler: upscale; h/v step 0x80000/0x80000, phase (step - 1.0) / 2
        .word 0x00000014, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000000, 0x00fc0000, 0x00080000, 0xfffc0000
        .word 0x00080000, 0x00fc0000, 0x00080000, 0xfffc0000
        .word 0x00080000
        // blend: colour = src x 1, alpha = src alpha x 1
        .word 0x00000201, 0x00000205, 0x00000000, 0x000000fa
        // ROP 0xcc (copy)
        .word 0x000000cc, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000000
        // source colour key (off)
        .word 0x00000000, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000000
        // destination colour key (off)
        .word 0x00000000, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000000
        // filter coefficients: point sample
        .word 0x00000000, 0x00000400, 0x00000000, 0x00000400
        .word 0x00000000, 0x00000400, 0x00000000, 0x00000400
        .word 0x00000000, 0x00000400, 0x00000000, 0x00000400
        .word 0x00000000, 0x00000400, 0x00000000, 0x00000400
        // source colour matrix (off)
        .word 0x00000000, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000000, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000000, 0x00000000, 0x00000000, 0x00000000

s_hdr:    .asciz "\r\nm2mc scale probe\r\n"
s_after:  .asciz " after the list:\r\n"
s_cnt:    .asciz "  changed words in the 64 KB area after the output: "
s_ok:     .asciz "  poll ok after us: "
s_tmo:    .asciz "  poll TIMEOUT after us: "
s_last:   .asciz "last "
s_w:      .asciz "  W "
s_ind:    .asciz "  "
s_sync:   .asciz "SYNC "
s_serr:   .asciz "SERR "
s_hang:   .asciz "\r\nUNEXPECTED EXCEPTION esr/elr: "
s_crlf:   .asciz "\r\n"
s_done:   .asciz "probe done\r\n"
