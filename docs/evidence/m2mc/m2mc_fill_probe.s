// AArch64 probe (go -64, EL2, MMU off): the M2MC 2D blitter's first job,
// a solid fill of a 16x8 rectangle at (8,4) in a 64x32 RGB565 test surface
// at 0x02400000 (pitch 128 bytes), in free RAM (not the framebuffer).
//
// Reset: nexus.ko BGRC_P_ResetDevice (base 0xf09b0000), with a 100 ms timeout
// on its wait. Packet layout: BGRC_PACKET_P_WriteHwPkt; group contents from
// BGRC_PACKET_P_ProcessSwPacket / ResetState. Size words are width << 16 | height
// (BGRC_PACKET_P_ProcSwPktFillBlit); position words are y << 16 | x.
// Start: +0x14 = packet address, +0x0c = 6. Done: +0x10 == 2 (100 ms timeout),
// then +0x1c (blit status) should read 0.
// The test surface's 64 KB area is pre-filled with 0xdeadbeef.
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
        ldr     x26, =0xf09b001c               // wait for blit status != 0
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
2:      adr     x0, s_reset; bl puts
        bl      show

        // test surface area: 64 KB of 0xdeadbeef
        ldr     x0, =0x02400000
        ldr     w1, =0xdeadbeef
        mov     x2, #0x4000
3:      str     w1, [x0], #4
        subs    x2, x2, #1
        b.ne    3b
        dsb     sy

        // start the list
        ldr     x0, =0xf09b0014
        adr     x1, packet
        bl      wr
        ldr     x0, =0xf09b000c
        mov     w1, #6
        bl      wr
        ldr     x26, =0xf09b0010               // LIST_STATUS
        mov     x27, #0xffffffff
        mov     x28, #2                        // finished
        bl      poll
        adr     x0, s_after; bl puts
        bl      show
        adr     x0, s_pk;   bl puts
        adr     x0, packet; bl puthex8
        adr     x0, s_crlf; bl puts
        bl      count
        bl      dump

        adr     x0, s_done; bl puts
9:      wfe
        b       9b

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

// count: halfwords of the 4 KB surface: 0xf800 inside the rectangle /
// other changed inside / changed outside the rectangle; then changed words
// after the first 4 KB of the 64 KB area
count:  stp     x29, x30, [sp, #-16]!
        ldr     x0, =0x02400000
        mov     w1, #0xbeef
        mov     w11, #0xdead
        mov     x3, #0                         // 0xf800 inside
        mov     x5, #0                         // other changed inside
        mov     x7, #0                         // changed outside
        mov     x9, #0                         // y
1:      mov     x10, #0                        // x
2:      add     x2, x10, x9, lsl #6            // halfword index
        ldrh    w4, [x0, x2, lsl #1]
        tst     x10, #1
        csel    w12, w1, w11, eq               // expected unchanged halfword
        cmp     w4, w12
        b.eq    5f
        cmp     x10, #8
        b.lo    4f
        cmp     x10, #24
        b.hs    4f
        cmp     x9, #4
        b.lo    4f
        cmp     x9, #12
        b.hs    4f
        mov     w13, #0xf800
        cmp     w4, w13
        cinc    x3, x3, eq
        cinc    x5, x5, ne
        b       5f
4:      add     x7, x7, #1
5:      add     x10, x10, #1
        cmp     x10, #64
        b.lo    2b
        add     x9, x9, #1
        cmp     x9, #32
        b.lo    1b
        // words 1024..16383
        ldr     w1, =0xdeadbeef
        mov     x2, #1024
        mov     x8, #0
6:      ldr     w4, [x0, x2, lsl #2]
        cmp     w4, w1
        cinc    x8, x8, ne
        add     x2, x2, #1
        cmp     x2, #0x4000
        b.lo    6b
        stp     x3, x5, [sp, #-16]!
        stp     x7, x8, [sp, #-16]!
        adr     x0, s_cnt;  bl puts
        ldr     x0, [sp, #16]; bl puthex8
        ldr     x0, [sp, #24]; bl puthex8
        ldr     x0, [sp];      bl puthex8
        ldr     x0, [sp, #8];  bl puthex8
        add     sp, sp, #32
        adr     x0, s_crlf; bl puts
        ldp     x29, x30, [sp], #16
        ret

// dump: surface rows 3, 4, 11, 12, words 0-15 (pixels 0-31)
dump:   stp     x29, x30, [sp, #-16]!
        stp     x21, x22, [sp, #-16]!
        adr     x22, rows
1:      ldr     w21, [x22], #4
        cmn     w21, #1
        b.eq    3f
        adr     x0, s_row;  bl puts
        mov     x0, x21;    bl puthex8
        adr     x0, s_colon; bl puts
        ldr     x1, =0x02400000
        add     x21, x1, x21, lsl #7
        mov     x2, #16
2:      ldr     w0, [x21], #4
        str     x2, [sp, #-16]!
        bl      puthex8
        ldr     x2, [sp], #16
        subs    x2, x2, #1
        b.ne    2b
        adr     x0, s_crlf; bl puts
        b       1b
3:      ldp     x21, x22, [sp], #16
        ldp     x29, x30, [sp], #16
        ret
rows:   .word 3, 4, 11, 12, -1

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
// BGRC_P_ResetDevice, after the second +0x1808 pulse (address, value)
init:
        .word 0xf09b02f0, 0x00000100
        .word 0xf09b0060, 0x82409024
        .word 0xf09b0064, 0x00000024
        .word 0xf09b006c, 0x00100000
        .word 0xf09b0068, 0x00000111
        .word 0, 0                             // +0xb0 left at its value (2)
regs:
        .word 0xf09b000c, 0xf09b0010, 0xf09b0014, 0xf09b0018, 0xf09b001c
        .word 0xf09b0060, 0xf09b0064, 0xf09b0068, 0xf09b006c, 0xf09b00b0
        .word 0xf09b02f0, 0xf09b1808
        .word 0
        .balign 32
// M2MC hardware packet (BGRC_PACKET_P_WriteHwPkt layout), 492 bytes
packet:
        .word 0x00000001, 0x00007ff0       // next = 1 (last packet), group mask bits 14-4
        // source feeder (constant colour 0xffff0000, ARGB8888)
        .word 0x00000001, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000000, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000000, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000000, 0x00211e01, 0x00000000, 0x00000000
        .word 0x00211e01, 0x000000ff, 0xffff0000
        // destination feeder (none)
        .word 0x00000000, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000000, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000000, 0x00001e01, 0x00000000, 0x00000000
        // output feeder: 0x02400000, pitch 128, RGB565
        .word 0x00000001, 0x00000000, 0x02400000, 0x00000080
        .word 0x00000000, 0x00000000, 0x00000000, 0x00000565
        .word 0x000b0500, 0x00001000
        // blit: rect x=8 y=4 16x8, last packet (bit 14)
        .word 0x00024000, 0x00040008, 0x00100008, 0x00000010
        .word 0x00000000, 0x00000000, 0x00000000, 0x00000000
        .word 0x00040008, 0x00100008, 0x00000000, 0x00040008
        .word 0x00100008, 0x00000010, 0x00000000, 0x00000000
        .word 0x00000000, 0x00000000, 0x00000000, 0x00020000
        // scaler (off)
        .word 0x00000000, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000000, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000000, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000000
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
        // filter coefficients (bilinear)
        .word 0x00000000, 0x00000400, 0x00000000, 0x00000200
        .word 0x00000000, 0x00000400, 0x00000000, 0x00000200
        .word 0x00000000, 0x00000400, 0x00000000, 0x00000200
        .word 0x00000000, 0x00000400, 0x00000000, 0x00000200
        // source colour matrix (off)
        .word 0x00000000, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000000, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000000, 0x00000000, 0x00000000, 0x00000000

s_hdr:    .asciz "\r\nm2mc fill probe\r\n"
s_reset:  .asciz " after reset:\r\n"
s_after:  .asciz " after the list:\r\n"
s_pk:     .asciz "  packet at "
s_cnt:    .asciz "  0xf800 in rect / other changed in rect / changed outside rect / changed words after 4 KB: "
s_row:    .asciz "  row "
s_colon:  .asciz ": "
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
