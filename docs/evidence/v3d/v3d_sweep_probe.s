// AArch64 probe (go -64, EL2, MMU off): V3D register sweep, read-only apart
// from the tested power-up and reset (v3d_reset_probe.s): power island
// 0xf041d020, GCA safe shutdown, bridge SW_INIT pulse, AXICFG = 0xf.
// Then every register named in Linux drivers/gpu/drm/v3d/v3d_regs.h is read
// once with the abort-safe sread (hub 0xf1200000, bridge 0xf1204000, GCA
// 0xf1204100, core 0xf1208000), before the Nexus default registers are
// written, so the values are reset values. Then power down.
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

        // sweep: every named register, abort-safe
        adr     x0, s_swp;  bl puts
        adr     x21, sweep
1:      ldr     w0, [x21], #4
        cbz     w0, 2f
        bl      sread
        b       1b
2:
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
        .byte 0x79, 0x04, 0x00,0xff,0x00,0xff, 0x00,0x00,0x00    // clear colour word0 = 0xff00ff00
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

        .balign 4
sweep:
        .word 0xf1200000                        // V3D_HUB_AXICFG
        .word 0xf1200004                        // V3D_HUB_UIFCFG
        .word 0xf1200008                        // V3D_HUB_IDENT0
        .word 0xf120000c                        // V3D_HUB_IDENT1
        .word 0xf1200010                        // V3D_HUB_IDENT2
        .word 0xf1200014                        // V3D_HUB_IDENT3
        .word 0xf1200050                        // V3D_HUB_INT_STS
        .word 0xf1200054                        // V3D_HUB_INT_SET
        .word 0xf1200058                        // V3D_HUB_INT_CLR
        .word 0xf120005c                        // V3D_HUB_INT_MSK_STS
        .word 0xf1200060                        // V3D_HUB_INT_MSK_SET
        .word 0xf1200064                        // V3D_HUB_INT_MSK_CLR
        .word 0xf1200400                        // V3D_TFU_CS
        .word 0xf1200404                        // V3D_TFU_SU
        .word 0xf1200408                        // V3D_TFU_ICFG
        .word 0xf120040c                        // V3D_TFU_IIA
        .word 0xf1200410                        // V3D_TFU_ICA
        .word 0xf1200414                        // V3D_TFU_IIS
        .word 0xf1200418                        // V3D_TFU_IUA
        .word 0xf120041c                        // V3D_TFU_IOA
        .word 0xf1200420                        // V3D_TFU_IOS
        .word 0xf1200424                        // V3D_TFU_COEF0
        .word 0xf1200428                        // V3D_TFU_COEF1
        .word 0xf120042c                        // V3D_TFU_COEF2
        .word 0xf1200430                        // V3D_TFU_COEF3
        .word 0xf1200434                        // V3D_TFU_CRC
        .word 0xf1201000                        // V3D_MMUC_CONTROL
        .word 0xf1201200                        // V3D_MMU_CTL
        .word 0xf1201204                        // V3D_MMU_PT_PA_BASE
        .word 0xf1201208                        // V3D_MMU_HIT
        .word 0xf120120c                        // V3D_MMU_MISSES
        .word 0xf1201210                        // V3D_MMU_STALLS
        .word 0xf1201214                        // V3D_MMU_ADDR_CAP
        .word 0xf1201218                        // V3D_MMU_SHOOT_DOWN
        .word 0xf120121c                        // V3D_MMU_BYPASS_START
        .word 0xf1201220                        // V3D_MMU_BYPASS_END
        .word 0xf120122c                        // V3D_MMU_VIO_ID
        .word 0xf1201230                        // V3D_MMU_ILLEGAL_ADDR
        .word 0xf1201234                        // V3D_MMU_VIO_ADDR
        .word 0xf1201238                        // V3D_MMU_DEBUG_INFO
        .word 0xf1204000                        // V3D_TOP_GR_BRIDGE_REVISION
        .word 0xf1204008                        // V3D_TOP_GR_BRIDGE_SW_INIT_0
        .word 0xf120400c                        // V3D_TOP_GR_BRIDGE_SW_INIT_1
        .word 0xf120410c                        // V3D_GCA_CACHE_CTRL
        .word 0xf12041b0                        // V3D_GCA_SAFE_SHUTDOWN
        .word 0xf12041b4                        // V3D_GCA_SAFE_SHUTDOWN_ACK
        .word 0xf1208000                        // V3D_CTL_IDENT0
        .word 0xf1208004                        // V3D_CTL_IDENT1
        .word 0xf1208008                        // V3D_CTL_IDENT2
        .word 0xf1208018                        // V3D_CTL_MISCCFG
        .word 0xf1208020                        // V3D_CTL_L2CACTL
        .word 0xf1208024                        // V3D_CTL_SLCACTL
        .word 0xf1208030                        // V3D_CTL_L2TCACTL
        .word 0xf1208034                        // V3D_CTL_L2TFLSTA
        .word 0xf1208038                        // V3D_CTL_L2TFLEND
        .word 0xf1208050                        // V3D_CTL_INT_STS
        .word 0xf1208054                        // V3D_CTL_INT_SET
        .word 0xf1208058                        // V3D_CTL_INT_CLR
        .word 0xf120805c                        // V3D_CTL_INT_MSK_STS
        .word 0xf1208060                        // V3D_CTL_INT_MSK_SET
        .word 0xf1208064                        // V3D_CTL_INT_MSK_CLR
        .word 0xf1208100                        // V3D_CLE_CT0CS
        .word 0xf1208104                        // V3D_CLE_CT1CS
        .word 0xf1208108                        // V3D_CLE_CT0EA
        .word 0xf120810c                        // V3D_CLE_CT1EA
        .word 0xf1208110                        // V3D_CLE_CT0CA
        .word 0xf1208114                        // V3D_CLE_CT1CA
        .word 0xf1208118                        // V3D_CLE_CT0RA
        .word 0xf120811c                        // V3D_CLE_CT1RA
        .word 0xf1208120                        // V3D_CLE_CT0LC
        .word 0xf1208124                        // V3D_CLE_CT1LC
        .word 0xf1208128                        // V3D_CLE_CT0PC
        .word 0xf120812c                        // V3D_CLE_CT1PC
        .word 0xf1208130                        // V3D_CLE_PCS
        .word 0xf1208134                        // V3D_CLE_BFC
        .word 0xf1208138                        // V3D_CLE_RFC
        .word 0xf120813c                        // V3D_CLE_TFBC
        .word 0xf1208140                        // V3D_CLE_TFIT
        .word 0xf1208144                        // V3D_CLE_CT1CFG
        .word 0xf1208148                        // V3D_CLE_CT1TILECT
        .word 0xf120814c                        // V3D_CLE_CT1TSKIP
        .word 0xf1208150                        // V3D_CLE_CT1PTCT
        .word 0xf1208154                        // V3D_CLE_CT0SYNC
        .word 0xf1208158                        // V3D_CLE_CT1SYNC
        .word 0xf120815c                        // V3D_CLE_CT0QTS
        .word 0xf1208160                        // V3D_CLE_CT0QBA
        .word 0xf1208164                        // V3D_CLE_CT1QBA
        .word 0xf1208168                        // V3D_CLE_CT0QEA
        .word 0xf120816c                        // V3D_CLE_CT1QEA
        .word 0xf1208170                        // V3D_CLE_CT0QMA
        .word 0xf1208174                        // V3D_CLE_CT0QMS
        .word 0xf1208178                        // V3D_CLE_CT1QCFG
        .word 0xf1208300                        // V3D_PTB_BPCA
        .word 0xf1208304                        // V3D_PTB_BPCS
        .word 0xf1208308                        // V3D_PTB_BPOA
        .word 0xf120830c                        // V3D_PTB_BPOS
        .word 0xf1208310                        // V3D_PTB_BXCF
        .word 0xf1208650                        // V3D_V4_PCTR_0_EN
        .word 0xf1208654                        // V3D_V4_PCTR_0_CLR
        .word 0xf1208658                        // V3D_PCTR_0_OVERFLOW
        .word 0xf1208660                        // V3D_V3_PCTR_0_PCTRS15, V3D_V4_PCTR_0_SRC_0_3
        .word 0xf1208670                        // V3D_V3_PCTR_0_CLR
        .word 0xf1208674                        // V3D_V3_PCTR_0_EN
        .word 0xf120867c                        // V3D_V4_PCTR_0_SRC_28_31
        .word 0xf1208680                        // V3D_PCTR_0_PCTR0
        .word 0xf1208684                        // V3D_V3_PCTR_0_PCTRS0
        .word 0xf1208688                        // V3D_PCTR_0_PCTR2
        .word 0xf120868c                        // V3D_PCTR_0_PCTR3
        .word 0xf1208690                        // V3D_PCTR_0_PCTR4
        .word 0xf1208694                        // V3D_PCTR_0_PCTR5
        .word 0xf1208698                        // V3D_PCTR_0_PCTR6
        .word 0xf120869c                        // V3D_PCTR_0_PCTR7
        .word 0xf12086a0                        // V3D_PCTR_0_PCTR8
        .word 0xf12086a4                        // V3D_PCTR_0_PCTR9
        .word 0xf12086a8                        // V3D_PCTR_0_PCTR10
        .word 0xf12086ac                        // V3D_PCTR_0_PCTR11
        .word 0xf12086b0                        // V3D_PCTR_0_PCTR12
        .word 0xf12086b4                        // V3D_PCTR_0_PCTR13
        .word 0xf12086b8                        // V3D_PCTR_0_PCTR14
        .word 0xf12086bc                        // V3D_PCTR_0_PCTR15
        .word 0xf12086c0                        // V3D_PCTR_0_PCTR16
        .word 0xf12086c4                        // V3D_PCTR_0_PCTR17
        .word 0xf12086c8                        // V3D_PCTR_0_PCTR18
        .word 0xf12086cc                        // V3D_PCTR_0_PCTR19
        .word 0xf12086d0                        // V3D_PCTR_0_PCTR20
        .word 0xf12086d4                        // V3D_PCTR_0_PCTR21
        .word 0xf12086d8                        // V3D_PCTR_0_PCTR22
        .word 0xf12086dc                        // V3D_PCTR_0_PCTR23
        .word 0xf12086e0                        // V3D_PCTR_0_PCTR24
        .word 0xf12086e4                        // V3D_PCTR_0_PCTR25
        .word 0xf12086e8                        // V3D_PCTR_0_PCTR26
        .word 0xf12086ec                        // V3D_PCTR_0_PCTR27
        .word 0xf12086f0                        // V3D_PCTR_0_PCTR28
        .word 0xf12086f4                        // V3D_PCTR_0_PCTR29
        .word 0xf12086f8                        // V3D_PCTR_0_PCTR30
        .word 0xf12086fc                        // V3D_PCTR_0_PCTR31
        .word 0xf1208800                        // V3D_GMP_STATUS
        .word 0xf1208804                        // V3D_GMP_CFG
        .word 0xf1208808                        // V3D_GMP_VIO_ADDR
        .word 0xf120880c                        // V3D_GMP_VIO_TYPE
        .word 0xf1208810                        // V3D_GMP_TABLE_ADDR
        .word 0xf1208814                        // V3D_GMP_CLEAR_LOAD
        .word 0xf1208818                        // V3D_GMP_PRESERVE_LOAD
        .word 0xf1208820                        // V3D_GMP_VALID_LINES
        .word 0xf1208900                        // V3D_CSD_STATUS
        .word 0xf1208904                        // V3D_CSD_QUEUED_CFG0
        .word 0xf1208908                        // V3D_CSD_QUEUED_CFG1
        .word 0xf120890c                        // V3D_CSD_QUEUED_CFG2
        .word 0xf1208910                        // V3D_CSD_QUEUED_CFG3
        .word 0xf1208914                        // V3D_CSD_QUEUED_CFG4
        .word 0xf1208918                        // V3D_CSD_QUEUED_CFG5
        .word 0xf120891c                        // V3D_CSD_QUEUED_CFG6
        .word 0xf1208920                        // V3D_CSD_CURRENT_CFG0
        .word 0xf1208924                        // V3D_CSD_CURRENT_CFG1
        .word 0xf1208928                        // V3D_CSD_CURRENT_CFG2
        .word 0xf120892c                        // V3D_CSD_CURRENT_CFG3
        .word 0xf1208930                        // V3D_CSD_CURRENT_CFG4
        .word 0xf1208934                        // V3D_CSD_CURRENT_CFG5
        .word 0xf1208938                        // V3D_CSD_CURRENT_CFG6
        .word 0xf120893c                        // V3D_CSD_CURRENT_ID0
        .word 0xf1208940                        // V3D_CSD_CURRENT_ID1
        .word 0xf1208f04                        // V3D_ERR_FDBGO
        .word 0xf1208f08                        // V3D_ERR_FDBGB
        .word 0xf1208f0c                        // V3D_ERR_FDBGR
        .word 0xf1208f10                        // V3D_ERR_FDBGS
        .word 0xf1208f20                        // V3D_ERR_STAT
        .word 0
s_swp:    .asciz " register sweep:\r\n"
s_hdr:    .asciz "\r\nv3d sweep probe\r\n"
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
