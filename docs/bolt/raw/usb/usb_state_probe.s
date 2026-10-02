// AArch64 probe (go -64, EL2, MMU off): print the USB controller registers
// that BOLT left behind, to compare with the same reads at the BOLT> prompt.
// Read-only. Every address is inside a range the stock DTB names
// (ehci_v2 0xa8, ohci_v2 0x58, xhci_v2 0x1000). Ends in wfe; use --watchdog.
        .global _start
_start:
        mov     x20, #0xc000
        movk    x20, #0xf040, lsl #16          // x20 = UART0
        adr     x0, s_hdr
        bl      puts
        adr     x21, regs                      // x21 = address list, 0-terminated
1:      ldr     w22, [x21], #4
        cbz     w22, 2f
        mov     x0, x22
        bl      puthex8                        // "f0b00310 "
        ldr     w0, [x22]                      // the read
        bl      puthex8
        adr     x0, s_crlf
        bl      puts
        b       1b
2:      adr     x0, s_done
        bl      puts
3:      wfe
        b       3b

// puts: x0 -> NUL-terminated string. Clobbers x0-x2.
puts:   ldrb    w1, [x0], #1
        cbz     w1, 9f
8:      ldr     w2, [x20, #0x14]
        tbz     w2, #5, 8b
        str     w1, [x20]
        b       puts
9:      ret

// puthex8: prints w0 as 8 hex digits + space. Clobbers x0-x4.
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

        .balign 4
regs:   .word 0xf0b00300, 0xf0b00310, 0xf0b00314, 0xf0b00350, 0xf0b00354   // EHCI0: caps, USBCMD, USBSTS, CONFIGFLAG, PORTSC
        .word 0xf0b00500, 0xf0b00510, 0xf0b00514, 0xf0b00550, 0xf0b00554   // EHCI1
        .word 0xf0b00400, 0xf0b00404, 0xf0b00448, 0xf0b00454               // OHCI0: rev, HcControl, RhDescA, RhPortStatus1
        .word 0xf0b00600, 0xf0b00604, 0xf0b00648, 0xf0b00654               // OHCI1
        .word 0xf0b01000, 0xf0b01020, 0xf0b01024, 0xf0b01420, 0xf0b01430   // xHCI: caps, USBCMD, USBSTS, PORTSC1, PORTSC2
        .word 0
s_hdr:  .asciz "\r\nUSB state after go -64:\r\n"
s_crlf: .asciz "\r\n"
s_done: .asciz "USB probe done\r\n"
