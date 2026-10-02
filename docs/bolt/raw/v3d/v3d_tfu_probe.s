// AArch64 probe (go -64, EL2, MMU off): the first V3D job, a TFU job
// (texture formatting unit: reads an image, writes it in a GPU layout).
//
// Bring-up as in v3d_reset_probe.s (power up, reset, Nexus default registers),
// then one TFU job with the GPU MMU off (its reset state; Nexus runs jobs that
// way when it has no page table):
//   input  0x02000000: 16x16 RGBA8 raster, pixel (x,y) = 0xaa00yyxx
//   output 0x02100000: 64 KB pre-filled with 0xdeadbeef; format lineartile
// Register order and fields from nexus.ko BVC5_P_HardwareIssueTFUJob; codes from
// the stock libGLES_nexus.so tables: type rgba8 = 4, iformat raster = 0,
// oformat lineartile = 3. Waits for TFU_CS.CVTCT to count 1 (100 ms timeout),
// counts and dumps the output, flushes the GCA (Linux v3d_flush_l3), counts again,
// powers down. Ends in wfe; use --watchdog.
        .global _start
_start:
        mov     x0, #0x01080000
        mov     sp, x0
        msr     DAIFSet, #3                    // IRQ, FIQ off
        mov     x20, #0xc000
        movk    x20, #0xf040, lsl #16
        adr     x0, vectors
        msr     VBAR_EL2, x0
        isb
        adr     x0, s_hdr;  bl puts

        // power up
        ldr     x0, =0xf041d020
        mov     w1, #0x1d00
        bl      wr
        ldr     x26, =0xf041d020
        ldr     x27, =0x74000000
        ldr     x28, =0x34000000
        bl      poll

        // reset
        ldr     x0, =0xf12041b0
        mov     w1, #1
        bl      wr
        ldr     x26, =0xf12041b4
        mov     x27, #0xffffffff
        mov     x28, #3
        bl      poll
        ldr     x0, =0xf1204008
        mov     w1, #1
        bl      wr
        ldr     x0, =0xf1204008
        mov     w1, #0
        bl      wr
        ldr     x0, =0xf1200000
        mov     w1, #0xf
        bl      wr

        // default register state
        adr     x21, defaults
1:      ldp     w0, w1, [x21], #8
        cbz     w0, 2f
        bl      wr
        b       1b
2:
        // input: 16x16 pixels, 0xaa00yyxx
        ldr     x0, =0x02000000
        mov     w5, #0xaa000000
        mov     w2, #0                         // y
3:      mov     w1, #0                         // x
4:      orr     w3, w1, w2, lsl #8
        orr     w3, w3, w5
        str     w3, [x0], #4
        add     w1, w1, #1
        cmp     w1, #16
        b.lo    4b
        add     w2, w2, #1
        cmp     w2, #16
        b.lo    3b
        // output: 64 KB of 0xdeadbeef
        ldr     x0, =0x02100000
        ldr     w1, =0xdeadbeef
        mov     x2, #0x4000
5:      str     w1, [x0], #4
        subs    x2, x2, #1
        b.ne    5b
        dsb     sy

        adr     x0, s_before; bl puts
        bl      show

        // TFU job (Nexus order: COEF, IOS, IOA, IIS, IUA, ICA, IIA, ICFG last)
        adr     x21, tfujob
6:      ldp     w0, w1, [x21], #8
        cbz     w0, 7f
        bl      wr
        b       6b
7:      ldr     x26, =0xf1200400               // TFU_CS
        mov     x27, #0x00ff0000               // CVTCT
        mov     x28, #0x00010000               // one conversion done
        bl      poll
        adr     x0, s_after; bl puts
        bl      show
        bl      count
        bl      dump

        // GCA flush (Linux v3d_flush_l3: CACHE_CTRL |= FLUSH), then count again
        ldr     x0, =0xf120410c
        ldr     w1, [x0]
        orr     w1, w1, #1
        bl      wr
        mrs     x0, CNTPCT_EL0
        mov     x1, #27000                     // 1 ms
        add     x1, x0, x1
8:      mrs     x0, CNTPCT_EL0
        cmp     x0, x1
        b.lo    8b
        adr     x0, s_flush; bl puts
        ldr     x0, =0xf120410c; bl sread
        bl      count
        bl      dump

        // power down
        ldr     x26, =0xf041d020
        ldr     w1, [x26]
        tbz     w1, #26, 9f
        mov     x0, x26
        mov     w1, #0xb00
        bl      wr
        ldr     x27, =0x72000000
        ldr     x28, =0x42000000
        bl      poll
9:      adr     x0, s_down; bl puts
        ldr     x0, =0xf041d020; bl sread

        adr     x0, s_done; bl puts
10:     wfe
        b       10b

// count: words of the 64 KB output that are not 0xdeadbeef, in the first 1 KB
// and in the rest. Prints "  changed first-1KB rest".
count:  stp     x29, x30, [sp, #-16]!
        ldr     x0, =0x02100000
        ldr     w1, =0xdeadbeef
        mov     x2, #0                         // index
        mov     x3, #0                         // changed in first 1 KB
        mov     x5, #0                         // changed after
1:      ldr     w4, [x0, x2, lsl #2]
        cmp     w4, w1
        b.eq    2f
        cmp     x2, #256
        cinc    x3, x3, lo
        cinc    x5, x5, hs
2:      add     x2, x2, #1
        cmp     x2, #0x4000
        b.lo    1b
        stp     x3, x5, [sp, #-16]!
        adr     x0, s_cnt;  bl puts
        ldr     x0, [sp];   bl puthex8
        ldr     x0, [sp, #8]; bl puthex8
        add     sp, sp, #16
        adr     x0, s_crlf; bl puts
        ldp     x29, x30, [sp], #16
        ret

// dump: first 1 KB of the output, 8 words per line
dump:   stp     x29, x30, [sp, #-16]!
        stp     x21, x22, [sp, #-16]!
        ldr     x21, =0x02100000
        mov     x22, #0
1:      tst     x22, #7
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
// BVC5_P_HardwareSetDefaultRegisterState, core 0, in order (address, value).
defaults:
        .word 0xf1208060, 0xffffffff           // core INT_MSK_SET: mask all
        .word 0xf1208058, 0xffffffff           // core INT_CLR: clear all
        .word 0xf1208064, 0x00000007           // core INT_MSK_CLR: unmask bits 0-2
        .word 0xf1204100, 0x000e0000           // GCA +0x00 (no Linux name)
        .word 0xf1204120, 0x000f0002           // GCA +0x20 (no Linux name)
        .word 0xf1208018, 0x00000001           // core MISCCFG: OVRTMUOUT
        .word 0xf1200000, 0x0000000f           // hub AXICFG: MAX_LEN 15
        .word 0xf1201000, 0x00000001           // MMUC_CONTROL: ENABLE
        .word 0xf1208138, 0x00000001           // core CLE_RFC
        .word 0xf12081b0, 0x00000003           // core +0x1b0 (no Linux name)
        .word 0xf1200470, 0x00000003           // hub +0x470 (no Linux name)
        .word 0xf1208034, 0x00000000           // core L2TFLSTA
        .word 0xf1208038, 0xffffffff           // core L2TFLEND
        .word 0, 0
// TFU job, written in this order; ICFG (last) starts it.
tfujob:
        .word 0xf1200424, 0x00000000           // COEF0: no YUV coefficients
        .word 0xf1200420, 0x00100010           // IOS: height 16 << 16 | width 16
        .word 0xf120041c, 0x02100018           // IOA: output 0x02100000 | oformat lineartile (3) << 3
        .word 0xf1200414, 0x00000010           // IIS: input stride 16 pixels
        .word 0xf1200418, 0x00000000           // IUA: no U plane
        .word 0xf1200410, 0x00000000           // ICA: no chroma plane
        .word 0xf120040c, 0x02000000           // IIA: input 0x02000000
        .word 0xf1200408, 0x00000801           // ICFG: IOC | type rgba8 (4) << 9 | iformat raster (0) << 18
        .word 0, 0
// registers printed before and after the job
regs:
        .word 0xf1200050, 0xf120005c           // hub INT_STS, INT_MSK_STS
        .word 0xf1200400, 0xf1200404           // TFU_CS, TFU_SU
        .word 0xf1200408, 0xf120040c, 0xf1200410, 0xf1200414 // ICFG, IIA, ICA, IIS
        .word 0xf1200418, 0xf120041c, 0xf1200420, 0xf1200424 // IUA, IOA, IOS, COEF0
        .word 0xf1200434                       // TFU CRC
        .word 0xf1201200                       // MMU_CTL
        .word 0xf120410c                       // GCA CACHE_CTRL
        .word 0

s_hdr:    .asciz "\r\nv3d tfu probe\r\n"
s_before: .asciz " before the job:\r\n"
s_after:  .asciz " after the job:\r\n"
s_flush:  .asciz " after GCA flush:\r\n"
s_down:   .asciz " powered down:\r\n"
s_cnt:    .asciz "  output words changed, first 1 KB / rest: "
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
