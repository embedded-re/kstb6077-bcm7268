// AArch64 probe (go -64, EL2, MMU off): power the V3D GPU up, reset it the
// way the stock nexus.ko does, replay BVC5_P_HardwareSetDefaultRegisterState,
// then power it down again. Prints the V3D registers after each step.
//
//   power up   (BVC5_P_HardwareBPCMPowerUp):  0xf041d020 = 0x1d00, wait (v & 0x74000000) == 0x34000000
//   reset      (BVC5_P_HardwareResetV3D):     GCA safe shutdown 0xf12041b0 = 1, wait 0xf12041b4 == 3,
//                                             bridge SW_INIT_0 0xf1204008 = 1, then 0
//              (Linux v3d_reset_by_bridge):   hub AXICFG 0xf1200000 = 0xf
//   defaults   (BVC5_P_HardwareSetDefaultRegisterState), minus the two writes
//              whose value comes from Nexus settings (0xf12041d0, 0xf12041d4)
//   power down (BVC5_P_HardwareBPCMPowerDown): 0xf041d020 = 0xb00, wait (v & 0x72000000) == 0x42000000
//
// Polls time out after 100 ms (nexus waits forever). Reads are abort-safe.
// Ends in wfe; use --watchdog.
        .global _start
_start:
        mov     x0, #0x01080000
        mov     sp, x0
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
        adr     x0, s_up;   bl puts
        bl      show

        // reset: GCA safe shutdown, then bridge SW_INIT_0 pulse, then AXICFG
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
        adr     x0, s_reset; bl puts
        bl      show

        // default register state, in Nexus's order
        adr     x21, defaults
1:      ldp     w0, w1, [x21], #8
        cbz     w0, 2f
        bl      wr
        b       1b
2:      adr     x0, s_def;  bl puts
        bl      show

        // power down (only if bit 26 is set, as Nexus does)
        ldr     x26, =0xf041d020
        ldr     w1, [x26]
        tbz     w1, #26, 3f
        mov     x0, x26
        mov     w1, #0xb00
        bl      wr
        ldr     x27, =0x72000000
        ldr     x28, =0x42000000
        bl      poll
3:      adr     x0, s_down; bl puts
        ldr     x0, =0xf041d020; bl sread
        ldr     x0, =0xf1200008; bl sread

        adr     x0, s_done; bl puts
9:      wfe
        b       9b

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
// registers printed after each step
regs:
        .word 0xf041d020                       // power island (BPCM)
        .word 0xf1204000, 0xf1204008           // bridge REVISION, SW_INIT_0
        .word 0xf1204100, 0xf120410c, 0xf1204120 // GCA +0x00, CACHE_CTRL, +0x20
        .word 0xf12041b0, 0xf12041b4           // GCA SAFE_SHUTDOWN, _ACK
        .word 0xf12041d0, 0xf12041d4           // GCA +0xd0, +0xd4 (Nexus settings)
        .word 0xf1200000                       // hub AXICFG
        .word 0xf1200008, 0xf120000c, 0xf1200010, 0xf1200014 // hub IDENT0-3
        .word 0xf1200050, 0xf120005c           // hub INT_STS, INT_MSK_STS
        .word 0xf1200400, 0xf1200404           // TFU_CS, TFU_SU
        .word 0xf1200470                       // hub +0x470
        .word 0xf1201000, 0xf1201100, 0xf1201200, 0xf1201204 // MMUC_CONTROL, MMU 0x1100, MMU_CTL, PT_PA_BASE
        .word 0xf1208000, 0xf1208004, 0xf1208008 // core IDENT0-2
        .word 0xf1208018                       // core MISCCFG
        .word 0xf1208034, 0xf1208038           // core L2TFLSTA, L2TFLEND
        .word 0xf1208050, 0xf120805c           // core INT_STS, INT_MSK_STS
        .word 0xf1208138, 0xf12081b0           // core CLE_RFC, +0x1b0
        .word 0

s_hdr:    .asciz "\r\nv3d reset probe\r\n"
s_up:     .asciz " powered up:\r\n"
s_reset:  .asciz " after reset:\r\n"
s_def:    .asciz " after default register state:\r\n"
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
