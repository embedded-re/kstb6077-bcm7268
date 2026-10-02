// AArch64 probe (go -64, EL2, MMU off): release one reset bit, see whether a
// switched-off block starts answering, then put the bit back. Reads are
// abort-safe (sync handler skips the load; SError unmasked after each read).
// Test A, PCIe: 0xf0469210 bit 1 (bridge reset, Linux RGR1_SW_INIT_1 INIT) -> 0,
//   PERST (bit 0) untouched; check 0xf046406c (Linux MISC_REVISION), 0xf0460000.
// Test B, USB BDC: 0xf0b00234 bit 23 (Linux USB_PM BDC_SOFT_RESETB) -> 1;
//   check 0xf0b02000, 0xf0b02004.
// Each test: before / released / restored. Ends in wfe; use --watchdog.
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

        // ---- Test A: PCIe
        adr     x0, s_a;    bl puts
        ldr     x26, =0xf0469210
        adr     x0, s_before; bl puts
        bl      showA
        ldr     w1, [x26]
        bic     w1, w1, #2                     // bridge out of reset, PERST stays
        str     w1, [x26]
        dsb     sy
        bl      delay1ms
        adr     x0, s_rel;  bl puts
        bl      showA
        ldr     w1, [x26]
        orr     w1, w1, #2                     // back into reset
        str     w1, [x26]
        dsb     sy
        bl      delay1ms
        adr     x0, s_rest; bl puts
        bl      showA

        // ---- Test B: USB device controller
        adr     x0, s_b;    bl puts
        ldr     x26, =0xf0b00234
        adr     x0, s_before; bl puts
        bl      showB
        ldr     w1, [x26]
        orr     w1, w1, #(1 << 23)             // BDC out of soft reset
        str     w1, [x26]
        dsb     sy
        bl      delay1ms
        adr     x0, s_rel;  bl puts
        bl      showB
        ldr     w1, [x26]
        bic     w1, w1, #(1 << 23)             // back into reset
        str     w1, [x26]
        dsb     sy
        bl      delay1ms
        adr     x0, s_rest; bl puts
        bl      showB

        adr     x0, s_done; bl puts
9:      wfe
        b       9b

showA:  stp     x29, x30, [sp, #-16]!
        ldr     x0, =0xf0469210; bl sread
        ldr     x0, =0xf046406c; bl sread
        ldr     x0, =0xf0460000; bl sread
        ldp     x29, x30, [sp], #16
        ret
showB:  stp     x29, x30, [sp, #-16]!
        ldr     x0, =0xf0b00234; bl sread
        ldr     x0, =0xf0b02000; bl sread
        ldr     x0, =0xf0b02004; bl sread
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

delay1ms:
        mrs     x0, CNTPCT_EL0
        mov     x1, #27000
        add     x0, x0, x1
1:      mrs     x1, CNTPCT_EL0
        cmp     x1, x0
        b.lo    1b
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
s_hdr:    .asciz "\r\nreset release probe\r\n"
s_a:      .asciz "== A: PCIe 0xf0469210 bit 1\r\n"
s_b:      .asciz "== B: USB BDC 0xf0b00234 bit 23\r\n"
s_before: .asciz " before:\r\n"
s_rel:    .asciz " released:\r\n"
s_rest:   .asciz " restored:\r\n"
s_ind:    .asciz "  "
s_sync:   .asciz "SYNC "
s_serr:   .asciz "SERR "
s_hang:   .asciz "\r\nUNEXPECTED EXCEPTION esr/elr: "
s_crlf:   .asciz "\r\n"
s_done:   .asciz "probe done\r\n"
