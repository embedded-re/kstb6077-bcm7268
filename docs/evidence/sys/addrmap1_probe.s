// AArch64 probe (go -64, EL2, MMU off), address-map items 1.6-1.8:
//  1. wake timer 0xf041a080: EVENT/COUNTER/ALARM, then ALARM = COUNTER + 3,
//     watch EVENT and the AON L2 controller 0xf0410640 (DTB: waketimer = bit 4)
//  2. GISB arbiter 0xf0400000-0x7fc: every word
//  3. boot SRAM 0xffe00000-0xffe1ffff: hex dump; all-zero lines print "*"
// Reads go through read32 (abort-safe, see ../periph/periph_probe.s).
// Writes: wake timer EVENT (W1C) and ALARM, AON L2 CPU_CLEAR bit 4 only.
// Ends in wfe; run with --watchdog 60.
        .global _start
_start:
        mov     x0, #0x01080000
        mov     sp, x0
        mov     x20, #0xc000
        movk    x20, #0xf040, lsl #16          // UART0
        adr     x0, vectors
        msr     VBAR_EL2, x0
        isb

// ---- 1. wake timer
        mov     x26, #0xa080
        movk    x26, #0xf041, lsl #16          // wake timer
        mov     x27, #0x0640
        movk    x27, #0xf041, lsl #16          // AON L2 (sys_pm)
        adr     x0, s_wk;   bl puts
        bl      wkline
        mov     w0, #1
        str     w0, [x26]                      // EVENT: write 1 to clear
        mov     w0, #0x10
        str     w0, [x27, #8]                  // L2 CPU_CLEAR bit 4
        dsb     sy
        adr     x0, s_clr;  bl puts
        bl      wkline
        add     x0, x26, #4
        bl      read32
        add     w0, w0, #3
        str     w0, [x26, #8]                  // ALARM = COUNTER + 3
        dsb     sy
        adr     x0, s_set;  bl puts
        bl      wkline
        mov     x28, #8
1:      bl      delay1s
        bl      wkline
        subs    x28, x28, #1
        b.ne    1b
        mov     w0, #1
        str     w0, [x26]                      // EVENT: clear
        dsb     sy
        adr     x0, s_clr;  bl puts
        bl      wkline
        mov     w0, #0x10
        str     w0, [x27, #8]                  // L2 clear bit 4
        dsb     sy
        adr     x0, s_l2c;  bl puts
        bl      wkline

// ---- 2. GISB arbiter
        adr     x0, s_gisb; bl puts
        mov     x0, #0xf0400000
        mov     x1, #0x800
        bl      dump

// ---- 3. boot SRAM
        adr     x0, s_sram; bl puts
        mov     x0, #0xffe00000
        mov     x1, #0x20000
        bl      dump

        adr     x0, s_done; bl puts
8:      wfe
        b       8b

// wkline: print "COUNTER EVENT ALARM L2status L2mask"
wkline: stp     x29, x30, [sp, #-16]!
        add     x0, x26, #4;  bl read32; bl puthex8
        mov     x0, x26;      bl read32; bl puthex8
        add     x0, x26, #8;  bl read32; bl puthex8
        mov     x0, x27;      bl read32; bl puthex8
        add     x0, x27, #0xc; bl read32; bl puthex8
        adr     x0, s_crlf;   bl puts
        ldp     x29, x30, [sp], #16
        ret

// dump: x0 = start, x1 = length (multiple of 32). 8 words per line, each
// read once; an aborted word prints as "--------"; all-zero lines print "*".
dump:   stp     x29, x30, [sp, #-16]!
        mov     x21, x0
        add     x22, x0, x1
        mov     x29, #0                        // 1 = last line was all-zero
        adr     x9, vals
        adr     x13, flags
1:      cmp     x21, x22
        b.hs    9f
        mov     x10, #0                        // OR of values and flags
        mov     x12, #0
2:      add     x0, x21, x12, lsl #2
        bl      read32                         // keeps x9-x13
        str     w0, [x9, x12, lsl #2]
        strb    w19, [x13, x12]
        orr     x10, x10, x0
        orr     x10, x10, x19
        add     x12, x12, #1
        cmp     x12, #8
        b.ne    2b
        cbnz    x10, 3f
        cbnz    x29, 7f
        mov     x29, #1
        adr     x0, s_star; bl puts
        b       7f
3:      mov     x29, #0
        mov     x0, x21;    bl puthex8
        mov     x12, #0
4:      ldrb    w0, [x13, x12]
        cbz     w0, 5f
        adr     x0, s_ab;   bl puts
        b       6f
5:      ldr     w0, [x9, x12, lsl #2]
        bl      puthex8
6:      add     x12, x12, #1
        cmp     x12, #8
        b.ne    4b
        adr     x0, s_crlf; bl puts
7:      add     x21, x21, #32
        b       1b
9:      ldp     x29, x30, [sp], #16
        ret

// read32: x0 = address -> w0 = value; x19 = 0 ok, 1 sync abort, 2 SError
read32: mov     x19, #0
        mov     w23, #0
        ldr     w23, [x0]                      // the sync handler skips this
        dsb     sy
        isb
        msr     DAIFClr, #4
        isb
        nop
        msr     DAIFSet, #4
        mov     w0, w23
        ret

delay1s: mrs    x0, CNTPCT_EL0
        mov     x1, #0xfcc0
        movk    x1, #0x019b, lsl #16           // 27,000,000
        add     x1, x0, x1
1:      mrs     x0, CNTPCT_EL0
        cmp     x0, x1
        b.lo    1b
        ret

        .balign 2048
vectors:
        .rept 4
        b       hang
        .balign 128
        .endr
        b       sync_h
        .balign 128
        b       hang
        .balign 128
        b       hang
        .balign 128
        b       serr_h
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
puthex8:                                       // w0 + space. Clobbers x0-x4.
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

s_wk:   .asciz "\r\nwake timer: COUNTER EVENT ALARM  AON-L2 status mask\r\n"
s_clr:  .asciz "EVENT and L2 bit 4 cleared:\r\n"
s_set:  .asciz "ALARM = COUNTER + 3, then once a second:\r\n"
s_l2c:  .asciz "L2 bit 4 cleared:\r\n"
s_gisb: .asciz "GISB 0xf0400000-0x7ff:\r\n"
s_sram: .asciz "boot SRAM 0xffe00000-0xffe1ffff:\r\n"
s_star: .asciz "*\r\n"
s_ab:   .asciz "-------- "
s_hang: .asciz "\r\nUNEXPECTED EXCEPTION esr/elr: "
s_crlf: .asciz "\r\n"
s_done: .asciz "probe done\r\n"
        .balign 8
vals:   .space 32
flags:  .space 8
