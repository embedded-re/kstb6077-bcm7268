// AArch64 probe (go -64, EL2, MMU off): bring OHCI1 (0xf0b00600, the USB-A
// port's full/low-speed controller) from BOLT's reset state to Operational,
// power its port, wait for a device, reset the port. No transfers yet.
// Registers: OHCI 1.0a spec; all inside the DTB range ohci_v2@f0b00600 (0x58).
// HCCA (256 bytes, 256-aligned) at 0x11000000. Ends in wfe; use --watchdog.
        .equ OHCI,  0xf0b00600
        .equ HCCA,  0x11000000
        .global _start
_start:
        mov     x20, #0xc000
        movk    x20, #0xf040, lsl #16          // x20 = UART0
        mov     x19, #0x0600
        movk    x19, #0xf0b0, lsl #16          // x19 = OHCI1
        adr     x0, s_hdr;  bl puts
        bl      dump

        // 1. Host controller reset (HcCommandStatus.HCR), wait for it to clear.
        mov     w0, #1
        str     w0, [x19, #0x08]
        mov     x0, #1000; bl delay_us          // spec: done within 10 us
        adr     x0, s_cs;   bl puts; ldr w0, [x19, #0x08]; bl puthex8; bl crlf

        // 2. HCCA: zero it, give its address to the controller.
        mov     x1, #HCCA
        mov     x2, #0
1:      str     xzr, [x1, x2]
        add     x2, x2, #8
        cmp     x2, #256
        b.lo    1b
        str     w1, [x19, #0x18]               // HcHCCA

        // 3. Frame timing (values from the spec), then Operational (HCFS = 10b).
        mov     w0, #0x2edf                    // FI = 11999 (1 ms at 12 MHz)
        movk    w0, #0x2778, lsl #16           // FSMPS = 0x2778
        str     w0, [x19, #0x34]               // HcFmInterval
        mov     w0, #0x2a2f                    // 90% of FI
        str     w0, [x19, #0x40]               // HcPeriodicStart
        mov     w0, #0x80
        str     w0, [x19, #0x04]               // HcControl: Operational, no lists
        mov     x0, #5000; bl delay_us
        adr     x0, s_op;   bl puts; ldr w0, [x19, #0x04]; bl puthex8
        ldr     w0, [x19, #0x3c]; bl puthex8   // HcFmNumber, twice: should count
        mov     x0, #10000; bl delay_us
        ldr     w0, [x19, #0x3c]; bl puthex8; bl crlf

        // 4. Port power: global (HcRhStatus.LPSC) and per-port (SetPortPower).
        mov     w0, #0x10000
        str     w0, [x19, #0x50]
        mov     w0, #0x100
        str     w0, [x19, #0x54]
        mov     x0, #20000; bl delay_us        // POTPGT = 2 -> 4 ms; wait more

        // 5. Wait up to 1 s for a connect (CCS, bit 0).
        mov     x21, #100
2:      ldr     w0, [x19, #0x54]
        tbnz    w0, #0, 3f
        mov     x0, #10000; bl delay_us
        subs    x21, x21, #1
        b.ne    2b
3:      adr     x0, s_conn; bl puts; ldr w0, [x19, #0x54]; bl puthex8; bl crlf
        ldr     w0, [x19, #0x54]
        tbz     w0, #0, done

        // 6. Port reset (SetPortReset, bit 4), wait for PRSC (bit 20).
        ldr     x0, =100000; bl delay_us      // USB: 100 ms debounce after connect
        mov     w0, #0x10
        str     w0, [x19, #0x54]
        mov     x21, #100
4:      ldr     w0, [x19, #0x54]
        tbnz    w0, #20, 5f
        mov     x0, #1000; bl delay_us
        subs    x21, x21, #1
        b.ne    4b
5:      adr     x0, s_rst;  bl puts; ldr w0, [x19, #0x54]; bl puthex8; bl crlf
        mov     w0, #0x001f0000                // clear the change bits
        str     w0, [x19, #0x54]
done:
        bl      dump
        adr     x0, s_done; bl puts
9:      wfe
        b       9b

// dump: HcControl, HcCommandStatus, HcRhDescriptorA, HcRhStatus, HcRhPortStatus1.
dump:   mov     x23, x30
        adr     x0, s_dump; bl puts
        ldr     w0, [x19, #0x04]; bl puthex8
        ldr     w0, [x19, #0x08]; bl puthex8
        ldr     w0, [x19, #0x48]; bl puthex8
        ldr     w0, [x19, #0x50]; bl puthex8
        ldr     w0, [x19, #0x54]; bl puthex8
        bl      crlf
        ret     x23

// delay_us: x0 microseconds, on the 27 MHz generic timer. Clobbers x0-x2.
delay_us:
        mov     x1, #27
        mul     x0, x0, x1
        mrs     x1, CNTPCT_EL0
        add     x0, x0, x1
1:      mrs     x1, CNTPCT_EL0
        cmp     x1, x0
        b.lo    1b
        ret

crlf:   adr     x0, s_crlf
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

s_hdr:  .asciz "\r\nOHCI1 port probe\r\n"
s_dump: .asciz "ctrl cmdst rhdA rhst port1: "
s_cs:   .asciz "after HCR, cmdstatus: "
s_op:   .asciz "control, fmnumber x2: "
s_conn: .asciz "port after power/wait: "
s_rst:  .asciz "port after reset: "
s_crlf: .asciz "\r\n"
s_done: .asciz "OHCI probe done\r\n"
