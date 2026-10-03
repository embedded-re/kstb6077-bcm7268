// AArch64 probe (go -64, EL2, MMU off): read-only dump of the IR receiver
// (KBD) registers that Nexus BKIR_* names, for all three channels
// (kbd1 0xf0419900, kbd2 0xf0419980, kbd3 0xf0419a00), plus the PM AON
// config word 0xf0419880 and the AON L2 interrupt controller 0xf0419c00.
// One abort-safe 32-bit read per address; an aborted read prints xxxxxxxx.
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
        adr     x26, addrs
next:   ldr     w22, [x26], #4
        cbz     w22, done
        mov     x0, x22;    bl puthex8
        mov     x19, #0
        ldr     w23, [x22]
        dsb     sy
        isb
        msr     DAIFClr, #4
        isb
        nop
        msr     DAIFSet, #4
        cbz     x19, 1f
        adr     x0, s_x;    bl puts
        b       2f
1:      mov     w0, w23;    bl puthex8
2:      adr     x0, s_crlf; bl puts
        b       next
done:   adr     x0, s_done; bl puts
9:      wfe
        b       9b

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
sync_h: mov     x19, #1
        mrs     x25, ELR_EL2
        add     x25, x25, #4
        msr     ELR_EL2, x25
        mov     w23, #0
        eret
serr_h: mov     x19, #2
        mov     w23, #0
        eret
hang:   adr     x0, s_hang; bl puts
        mrs     x0, ESR_EL2; bl puthex8
        mrs     x0, ELR_EL2; bl puthex8
1:      wfe
        b       1b

// puts / puthex8 do not touch x9-x12, x19-x28.
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

        .macro  chan base
        .word   \base+0x00, \base+0x08, \base+0x0c, \base+0x10, \base+0x14
        .word   \base+0x18, \base+0x1c, \base+0x20, \base+0x24, \base+0x28
        .word   \base+0x2c, \base+0x30, \base+0x34
        .endm
        .balign 4
addrs:  .word   0xf0419880
        chan    0xf0419900
        chan    0xf0419980
        chan    0xf0419a00
        .word   0xf0419c00, 0xf0419c04, 0xf0419c08, 0xf0419c0c
        .word   0
s_hdr:  .asciz "\r\nIR read probe (KBD regs, read-only)\r\n"
s_x:    .asciz "xxxxxxxx "
s_hang: .asciz "\r\nUNEXPECTED EXCEPTION esr/elr: "
s_crlf: .asciz "\r\n"
s_done: .asciz "probe done\r\n"
