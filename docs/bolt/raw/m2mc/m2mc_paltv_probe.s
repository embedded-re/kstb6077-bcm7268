// AArch64 probe (go -64, EL2, MMU off): the M2MC 2D blitter turns a 320x200
// palette-8 image into RGB565 through a 256-entry palette and scales it 5x to
// 1600x1000 (filter off, hardware striping on), first into scratch RAM, then
// onto the TV. Same flow and scaler words as m2mc_tv_probe.s.
//
//   source  0x02600000, pitch 320, 8-bit index: white border = 255; inside,
//           index = (x >> 4) | (y >> 5) << 4 | checker8 << 7   (max 239)
//   palette 0x02610000, 256 x AARRGGBB: R = (i & 15) * 17,
//           G = ((i >> 4) & 7) * 36, B = (i >> 7) * 255, A = ff; entry 255 white
//   reference 0x02400000, pitch 640: RGB565 = palette[index], made by the CPU;
//           the scratch check compares against it
//   scratch 0x03000000: framebuffer-sized, pre-filled with 0xdeadbeef
//   screen  framebuffer 0x7db0b700; output at (160,40) = 0x7db31040
// Source format = BM2MC format 33 (palette-8): source feeder words 11-13 =
// 00030008, 0, 00211c01; palette = group bit 1 (mask 7ff2), words
// (02610000, 0) after the colour matrix group (m2mc_pal4_probe.s).
// 1. M2MC reset, scale into scratch, check: every output pixel = reference
//    pixel (x/5, y/5), and exactly 1,600,000 halfwords of the area changed.
// 2. Only if both pass: CPU clears the framebuffer to black, M2MC reset,
//    timed run onto the screen: CNTPCT_EL0 just before the +0x0c = 6 write and
//    after +0x18 = packet, +0x10 = 2, +0x1c = 0, no UART output in between.
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

        // palette: entry i
        ldr     x0, =0x02610000
        mov     w1, #0
1:      and     w2, w1, #15
        mov     w3, #17
        mul     w2, w2, w3                     // R
        ubfx    w4, w1, #4, #3
        mov     w3, #36
        mul     w4, w4, w3                     // G
        lsr     w5, w1, #7
        neg     w5, w5
        and     w5, w5, #0xff                  // B
        lsl     w2, w2, #16
        orr     w2, w2, w4, lsl #8
        orr     w2, w2, w5
        orr     w2, w2, #0xff000000
        cmp     w1, #255
        b.ne    2f
        mov     w2, #-1                        // entry 255: white
2:      str     w2, [x0], #4
        add     w1, w1, #1
        cmp     w1, #256
        b.lo    1b
        // index image and the RGB565 reference
        ldr     x0, =0x02600000
        ldr     x6, =0x02400000
        ldr     x7, =0x02610000
        mov     w2, #0                         // y
1:      mov     w1, #0                         // x
2:      mov     w3, #255
        cmp     w1, #0
        b.eq    3f
        cmp     w1, #319
        b.eq    3f
        cmp     w2, #0
        b.eq    3f
        cmp     w2, #199
        b.eq    3f
        lsr     w3, w1, #4
        lsr     w4, w2, #5
        orr     w3, w3, w4, lsl #4
        eor     w4, w1, w2
        ubfx    w4, w4, #3, #1
        orr     w3, w3, w4, lsl #7
3:      strb    w3, [x0], #1
        ldr     w4, [x7, x3, lsl #2]           // AARRGGBB -> RGB565
        ubfx    w5, w4, #19, #5
        lsl     w5, w5, #11
        ubfx    w8, w4, #10, #6
        orr     w5, w5, w8, lsl #5
        ubfx    w8, w4, #3, #5
        orr     w5, w5, w8
        strh    w5, [x6], #2
        add     w1, w1, #1
        cmp     w1, #320
        b.lo    2b
        add     w2, w2, #1
        cmp     w2, #200
        b.lo    1b
        // scratch area: 0xdeadbeef
        ldr     x0, =0x03000000
        ldr     w1, =0xdeadbeef
        ldr     x2, =0x3f4800
        bl      fill32
        dsb     sy

        // 1. into scratch RAM
        bl      m2mc_reset
        adr     x24, pk_scr
        bl      run
        bl      check
        cbnz    x0, 8f

        // 2. onto the screen: clear to black, then scale
        adr     x0, s_clear; bl puts
        ldr     x0, =0x7db0b700
        mov     w1, #0
        ldr     x2, =0x3f4800
        bl      fill32
        dsb     sy
        bl      m2mc_reset
        adr     x24, pk_fb
        bl      trun
        adr     x0, s_shown; bl puts
        b       9f
8:      adr     x0, s_skip; bl puts
9:      adr     x0, s_done; bl puts
10:     wfe
        b       10b

// fill32(x0 = address, w1 = word, x2 = bytes)
fill32: str     w1, [x0], #4
        subs    x2, x2, #4
        b.ne    fill32
        ret

// m2mc_reset: BGRC_P_ResetDevice
m2mc_reset:
        stp     x29, x30, [sp, #-16]!
        stp     x21, x22, [sp, #-16]!
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
2:      ldp     x21, x22, [sp], #16
        ldp     x29, x30, [sp], #16
        ret

// run(x24 = packet): start a one-packet list, wait until done, time it
run:    stp     x29, x30, [sp, #-16]!
        ldr     x0, =0xf09b0014
        mov     w1, w24
        bl      wr
        ldr     x0, =0xf09b000c
        mov     w1, #6
        bl      wr
        ldr     x26, =0xf09b0018               // CURR_PKT_ADDR = this packet
        mov     x27, #0xffffffff
        mov     x28, x24
        bl      poll
        ldr     x26, =0xf09b0010               // LIST_STATUS = 2
        mov     x28, #2
        bl      poll
        ldr     x26, =0xf09b001c               // BLIT_STATUS = 0
        mov     x28, #0
        bl      poll
        bl      show
        ldp     x29, x30, [sp], #16
        ret

// trun(x24 = packet): start a one-packet list with no printing until it is
// done (or 100 ms passed), then print the time in counter ticks (27 MHz) and
// the registers
trun:   stp     x29, x30, [sp, #-16]!
        ldr     x0, =0xf09b0014
        str     w24, [x0]
        dsb     sy
        ldr     x1, =0xf09b000c
        mov     w2, #6
        ldr     x9, =0xf09b0000
        ldr     x3, =2700000
        isb
        mrs     x10, CNTPCT_EL0
        str     w2, [x1]
        add     x3, x3, x10
1:      mrs     x11, CNTPCT_EL0
        cmp     x11, x3
        b.hs    2f
        ldr     w4, [x9, #0x18]
        cmp     w4, w24
        b.ne    1b
        ldr     w4, [x9, #0x10]
        cmp     w4, #2
        b.ne    1b
        ldr     w4, [x9, #0x1c]
        cbnz    w4, 1b
        mrs     x11, CNTPCT_EL0
        adr     x0, s_tok;  bl puts
        b       3f
2:      adr     x0, s_ttmo; bl puts
3:      sub     x0, x11, x10
        bl      puthex8
        adr     x0, s_crlf; bl puts
        bl      show
        ldp     x29, x30, [sp], #16
        ret

// check: x0 = 0 if the scratch output is exact, else non-zero.
// Prints mismatching pixels inside the rectangle and changed halfwords in
// the whole area (expected 0 and 1,600,000 = 0x186a00).
check:  stp     x29, x30, [sp, #-16]!
        ldr     x10, =0x03025940           // output top-left
        ldr     x11, =0x02400000
        mov     x12, #0                        // mismatches
        mov     x13, #5
        mov     x2, #0                         // y
1:      mov     x1, #0                         // x
        udiv    x5, x2, x13                    // source row
        mov     x6, #640
        mul     x5, x5, x6
        add     x5, x5, x11
        mov     x6, #3840
        mul     x7, x2, x6
        add     x7, x7, x10                    // output row
2:      udiv    x3, x1, x13
        ldrh    w3, [x5, x3, lsl #1]
        ldrh    w4, [x7, x1, lsl #1]
        cmp     w3, w4
        cinc    x12, x12, ne
        add     x1, x1, #1
        cmp     x1, #1600
        b.lo    2b
        add     x2, x2, #1
        cmp     x2, #1000
        b.lo    1b
        // changed halfwords in the whole area
        ldr     x0, =0x03000000
        ldr     x2, =0x1fa400                  // halfwords
        mov     w8, #0xbeef
        mov     w9, #0xdead
        mov     x14, #0
        mov     x1, #0
3:      ldrh    w3, [x0, x1, lsl #1]
        tst     x1, #1
        csel    w4, w8, w9, eq
        cmp     w3, w4
        cinc    x14, x14, ne
        add     x1, x1, #1
        cmp     x1, x2
        b.lo    3b
        stp     x12, x14, [sp, #-16]!
        adr     x0, s_chk;  bl puts
        ldr     x0, [sp];   bl puthex8
        ldr     x0, [sp, #8]; bl puthex8
        adr     x0, s_crlf; bl puts
        ldp     x12, x14, [sp], #16
        ldr     x0, =0x186a00
        eor     x0, x14, x0
        orr     x0, x0, x12
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
// scale into scratch: output 0x03025940
pk_scr:
        .word 0x00000001, 0x00007ff2, 0x00000001, 0x00000000
        .word 0x02600000, 0x00000000, 0x00000000, 0x00000140
        .word 0x00000000, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000000, 0x00030008, 0x00000000, 0x00211c01
        .word 0x00000000, 0x00000000, 0x00211e01, 0x00000000
        .word 0x00000000, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000000, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000000, 0x00000000, 0x00001e01, 0x00000000
        .word 0x00000000, 0x00000001, 0x00000000, 0x03025940
        .word 0x00000f00, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000565, 0x000b0500, 0x00001000, 0x80004000
        .word 0x00000000, 0x014000c8, 0x000000d0, 0x00000000
        .word 0x014000c8, 0x00000000, 0x00000000, 0x00000000
        .word 0x064003e8, 0x000003f0, 0x00000000, 0x064003e8
        .word 0x000003f0, 0x0077fff8, 0x0077fff8, 0x00000258
        .word 0x00000004, 0x00000004, 0x00030000, 0x04000014
        .word 0x00000000, 0x00000000, 0x00000000, 0x00000000
        .word 0x00f99999, 0x00033333, 0xfff99999, 0x00033333
        .word 0x00f99999, 0x00033333, 0xfff99999, 0x00033333
        .word 0x00000201, 0x00000205, 0x00000000, 0x000000fa
        .word 0x000000cc, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000000, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000000, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000000, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000400, 0x00000000, 0x00000200, 0x00000000
        .word 0x00000400, 0x00000000, 0x00000200, 0x00000000
        .word 0x00000400, 0x00000000, 0x00000200, 0x00000000
        .word 0x00000400, 0x00000000, 0x00000200, 0x00000000
        .word 0x00000000, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000000, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000000, 0x00000000, 0x00000000, 0x02610000
        .word 0x00000000
        .balign 32
// scale onto the screen: output 0x7db31040
pk_fb:
        .word 0x00000001, 0x00007ff2, 0x00000001, 0x00000000
        .word 0x02600000, 0x00000000, 0x00000000, 0x00000140
        .word 0x00000000, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000000, 0x00030008, 0x00000000, 0x00211c01
        .word 0x00000000, 0x00000000, 0x00211e01, 0x00000000
        .word 0x00000000, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000000, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000000, 0x00000000, 0x00001e01, 0x00000000
        .word 0x00000000, 0x00000001, 0x00000000, 0x7db31040
        .word 0x00000f00, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000565, 0x000b0500, 0x00001000, 0x80004000
        .word 0x00000000, 0x014000c8, 0x000000d0, 0x00000000
        .word 0x014000c8, 0x00000000, 0x00000000, 0x00000000
        .word 0x064003e8, 0x000003f0, 0x00000000, 0x064003e8
        .word 0x000003f0, 0x0077fff8, 0x0077fff8, 0x00000258
        .word 0x00000004, 0x00000004, 0x00030000, 0x04000014
        .word 0x00000000, 0x00000000, 0x00000000, 0x00000000
        .word 0x00f99999, 0x00033333, 0xfff99999, 0x00033333
        .word 0x00f99999, 0x00033333, 0xfff99999, 0x00033333
        .word 0x00000201, 0x00000205, 0x00000000, 0x000000fa
        .word 0x000000cc, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000000, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000000, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000000, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000400, 0x00000000, 0x00000200, 0x00000000
        .word 0x00000400, 0x00000000, 0x00000200, 0x00000000
        .word 0x00000400, 0x00000000, 0x00000200, 0x00000000
        .word 0x00000400, 0x00000000, 0x00000200, 0x00000000
        .word 0x00000000, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000000, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000000, 0x00000000, 0x00000000, 0x02610000
        .word 0x00000000

s_hdr:    .asciz "\r\nm2mc palette tv probe\r\n"
s_tok:    .asciz "  timed list done, ticks (27 MHz): "
s_ttmo:   .asciz "  timed list TIMEOUT, ticks: "
s_chk:    .asciz "  scratch check: mismatching pixels / changed halfwords (want 0 / 00186a00): "
s_clear:  .asciz "  check passed: clearing the screen, drawing onto it\r\n"
s_shown:  .asciz "  drawn onto the screen\r\n"
s_skip:   .asciz "  check FAILED: screen left alone\r\n"
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
