// AArch64 probe (go -64, EL2, MMU off): read one word from each address in a
// list and survive aborts. Prints per address:
//   "addr value"            the read worked
//   "addr SYNC esr"         synchronous external abort (handler skips the ldr)
//   "addr SERR esr"         asynchronous SError, taken when unmasked right after the read
// Addresses: words inside the DTB's PCIe range, and the whole USB control
// block (DTB usb-phy@f0b00200).
// Read-only. Ends in wfe; use --watchdog as a backup.
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
        adr     x21, list
1:      ldr     x22, [x21], #8
        cbz     x22, 9f
        mov     x0, x22;    bl puthex8
        mov     x19, #0                        // fault flag: 0 none, 1 sync, 2 SError
        ldr     w23, [x22]                     // THE read (sync handler skips it)
        dsb     sy
        isb
        msr     DAIFClr, #4                    // unmask SError: a pending one is taken now
        isb
        nop
        msr     DAIFSet, #4
        cbz     x19, 2f
        cmp     x19, #1
        adr     x0, s_sync
        adr     x1, s_serr
        csel    x0, x0, x1, eq
        bl      puts
        mov     x0, x24                        // ESR saved by the handler
        bl      puthex8
        b       3f
2:      mov     x0, x23;    bl puthex8
3:      adr     x0, s_crlf; bl puts
        b       1b
9:      adr     x0, s_done; bl puts
8:      wfe
        b       8b

// ---- exceptions: sync at EL2 -> skip the instruction; SError -> just note it
        .balign 2048
vectors:
        .rept 4                                // current EL with SP_EL0 (unused)
        b       hang
        .balign 128
        .endr
        b       sync_h                         // 0x200 current EL, SP_ELx: synchronous
        .balign 128
        b       hang                           // 0x280 IRQ
        .balign 128
        b       hang                           // 0x300 FIQ
        .balign 128
        b       serr_h                         // 0x380 SError
        .balign 128
        .rept 8                                // lower ELs (unused)
        b       hang
        .balign 128
        .endr

sync_h: mrs     x24, ESR_EL2
        mov     x19, #1
        mrs     x25, ELR_EL2
        add     x25, x25, #4                   // skip the faulting ldr
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

        .balign 8
list:   // PCIe (DTB range 0xf0460000 + 0x9310; offsets from the Linux driver layout, less trusted)
        .quad 0xf0460000, 0xf0464000, 0xf0464008, 0xf0464064, 0xf0464068, 0xf046406c, 0xf0469000, 0xf0469210, 0xf0469300
        // USB control block 0xf0b00200-0xf0b002fc (DTB usb-phy@f0b00200, size 0x100)
        .quad 0xf0b00200, 0xf0b00204, 0xf0b00208, 0xf0b0020c, 0xf0b00210, 0xf0b00214, 0xf0b00218, 0xf0b0021c
        .quad 0xf0b00220, 0xf0b00224, 0xf0b00228, 0xf0b0022c, 0xf0b00230, 0xf0b00234, 0xf0b00238, 0xf0b0023c
        .quad 0xf0b00240, 0xf0b00244, 0xf0b00248, 0xf0b0024c, 0xf0b00250, 0xf0b00254, 0xf0b00258, 0xf0b0025c
        .quad 0xf0b00260, 0xf0b00264, 0xf0b00268, 0xf0b0026c, 0xf0b00270, 0xf0b00274, 0xf0b00278, 0xf0b0027c
        .quad 0xf0b00280, 0xf0b00284, 0xf0b00288, 0xf0b0028c, 0xf0b00290, 0xf0b00294, 0xf0b00298, 0xf0b0029c
        .quad 0xf0b002a0, 0xf0b002a4, 0xf0b002a8, 0xf0b002ac, 0xf0b002b0, 0xf0b002b4, 0xf0b002b8, 0xf0b002bc
        .quad 0xf0b002c0, 0xf0b002c4, 0xf0b002c8, 0xf0b002cc, 0xf0b002d0, 0xf0b002d4, 0xf0b002d8, 0xf0b002dc
        .quad 0xf0b002e0, 0xf0b002e4, 0xf0b002e8, 0xf0b002ec, 0xf0b002f0, 0xf0b002f4, 0xf0b002f8, 0xf0b002fc
        .quad 0

s_hdr:  .asciz "\r\npcie/usbctrl probe (EL2, aborts caught)\r\n"
s_sync: .asciz "SYNC "
s_serr: .asciz "SERR "
s_hang: .asciz "\r\nUNEXPECTED EXCEPTION esr/elr: "
s_crlf: .asciz "\r\n"
s_done: .asciz "probe done\r\n"
