// AArch64 probe (go -64, EL2, MMU off): first V3D triangle, scratch RAM only.
// A binner job, then the render job of v3d_fb_scr_probe.s (one 64x64 tile,
// RGB565 output format 7, stride 1920, cleared to red 0x000000ff), with the
// binner's tile list run from the generic tile list.
//
// Binner job (nexus.ko BVC5_P_HardwareIssueBinnerJob order):
//   L2TCACTL = 1, SLCACTL = 0f0f0f0f, INT_CLR = 7,
//   PTB_BPOS 0xf120830c = 0, CT0QMA 0xf1208170 = 0x04000000 (tile alloc
//   memory), CT0QMS 0xf1208174 = 0x00100000, CT0QBA 0xf1208160 = bcl,
//   CT0QEA 0xf1208168 = bcl_end (starts it); wait for INT_STS bit 1 (FLDONE).
// Binning control list (libGLES create_cls_and_flush and glxx_draw_rect):
//   78 tile binning mode cfg part 1 (tile state 0x04100000, auto-init,
//   blocks 64/128, 1x1 tiles, 1 RT 32 bpp), 13, 06 start tile binning,
//   0e 00, 5c 0, then clip window 0,0 64x64, clipz 0..1.0, cfg bits 03 70 00,
//   colour write masks 0, viewport offset 0, vcm cache size 44,
//   nv_shader (44, record | 2 attributes), vertex array prims (24):
//   triangles, 3 vertices from 0; then 04 flush.
// NV shader record (60 bytes, libGLES v3d_create_nv_shader_record):
//   no vertex shader; vertices X, Y as 24.8 fixed point pixels, Z float.
// Fragment shader: libGLES v3d_clear_shader_color, 16-bit path, 6
//   instructions, uniforms R|G<<16 (f16), TLB config ffffffff, B|A<<16:
//   blue 0, 0, 1.0, 1.0 -> RGB565 001f.
// Triangle (8,8) (56,8) (32,56) in the 64x64 tile at 0x031dcb40 inside the
// 4 MB scratch area at 0x03000000 (0xdeadbeef).
// Run 1 sent width/height bytes 00 00 00 (taken as minus one): the binner read
// its list, FLDONE never came, BPCA moved 0xaa000, no triangle
// (v3d_tri_scr_output_run1_tiles0.txt). libGLES packs tile counts as they are.
// Opcode names: libGLES v3d_desc_cl_opcode; bin/render validity:
// v3d_cl_instr_ok_in_bin / _render.
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
        // depth buffer: 64 KB of 0xdeadbeef
        ldr     x0, =0x02300000
        bl      fill
        // tile state 0x04100000 (4 KB) and tile alloc 0x04000000 (64 KB): zero
        ldr     x0, =0x04100000
        mov     x2, #0x1000
3:      str     wzr, [x0], #4
        subs    x2, x2, #4
        b.ne    3b
        ldr     x0, =0x04000000
        mov     x2, #0x10000
3:      str     wzr, [x0], #4
        subs    x2, x2, #4
        b.ne    3b
        // scratch area: 4 MB of 0xdeadbeef
        ldr     x0, =0x03000000
        ldr     w1, =0xdeadbeef
        ldr     x2, =0x3f4800
3:      str     w1, [x0], #4
        subs    x2, x2, #4
        b.ne    3b
        dsb     sy

        adr     x0, s_before; bl puts
        bl      show

        // binner job
        adr     x21, bjob
5:      ldp     w0, w1, [x21], #8
        cbz     w0, 4f
        bl      wr
        b       5b
4:      ldr     x26, =0xf1208050               // core INT_STS
        mov     x27, #2                        // FLDONE
        mov     x28, #2
        bl      poll
        adr     x0, s_bin;  bl puts
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
        bl      check
        bl      dump
        bl      spans
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
// "  0xff00ff00 in first 16 KB / other changed in first 16 KB / changed after 16 KB"
count:  stp     x29, x30, [sp, #-16]!
        ldr     w1, =0xdeadbeef
        ldr     w6, =0xff00ff00
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

// check: over the 4 MB scratch area, prints changed halfwords / f800 / 001f
// (want 00001000 and 4096 of one of the two)
check:  stp     x29, x30, [sp, #-16]!
        adr     x0, s_chk;  bl puts
        ldr     x0, =0x03000000
        ldr     x2, =0x1fa400                  // halfwords
        mov     w8, #0xbeef
        mov     w9, #0xdead
        mov     w10, #0xf800
        mov     x3, #0
        mov     x4, #0
        mov     x5, #0
        mov     x1, #0
1:      ldrh    w6, [x0, x1, lsl #1]
        tst     x1, #1
        csel    w7, w8, w9, eq
        cmp     w6, w7
        cinc    x3, x3, ne
        cmp     w6, w10
        cinc    x4, x4, eq
        cmp     w6, #0x1f
        cinc    x5, x5, eq
        add     x1, x1, #1
        cmp     x1, x2
        b.lo    1b
        stp     x3, x4, [sp, #-16]!
        str     x5, [sp, #-16]!
        ldr     x0, [sp, #16]; bl puthex8
        ldr     x0, [sp, #24]; bl puthex8
        ldr     x0, [sp];      bl puthex8
        add     sp, sp, #32
        adr     x0, s_crlf; bl puts
        ldp     x29, x30, [sp], #16
        ret

// spans: for each of the 64 tile rows, "row first-x last-x" of the 001f pixels
// (ffffffff 00000000 when the row has none)
spans:  stp     x29, x30, [sp, #-16]!
        stp     x21, x22, [sp, #-16]!
        stp     x23, x24, [sp, #-16]!
        adr     x0, s_spn;  bl puts
        mov     x21, #0                        // row
1:      ldr     x22, =0x031dcb40
        mov     x0, #3840
        madd    x22, x21, x0, x22
        mov     x23, #-1                       // first
        mov     x24, #0                        // last
        mov     x1, #0
2:      ldrh    w2, [x22, x1, lsl #1]
        cmp     w2, #0x1f
        b.ne    3f
        cmn     x23, #1
        csel    x23, x1, x23, eq
        mov     x24, x1
3:      add     x1, x1, #1
        cmp     x1, #64
        b.lo    2b
        adr     x0, s_ind;  bl puts
        mov     x0, x21;    bl puthex8
        mov     x0, x23;    bl puthex8
        mov     x0, x24;    bl puthex8
        adr     x0, s_crlf; bl puts
        add     x21, x21, #1
        cmp     x21, #64
        b.lo    1b
        ldp     x23, x24, [sp], #16
        ldp     x21, x22, [sp], #16
        ldp     x29, x30, [sp], #16
        ret

// dump: 8 words at each dumpidx word offset from the tile address
dump:   stp     x29, x30, [sp, #-16]!
        stp     x21, x22, [sp, #-16]!
        adr     x22, dumpidx
1:      ldr     w21, [x22], #4
        cmn     w21, #1
        b.eq    3f
        adr     x0, s_ind;  bl puts
        mov     x0, x21;    bl puthex8
        adr     x0, s_colon; bl puts
        ldr     x1, =0x031dcb40
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
dumpidx: .word 0, 28, 60480, 61440, -1         // tile row 0 px 0-15, row 0 px 56-71, row 63, row 64 (words, pitch 960)

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
bjob:
        .word 0xf1208030, 0x00000001           // L2TCACTL: flush L2T
        .word 0xf1208024, 0x0f0f0f0f           // SLCACTL: flush slice caches
        .word 0xf1208058, 0x00000007           // core INT_CLR
        .word 0xf120830c, 0x00000000           // PTB_BPOS: no overflow memory
        .word 0xf1208170, 0x04000000           // CT0QMA: tile alloc memory
        .word 0xf1208174, 0x00100000           // CT0QMS: its size, 1 MB
        .word 0xf1208160, bcl                  // CT0QBA: binning list start
        .word 0xf1208168, bcl_end              // CT0QEA: end, starts the binner
        .word 0, 0
// registers printed before and after the job
regs:
        .word 0xf1208050, 0xf120805c           // core INT_STS, INT_MSK_STS
        .word 0xf1208100, 0xf1208104           // CT0CS, CT1CS
        .word 0xf1208108, 0xf1208110           // CT0EA, CT0CA
        .word 0xf1208300                       // PTB_BPCA
        .word 0xf120810c, 0xf1208114           // CT1EA, CT1CA
        .word 0xf1208134, 0xf1208138           // CLE_BFC, CLE_RFC
        .word 0xf1200050                       // hub INT_STS
        .word 0
        .balign 256
// render control list (90 bytes)
rcl:
        .byte 0x79, 0x00, 0x40,0x00, 0x40,0x00, 0x40, 0x00, 0x0e  // common: 1 RT, 64x64, 32 bpp, ez off, store RT0 only
        .byte 0x79, 0x02, 0x08, 0x07, 0xf0, 0x40,0xcb,0x1d,0x03  // colour RT0: internal 8, bgr565 (7), raster, pad f, 0x031dcb40
        .byte 0x79, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,0x30,0x02 // z/stencil: 32f, address 0x02300000 (not stored)
        .byte 0x79, 0x04, 0xff,0x00,0x00,0x00, 0x00,0x00,0x00    // clear colour: bytes 2-5 = word 0 = 0x000000ff
        .byte 0x79, 0x06, 0x00,0x00, 0x80,0x07, 0x00,0x00, 0x00  // clear part3: raster row stride 1920 pixels
        .byte 0x79, 0x03, 0x00, 0x00,0x00,0x80,0x3f, 0x00,0x00   // z/s clear: stencil 0, depth 1.0
        .byte 0x7c, 0x00,0x00,0x00                               // tile coords (0,0)
        .byte 0x1d, 0x08, 0x02, 0x00, 0x00,0x00,0x00             // store general: buffer none, clears on (dummy tile)
        .byte 0x13                                               // clear VCD cache
        .byte 0x7e, 0x04                                         // tile list initial block size 64, chain
        .byte 0x7b, 0x00,0x00,0x00,0x04                          // tile list base: set 0, 0x04000000 (tile alloc)
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
        .byte 0x15, 0x00                                         // branch to implicit tile list, set 0
        .byte 0x18                                               // store subsample (RT0 per common config)
        .byte 0x12                                               // return
gtl_end:
        .space 32

        .balign 64
// binning control list
bcl:
        .byte 0x78, 0x12,0x00,0x10,0x04, 0x01,0x10,0x00,0x00     // tile binning cfg part 1: tile state 0x04100000, auto-init, blocks 64/128; width 1, height 1 tiles (counts, not minus one); 1 RT, 32 bpp
        .byte 0x13                                               // clear VCD cache
        .byte 0x06                                               // start tile binning
        .byte 0x0e, 0x00                                         // wait transform feedback, 0
        .byte 0x5c, 0x00,0x00,0x00,0x00                          // occlusion query counter: off
        .byte 0x6b, 0x00,0x00, 0x00,0x00, 0x40,0x00, 0x40,0x00   // clip window 0,0 64x64
        .byte 0x6d, 0x00,0x00,0x00,0x00, 0x00,0x00,0x80,0x3f     // clipz: 0.0 .. 1.0
        .byte 0x60, 0x03, 0x70, 0x00                             // cfg bits: both faces, depth func always
        .byte 0x57, 0x00,0x00,0x00,0x00                          // colour write masks: all on
        .byte 0x6c, 0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00     // viewport offset 0
        .byte 0x49, 0x44                                         // vcm cache size
        .byte 0x44                                               // nv_shader: record | 2 attribute arrays
        .4byte nvrec + 2
        .byte 0x24, 0x04, 0x03,0x00,0x00,0x00, 0x00,0x00,0x00,0x00   // vertex array prims: triangles, 3, first 0
        .byte 0x04                                               // flush
bcl_end:
        .space 32
        .balign 64
// NV shader record (60 bytes)
nvrec:
        .word 0x00000000, 0x00010001, attrdef, fshader
        .word unifs, 0, 0, 0
        .word 0
        .word attrdef, 0x00000408, 0                             // attribute 0: defaults
        .word verts, 0x0000440b, 12                              // attribute 1: X, Y, Z, stride 12
        .balign 16
attrdef:
        .word 0, 0, 0, 0, 0, 0, 0, 0x3f800000                    // vec4(0,0,0,0), vec4(0,0,0,1.0)
        .balign 16
verts:
        .word 8 << 8, 8 << 8, 0
        .word 56 << 8, 8 << 8, 0
        .word 32 << 8, 56 << 8, 0
        .balign 16
unifs:
        .word 0x00000000, 0xffffffff, 0x3c003c00                 // R|G f16, TLB config, B|A f16 (blue)
        .balign 64
// fragment shader: libGLES v3d_clear_shader_color, 16-bit path
fshader:
        .quad 0x3c403186bb800000                                 // nop; ldunif
        .quad 0x3c003188b682d000                                 // or tlbu, r5, r5
        .quad 0x3c403186bb800000                                 // nop; ldunif
        .quad 0x3c203187b682d000                                 // or tlb, r5, r5; thrsw
        .quad 0x3c003186bb800000                                 // nop
        .quad 0x3c003186bb800000                                 // nop
        .space 32

s_spn:    .asciz "  blue span per row: row, first x, last x\r\n"
s_bin:    .asciz " after the binner job:\r\n"
s_hdr:    .asciz "\r\nv3d tri scr probe\r\n"
s_before: .asciz " before the job:\r\n"
s_after:  .asciz " after the job:\r\n"
s_chk:    .asciz "  scratch: changed halfwords / f800 / 001f: "
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
