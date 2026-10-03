// AArch64 probe (go -64, EL2, MMU off): M2MC palette lookup, scratch RAM only.
// One packet: 16x8 palette-8 source -> 16x8 RGB565 at 0x02500000, scaler off.
// Source format = BM2MC format 33 (BPXL 0x12e40008, the palette family):
// table words 00030008, 0, 00001c00 -> source feeder words 11-13 =
// 00030008, 0, 00211c01 (Nexus rule: word 13 = table word 2 | 00210000 | bit 0).
// Format 27 (0x01390008, used in m2mc_pal*_probe.s before) is A8: colour came
// from the constant. Constant colour (word 18) stays ff123456 to show that.
//   source  0x02400000, pitch 16: index(x,y) = y*16 + x (0..127)
//   palette 0x02410000, 256 x 32-bit: entry i = 0xff00a030 | i << 16
//   output pitch 32, 192 KB pre-filled with 0xdeadbeef
// Palette = group bit 1, words (02410000, 0), after the colour matrix group.
// Reset and list start as in m2mc_fill_probe.s. Ends in wfe; use --watchdog.
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
        // source: index (x,y) = y*16 + x, 128 bytes
        ldr     x0, =0x02400000
        mov     w1, #0
3:      strb    w1, [x0], #1
        add     w1, w1, #1
        cmp     w1, #128
        b.lo    3b
        // palette: entry i = 0xff00a030 | i << 16
        ldr     x0, =0x02410000
        ldr     w2, =0xff00a030
        mov     w1, #0
4:      orr     w3, w2, w1, lsl #16
        str     w3, [x0], #4
        add     w1, w1, #1
        cmp     w1, #256
        b.lo    4b
        // output area: 64 KB of 0xdeadbeef
        ldr     x0, =0x02500000
        ldr     w1, =0xdeadbeef
        mov     x2, #0xc000
5:      str     w1, [x0], #4
        subs    x2, x2, #1
        b.ne    5b
        dsb     sy

        // one packet
        ldr     x0, =0xf09b0014
        adr     x1, pk1
        bl      wr
        ldr     x0, =0xf09b000c
        mov     w1, #6
        bl      wr
        ldr     x26, =0xf09b0018               // wait until CURR_PKT_ADDR = last packet
        mov     x27, #0xffffffff
        adr     x28, pk1
        bl      poll
        ldr     x26, =0xf09b0010               // and LIST_STATUS = 2
        mov     x28, #2
        bl      poll
        ldr     x26, =0xf09b001c               // and BLIT_STATUS = 0
        mov     x28, #0
        bl      poll
        adr     x0, s_after; bl puts
        bl      show
        adr     x23, lists
6:      ldp     w0, w1, [x23], #8              // output address, words per row
        cbz     w0, 8f
        bl      dump
        b       6b
8:      bl      count
        adr     x0, s_done; bl puts
9:      wfe
        b       9b

// dump(x0 = buffer, x1 = words per row): 8 or 16 rows
dump:   stp     x29, x30, [sp, #-16]!
        stp     x21, x22, [sp, #-16]!
        stp     x23, x24, [sp, #-16]!
        mov     x21, x0
        mov     x23, x1
        lsl     x24, x1, #3                    // 8 rows of x1 words ...
        cmp     x1, #8
        b.eq    0f
        lsl     x24, x1, #4                    // ... or 16 rows
0:      mov     x22, #0
1:      udiv    x0, x22, x23
        msub    x0, x0, x23, x22
        cbnz    x0, 2f
        adr     x0, s_crlf; bl puts
        adr     x0, s_ind;  bl puts
2:      ldr     w0, [x21, x22, lsl #2]
        bl      puthex8
        add     x22, x22, #1
        cmp     x22, x24
        b.lo    1b
        adr     x0, s_crlf; bl puts
        ldp     x23, x24, [sp], #16
        ldp     x21, x22, [sp], #16
        ldp     x29, x30, [sp], #16
        ret

// count: changed words in each 64 KB output area outside its image
count:  stp     x29, x30, [sp, #-16]!
        ldr     w1, =0xdeadbeef
        adr     x5, areas
1:      ldp     w0, w2, [x5], #8               // area, first word after the image
        cbz     w0, 3f
        mov     x3, #0
2:      ldr     w4, [x0, x2, lsl #2]
        cmp     w4, w1
        cinc    x3, x3, ne
        add     x2, x2, #1
        cmp     x2, #0x4000
        b.lo    2b
        stp     x5, x1, [sp, #-16]!
        str     x3, [sp, #-16]!
        adr     x0, s_cnt;  bl puts
        ldr     x0, [sp];   bl puthex8
        add     sp, sp, #16
        adr     x0, s_crlf; bl puts
        ldp     x5, x1, [sp], #16
        b       1b
3:      ldp     x29, x30, [sp], #16
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
lists:  .word 0x02500000, 8, 0, 0
areas:  .word 0x02500000, 64, 0, 0
        .balign 32
pk1:    // format 33 (palette-8) -> 0x02500000; last packet
        .word 1
        .word 0x00007ff2, 0x00000001, 0x00000000, 0x02400000
        .word 0x00000000, 0x00000000, 0x00000010, 0x00000000
        .word 0x00000000, 0x00000000, 0x00000000, 0x00000000
        .word 0x00030008, 0x00000000, 0x00211c01, 0x00000000
        .word 0x00000000, 0x00211e01, 0x00000000, 0xff123456
        .word 0x00000000, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000000, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000000, 0x00001e01, 0x00000000, 0x00000000
        .word 0x00000001, 0x00000000, 0x02500000, 0x00000020
        .word 0x00000000, 0x00000000, 0x00000000, 0x00000565
        .word 0x000b0500, 0x00001000, 0x00004000, 0x00000000
        .word 0x00100008, 0x00000010, 0x00000000, 0x00100008
        .word 0x00000000, 0x00000000, 0x00000000, 0x00100008
        .word 0x00000010, 0x00000000, 0x00100008, 0x00000010
        .word 0x00000000, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000000, 0x00020000, 0x00000000, 0x00000000
        .word 0x00000000, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000000, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000000, 0x00000000, 0x00000000, 0x00000201
        .word 0x00000205, 0x00000000, 0x000000fa, 0x000000cc
        .word 0x00000000, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000000, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000000, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000000, 0x00000000, 0x00000000, 0x00000400
        .word 0x00000000, 0x00000400, 0x00000000, 0x00000400
        .word 0x00000000, 0x00000400, 0x00000000, 0x00000400
        .word 0x00000000, 0x00000400, 0x00000000, 0x00000400
        .word 0x00000000, 0x00000400, 0x00000000, 0x00000000
        .word 0x00000000, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000000, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000000, 0x00000000, 0x02410000, 0x00000000

s_hdr:    .asciz "\r\nm2mc palette probe 4\r\n"
s_after:  .asciz " after the list:\r\n"
s_cnt:    .asciz "  changed words outside the image, per area: "
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
