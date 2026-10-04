// AArch64 probe (go -64, EL2, MMU off): M2MC back-to-back lists without a
// reset, scratch RAM only. Nexus BGRC_PACKET_P_ProcessSwPktFifo: the first
// batch starts with +0x14 = packet, +0x0c = 6; a later batch is linked by
// writing its first packet's address into word 0 of the previous batch's
// last packet, then +0x0c = 3.
//   list 1: packet A, palette 1 -> 0x02500000, +0x14 = A, +0x0c = 6
//   then:   A word 0 = B, +0x0c = 3; B = palette 2 -> 0x02510000
//   then:   B word 0 = C, +0x0c = 3; C = palette 1 -> 0x02520000
// Each packet is the m2mc_pal4_probe.s packet (palette-8 16x8 -> RGB565
// 16x8, format 33), last in its batch (word 0 = 1, blit word 0 bit 14).
//   source    0x02400000, pitch 16: index(x,y) = y*16 + x (0..127)
//   palette 1 0x02410000: entry i = 0xff00a030 | i << 16 (red = i)
//   palette 2 0x02420000: entry i = 0xff000000 | i       (blue = i)
//   outputs pitch 32, 192 KB pre-filled with 0xdeadbeef
// Reset as in m2mc_fill_probe.s. Ends in wfe; use --watchdog.
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
        // palette 2: entry i = 0xff000000 | i
        ldr     x0, =0x02420000
        mov     w1, #0
4:      orr     w3, w1, #0xff000000
        str     w3, [x0], #4
        add     w1, w1, #1
        cmp     w1, #256
        b.lo    4b
        // output areas: 192 KB of 0xdeadbeef
        ldr     x0, =0x02500000
        ldr     w1, =0xdeadbeef
        mov     x2, #0xc000
5:      str     w1, [x0], #4
        subs    x2, x2, #1
        b.ne    5b
        dsb     sy

        // list 1: packet A, started with +0x0c = 6
        ldr     x0, =0xf09b0014
        adr     x1, pkA
        bl      wr
        ldr     x0, =0xf09b000c
        mov     w1, #6
        bl      wr
        adr     x28, pkA
        bl      waitpk
        // link B onto A, +0x0c = 3
        adr     x0, s_link; bl puts
        adr     x0, pkA
        adr     x1, pkB
        str     w1, [x0]
        dsb     sy
        ldr     x0, =0xf09b000c
        mov     w1, #3
        bl      wr
        adr     x28, pkB
        bl      waitpk
        // link C onto B, +0x0c = 3
        adr     x0, s_link; bl puts
        adr     x0, pkB
        adr     x1, pkC
        str     w1, [x0]
        dsb     sy
        ldr     x0, =0xf09b000c
        mov     w1, #3
        bl      wr
        adr     x28, pkC
        bl      waitpk
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

// waitpk(x28 = packet): wait until +0x18 = packet, +0x10 = 2, +0x1c = 0,
// then print the list registers
waitpk: stp     x29, x30, [sp, #-16]!
        ldr     x26, =0xf09b0018               // CURR_PKT_ADDR = this packet
        mov     x27, #0xffffffff
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
lists:  .word 0x02500000, 8, 0x02510000, 8, 0x02520000, 8, 0, 0
areas:  .word 0x02500000, 64, 0x02510000, 64, 0x02520000, 64, 0, 0
        .balign 32
pkA:    // palette 1 -> 0x02500000; word 0 = 1, set to pkB later
        .word 0x00000001, 0x00007ff2, 0x00000001, 0x00000000
        .word 0x02400000, 0x00000000, 0x00000000, 0x00000010
        .word 0x00000000, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000000, 0x00030008, 0x00000000, 0x00211c01
        .word 0x00000000, 0x00000000, 0x00211e01, 0x00000000
        .word 0xff123456, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000000, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000000, 0x00000000, 0x00001e01, 0x00000000
        .word 0x00000000, 0x00000001, 0x00000000, 0x02500000
        .word 0x00000020, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000565, 0x000b0500, 0x00001000, 0x00004000
        .word 0x00000000, 0x00100008, 0x00000010, 0x00000000
        .word 0x00100008, 0x00000000, 0x00000000, 0x00000000
        .word 0x00100008, 0x00000010, 0x00000000, 0x00100008
        .word 0x00000010, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000000, 0x00000000, 0x00020000, 0x00000000
        .word 0x00000000, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000000, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000000, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000201, 0x00000205, 0x00000000, 0x000000fa
        .word 0x000000cc, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000000, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000000, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000000, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000400, 0x00000000, 0x00000400, 0x00000000
        .word 0x00000400, 0x00000000, 0x00000400, 0x00000000
        .word 0x00000400, 0x00000000, 0x00000400, 0x00000000
        .word 0x00000400, 0x00000000, 0x00000400, 0x00000000
        .word 0x00000000, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000000, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000000, 0x00000000, 0x00000000, 0x02410000
        .word 0x00000000
        .balign 32
pkB:    // palette 2 -> 0x02510000; word 0 = 1, set to pkC later
        .word 0x00000001, 0x00007ff2, 0x00000001, 0x00000000
        .word 0x02400000, 0x00000000, 0x00000000, 0x00000010
        .word 0x00000000, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000000, 0x00030008, 0x00000000, 0x00211c01
        .word 0x00000000, 0x00000000, 0x00211e01, 0x00000000
        .word 0xff123456, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000000, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000000, 0x00000000, 0x00001e01, 0x00000000
        .word 0x00000000, 0x00000001, 0x00000000, 0x02510000
        .word 0x00000020, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000565, 0x000b0500, 0x00001000, 0x00004000
        .word 0x00000000, 0x00100008, 0x00000010, 0x00000000
        .word 0x00100008, 0x00000000, 0x00000000, 0x00000000
        .word 0x00100008, 0x00000010, 0x00000000, 0x00100008
        .word 0x00000010, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000000, 0x00000000, 0x00020000, 0x00000000
        .word 0x00000000, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000000, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000000, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000201, 0x00000205, 0x00000000, 0x000000fa
        .word 0x000000cc, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000000, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000000, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000000, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000400, 0x00000000, 0x00000400, 0x00000000
        .word 0x00000400, 0x00000000, 0x00000400, 0x00000000
        .word 0x00000400, 0x00000000, 0x00000400, 0x00000000
        .word 0x00000400, 0x00000000, 0x00000400, 0x00000000
        .word 0x00000000, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000000, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000000, 0x00000000, 0x00000000, 0x02420000
        .word 0x00000000
        .balign 32
pkC:    // palette 1 -> 0x02520000; last
        .word 0x00000001, 0x00007ff2, 0x00000001, 0x00000000
        .word 0x02400000, 0x00000000, 0x00000000, 0x00000010
        .word 0x00000000, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000000, 0x00030008, 0x00000000, 0x00211c01
        .word 0x00000000, 0x00000000, 0x00211e01, 0x00000000
        .word 0xff123456, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000000, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000000, 0x00000000, 0x00001e01, 0x00000000
        .word 0x00000000, 0x00000001, 0x00000000, 0x02520000
        .word 0x00000020, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000565, 0x000b0500, 0x00001000, 0x00004000
        .word 0x00000000, 0x00100008, 0x00000010, 0x00000000
        .word 0x00100008, 0x00000000, 0x00000000, 0x00000000
        .word 0x00100008, 0x00000010, 0x00000000, 0x00100008
        .word 0x00000010, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000000, 0x00000000, 0x00020000, 0x00000000
        .word 0x00000000, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000000, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000000, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000201, 0x00000205, 0x00000000, 0x000000fa
        .word 0x000000cc, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000000, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000000, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000000, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000400, 0x00000000, 0x00000400, 0x00000000
        .word 0x00000400, 0x00000000, 0x00000400, 0x00000000
        .word 0x00000400, 0x00000000, 0x00000400, 0x00000000
        .word 0x00000400, 0x00000000, 0x00000400, 0x00000000
        .word 0x00000000, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000000, 0x00000000, 0x00000000, 0x00000000
        .word 0x00000000, 0x00000000, 0x00000000, 0x02410000
        .word 0x00000000

s_hdr:    .asciz "\r\nm2mc continue probe\r\n"
s_after:  .asciz " after the list:\r\n"
s_link:   .asciz " link the next packet, +0x0c = 3:\r\n"
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
