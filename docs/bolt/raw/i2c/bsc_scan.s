// AArch64 probe (go -64, EL2, MMU off), address-map item 2.7: I2C bus scan.
// For each BSC controller (bases from nexus.ko BI2C_OpenChannel) and each
// 7-bit address 0x08-0x77: a 1-byte read; print the addresses that ACK.
// Register use (Linux i2c-brcmstb names, not tested before this probe):
//   +0x00 CHIP_ADDRESS = addr<<1 | 1 (read)
//   +0x04 DATA_IN0      (received byte, bits 7:0)
//   +0x24 CNT_REG       = 1 byte
//   +0x28 CTL_REG       = 0x91: DIV_CLK, SCL_SEL 1, DTF 1 = read only, no INT_EN
//   +0x2c IIC_ENABLE    = 1 start; bit 1 = done, bit 2 = no ACK
//   +0x50 CTLHI_REG     = 0 (1-byte data registers)
// Writes only these registers of the five BSC blocks. Ends in wfe.
        .global _start
_start:
        mov     x0, #0x01080000
        mov     sp, x0
        mov     x20, #0xc000
        movk    x20, #0xf040, lsl #16          // UART0
        adr     x0, vectors
        msr     VBAR_EL2, x0
        isb
        adr     x27, bases
        mov     x26, #0                        // channel
1:      ldr     w21, [x27, x26, lsl #2]
        adr     x0, s_ch;   bl puts
        mov     x0, x26;    bl puthex8
        mov     x0, x21;    bl puthex8
        adr     x0, s_crlf; bl puts
        mov     w0, #0
        str     w0, [x21, #0x2c]               // IIC_ENABLE off
        str     w0, [x21, #0x50]               // CTLHI
        mov     w0, #0x91
        str     w0, [x21, #0x28]               // CTL
        mov     x22, #0x08                     // address
        mov     x23, #0                        // no-ACK count
        mov     x24, #0                        // timeout count
2:      mov     w0, #0
        str     w0, [x21, #0x2c]
        lsl     w0, w22, #1
        orr     w0, w0, #1
        str     w0, [x21, #0x00]
        mov     w0, #1
        str     w0, [x21, #0x24]
        dsb     sy
        str     w0, [x21, #0x2c]               // start
        dsb     sy
        mrs     x9, CNTPCT_EL0
        mov     x10, #0x7c00
        movk    x10, #0x4, lsl #16             // 0x47c00 = 294,912 ticks ~ 11 ms
        add     x10, x9, x10
3:      ldr     w11, [x21, #0x2c]
        tbnz    w11, #1, 4f
        mrs     x9, CNTPCT_EL0
        cmp     x9, x10
        b.lo    3b
        add     x24, x24, #1                   // timeout
        b       5f
4:      tbz     w11, #2, 6f
        add     x23, x23, #1                   // no ACK
        b       5f
6:      ldr     w12, [x21, #0x04]              // ACK: print addr<<16 | iic_en<<8 | data
        and     w12, w12, #0xff
        lsl     w0, w22, #16
        and     w11, w11, #0xff
        orr     w0, w0, w11, lsl #8
        orr     w0, w0, w12
        bl      puthex8
5:      mov     w0, #0
        str     w0, [x21, #0x2c]
        dsb     sy
        add     x22, x22, #1
        cmp     x22, #0x78
        b.ne    2b
        adr     x0, s_cnt;  bl puts
        mov     x0, x23;    bl puthex8
        mov     x0, x24;    bl puthex8
        adr     x0, s_crlf; bl puts
        add     x26, x26, #1
        cmp     x26, #5
        b.ne    1b
        adr     x0, s_done; bl puts
8:      wfe
        b       8b

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

bases:  .word   0xf040a300, 0xf0419a80, 0xf0419b00, 0xf0419b80, 0xf040a400
s_ch:   .asciz "\r\nchannel, base; ACKs as addr<<16|iic_en<<8|data:\r\n"
s_cnt:  .asciz "\r\nno-ACK, timeout counts: "
s_hang: .asciz "\r\nUNEXPECTED EXCEPTION esr/elr: "
s_crlf: .asciz "\r\n"
s_done: .asciz "scan done\r\n"
