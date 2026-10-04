// AArch64 probe (go -64, EL2, MMU off): V3D render job channel order: the
// v3d_render_probe.s job with clear colour packet bytes 00 11 22 33 44.
// The clear word is packet bytes 2-5, so the word sent was 0x33221100; the
// count below looks for 0x44332211 and so counts the tile as 'other changed'. The first render job, with no
// binner and no shaders. One 64x64 tile is cleared to a colour and stored
// to RAM as a raster RGBA8 image.
//
// Bring-up as in v3d_reset_probe.s. Control lists built after the stock
// libGLES_nexus.so (create_cls_and_flush, glxx_hw_create_generic_tile_list);
// submission after nexus.ko BVC5_P_HardwareIssueRenderJob and
// BVC5_P_HardwarePrepareForJob:
//   L2TCACTL 0xf1208030 = 1, SLCACTL 0xf1208024 = 0x0f0f0f0f (cache flushes)
//   INT_CLR 0xf1208058 = 7, CT1QCFG 0xf1208178 = 1,
//   CT1QBA 0xf1208164 = rcl, CT1QEA 0xf120816c = rcl_end (starts the job)
// Waits for core INT_STS bit 0 (FRDONE), 100 ms timeout.
//   colour buffer 0x02200000 (64 KB pre-filled with 0xdeadbeef), clear word 0x33221100
//   (four different bytes: shows which clear byte lands in which output byte)
//   depth buffer  0x02300000 (64 KB pre-filled; must stay unchanged, no depth store)
// Ends in wfe; use --watchdog.
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
        // colour and depth buffers: 64 KB each of 0xdeadbeef
        ldr     x0, =0x02200000
        bl      fill
        ldr     x0, =0x02300000
        bl      fill
        dsb     sy

        adr     x0, s_before; bl puts
        bl      show

        // render job
        adr     x21, job
6:      ldp     w0, w1, [x21], #8
        cbz     w0, 7f
        bl      wr
        b       6b
7:      ldr     x26, =0xf1208050               // core INT_STS
        mov     x27, #1                        // FRDONE
        mov     x28, #1
        bl      poll
        adr     x0, s_after; bl puts
        bl      show
        adr     x0, s_col;  bl puts
        ldr     x0, =0x02200000
        bl      count
        bl      dump
        adr     x0, s_dep;  bl puts
        ldr     x0, =0x02300000
        bl      count

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

// fill(x0): 64 KB of 0xdeadbeef
fill:   ldr     w1, =0xdeadbeef
        mov     x2, #0x4000
1:      str     w1, [x0], #4
        subs    x2, x2, #1
        b.ne    1b
        ret

// count(x0 = buffer): over its 64 KB, prints
// "  0x44332211 in first 16 KB / other changed in first 16 KB / changed after 16 KB"
count:  stp     x29, x30, [sp, #-16]!
        ldr     w1, =0xdeadbeef
        ldr     w6, =0x44332211
        mov     x2, #0                         // index
        mov     x3, #0                         // == clear colour, first 16 KB
        mov     x5, #0                         // other changed, first 16 KB
        mov     x7, #0                         // changed after 16 KB
1:      ldr     w4, [x0, x2, lsl #2]
        cmp     w4, w1
        b.eq    3f
        cmp     x2, #0x1000
        b.hs    2f
        cmp     w4, w6
        cinc    x3, x3, eq
        cinc    x5, x5, ne
        b       3f
2:      add     x7, x7, #1
3:      add     x2, x2, #1
        cmp     x2, #0x4000
        b.lo    1b
        stp     x3, x5, [sp, #-16]!
        str     x7, [sp, #-16]!
        ldr     x0, [sp, #16]; bl puthex8
        ldr     x0, [sp, #24]; bl puthex8
        ldr     x0, [sp];      bl puthex8
        add     sp, sp, #32
        adr     x0, s_crlf; bl puts
        ldp     x29, x30, [sp], #16
        ret

// dump: colour buffer words 0-7, 64-71 (row 1), 4088-4095 (end of row 63), 4096-4103 (after)
dump:   stp     x29, x30, [sp, #-16]!
        stp     x21, x22, [sp, #-16]!
        adr     x22, dumpidx
1:      ldr     w21, [x22], #4
        cmn     w21, #1
        b.eq    3f
        adr     x0, s_ind;  bl puts
        mov     x0, x21;    bl puthex8
        adr     x0, s_colon; bl puts
        ldr     x1, =0x02200000
        add     x21, x1, x21, lsl #2
        mov     x2, #8
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
dumpidx: .word 0, 64, 4088, 4096, -1

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
// render job: register writes in order; CT1QEA (last) starts it.
job:
        .word 0xf1208030, 0x00000001           // L2TCACTL: L2TFLS (flush L2T)
        .word 0xf1208024, 0x0f0f0f0f           // SLCACTL: flush slice caches
        .word 0xf1208058, 0x00000007           // core INT_CLR: FRDONE, FLDONE, OUTOMEM
        .word 0xf1208178, 0x00000001           // CT1QCFG: MCDIS
        .word 0xf1208164, rcl                  // CT1QBA: render control list start
        .word 0xf120816c, rcl_end              // CT1QEA: end, starts the job
        .word 0, 0
// registers printed before and after the job
regs:
        .word 0xf1208050, 0xf120805c           // core INT_STS, INT_MSK_STS
        .word 0xf1208100, 0xf1208104           // CT0CS, CT1CS
        .word 0xf120810c, 0xf1208114           // CT1EA, CT1CA
        .word 0xf1208138                       // CLE_RFC
        .word 0xf1200050                       // hub INT_STS
        .word 0
        .balign 256
// render control list (90 bytes)
rcl:
        .byte 0x79, 0x00, 0x40,0x00, 0x40,0x00, 0x40, 0x00, 0x0e  // common: 1 RT, 64x64, 32 bpp, ez off, store RT0 only
        .byte 0x79, 0x02, 0x08, 0x1b, 0xf0, 0x00,0x00,0x20,0x02  // colour RT0: internal 8, rgba8, raster, pad f, 0x02200000
        .byte 0x79, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,0x30,0x02 // z/stencil: 32f, address 0x02300000 (not stored)
        .byte 0x79, 0x04, 0x00,0x11,0x22,0x33,0x44, 0x00,0x00    // clear colour: bytes 2-5 = word0 = 0x33221100 (0x44 goes to the upper bits)
        .byte 0x79, 0x06, 0x00,0x00, 0x40,0x00, 0x00,0x00, 0x00  // clear part3: raster padded width 64
        .byte 0x79, 0x03, 0x00, 0x00,0x00,0x80,0x3f, 0x00,0x00   // z/s clear: stencil 0, depth 1.0
        .byte 0x7c, 0x00,0x00,0x00                               // tile coords (0,0)
        .byte 0x1d, 0x08, 0x02, 0x00, 0x00,0x00,0x00             // store general: buffer none, clears on (dummy tile)
        .byte 0x13                                               // clear VCD cache
        .byte 0x7e, 0x04                                         // tile list initial block size 64, chain
        .byte 0x7a, 0x00,0x00, 0x01,0x01, 0x01,0x10,0x00, 0x10   // supertiles 1x1 tile, frame 1x1, raster order
        .byte 0x14
        .4byte gtl, gtl_end                                      // generic tile list start, end
        .byte 0x17, 0x00, 0x00                                   // supertile (0,0)
        .byte 0x0d                                               // end render
rcl_end:
        .space 32
        .balign 64
// generic tile list, run for each tile
gtl:
        .byte 0x7d                                               // implicit tile coords (end of loads)
        .byte 0x18                                               // store subsample (RT0 per common config)
        .byte 0x12                                               // return
gtl_end:
        .space 32

s_hdr:    .asciz "\r\nv3d order probe\r\n"
s_before: .asciz " before the job:\r\n"
s_after:  .asciz " after the job:\r\n"
s_col:    .asciz "  colour buffer, clear-colour words / other changed (first 16 KB) / changed after: "
s_dep:    .asciz "  depth buffer, same counts: "
s_colon:  .asciz ": "
s_down:   .asciz " powered down:\r\n"
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
