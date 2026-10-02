// AArch64 probe (go -64, EL2, MMU off): power the V3D GPU's power island up
// and down with the sequence from the stock nexus.ko:
//   up   (BVC5_P_HardwareBPCMPowerUp):   0xf041d020 = 0x1d00, wait (v & 0x74000000) == 0x34000000
//   down (BVC5_P_HardwareBPCMPowerDown): if bit 26: 0xf041d020 = 0xb00, wait (v & 0x72000000) == 0x42000000
// Polls time out after 100 ms (nexus waits forever). Reads are abort-safe.
// Prints the BPCM register and the V3D ID registers before / powered up / powered down.
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

        ldr     x26, =0xf041d020               // V3D power island control (BPCM)
        adr     x0, s_before; bl puts
        bl      show

        // power up (nexus.ko BVC5_P_HardwareBPCMPowerUp)
        mov     w1, #0x1d00
        str     w1, [x26]
        dsb     sy
        ldr     x27, =0x74000000               // mask
        ldr     x28, =0x34000000               // "powered up"
        bl      poll
        adr     x0, s_up;   bl puts
        bl      show

        // power down (nexus.ko BVC5_P_HardwareBPCMPowerDown: only if bit 26 set)
        ldr     w1, [x26]
        tbz     w1, #26, 1f
        mov     w1, #0xb00
        str     w1, [x26]
        dsb     sy
        ldr     x27, =0x72000000
        ldr     x28, =0x42000000               // "powered down"
        bl      poll
1:      adr     x0, s_down; bl puts
        bl      show

        adr     x0, s_done; bl puts
9:      wfe
        b       9b

// poll: wait until (*x26 & x27) == x28, at most 100 ms. Prints the result and time in us.
poll:   stp     x29, x30, [sp, #-16]!
        mrs     x21, CNTPCT_EL0
        ldr     x0, =2700000
        add     x22, x21, x0
1:      ldr     w0, [x26]
        and     x1, x0, x27
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
        adr     x0, s_us;   bl puts
        ldp     x29, x30, [sp], #16
        ret

show:   stp     x29, x30, [sp, #-16]!
        ldr     x0, =0xf041d020; bl sread
        ldr     x0, =0xf1204000; bl sread      // bridge REVISION
        ldr     x0, =0xf1200008; bl sread      // hub IDENT0
        ldr     x0, =0xf120000c; bl sread      // hub IDENT1
        ldr     x0, =0xf1200010; bl sread      // hub IDENT2
        ldr     x0, =0xf1200014; bl sread      // hub IDENT3
        ldr     x0, =0xf1208000; bl sread      // core0 IDENT0
        ldr     x0, =0xf1208004; bl sread      // core0 IDENT1
        ldr     x0, =0xf1208008; bl sread      // core0 IDENT2
        ldp     x29, x30, [sp], #16
        ret

// sread(x0 = address): one abort-safe 32-bit read, printed as
// "  addr value" or "  addr SYNC esr" / "SERR esr".
sread:  stp     x29, x30, [sp, #-16]!
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
s_hdr:    .asciz "\r\nv3d power probe\r\n"
s_up:     .asciz " powered up:\r\n"
s_down:   .asciz " powered down:\r\n"
s_ok:     .asciz " poll ok after us: "
s_tmo:    .asciz " poll TIMEOUT after us: "
s_us:     .asciz "\r\n"
s_before: .asciz " before:\r\n"
s_ind:    .asciz "  "
s_sync:   .asciz "SYNC "
s_serr:   .asciz "SERR "
s_hang:   .asciz "\r\nUNEXPECTED EXCEPTION esr/elr: "
s_crlf:   .asciz "\r\n"
s_done:   .asciz "probe done\r\n"
