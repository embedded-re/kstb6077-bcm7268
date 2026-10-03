// AArch64 probe (go -64, EL2, MMU off): enable IR channel kbd1
// (0xf0419900) as an NEC CIR receiver the way Nexus does it
// (BKIR_OpenChannel + BKIR_P_EnableInt + BKIR_EnableIrDevice(16 = CirNec)),
// then poll STATUS for 90 s and print every received code. Restores the
// channel at the end. Ends in wfe; use --watchdog.
//
// Registers (offsets from the channel base, names from the Nexus code):
//   +0x00 STATUS  bit0 data ready (cleared by writing it back with bit0=0)
//   +0x08 FILTER1 +0x0c DATA1 +0x10 DATA0
//   +0x14 CMD     bit4 CIR enable, bit5 interrupt enable
//   +0x18 CIR_ADDR (param index) +0x1c CIR_DATA (param value)
        .equ BASE, 0xf0419900
        .equ L2,   0xf0419c00
        .equ WINDOW, 90 * 27000000            // CNTPCT ticks (27 MHz)
        .global _start
_start:
        mov     x0, #0x01080000
        mov     sp, x0
        mov     x20, #0xc000
        movk    x20, #0xf040, lsl #16
        adr     x0, vectors
        msr     VBAR_EL2, x0
        isb
        ldr     x24, =BASE
        ldr     x27, =L2
        adr     x0, s_hdr;  bl puts

        // 1. save the CIR params: CIR_ADDR = i, read CIR_DATA
        adr     x0, s_save; bl puts
        adr     x21, saved
        mov     x9, #0
1:      str     w9, [x24, #0x18]
        ldr     w0, [x24, #0x1c]
        str     w0, [x21, x9, lsl #2]
        bl      puthex8
        add     x9, x9, #1
        cmp     x9, #28
        b.lo    1b
        adr     x0, s_crlf; bl puts

        // 2. enable as Nexus does
        str     wzr, [x24, #0x00]              // STATUS = 0
        ldr     w0, [x24, #0x14]
        orr     w0, w0, #0x20                  // CMD |= interrupt enable
        str     w0, [x24, #0x14]
        ldr     w0, [x24, #0x14]
        orr     w0, w0, #0x10                  // CMD |= CIR enable
        str     w0, [x24, #0x14]
        str     wzr, [x24, #0x08]              // FILTER1 = 0
        adr     x21, necp                      // NEC params 0..27 (24 skipped)
2:      ldr     w9, [x21], #4
        cmn     w9, #1
        b.eq    3f
        ldr     w10, [x21], #4
        str     w9, [x24, #0x18]
        str     w10, [x24, #0x1c]
        b       2b
3:      adr     x0, s_cmd;  bl puts
        ldr     w0, [x24, #0x14]; bl puthex8
        adr     x0, s_crlf; bl puts
        adr     x0, s_go;   bl puts

        // 3. poll for 90 s
        mrs     x26, CNTPCT_EL0
        ldr     x9, =WINDOW
        add     x26, x26, x9
        mov     x28, #0                        // codes received
4:      mrs     x9, CNTPCT_EL0
        cmp     x9, x26
        b.hs    6f
        ldr     w22, [x24, #0x00]
        tbz     w22, #0, 4b
        ldr     w0, [x27];        bl puthex8   // L2 STATUS (before clear)
        mov     w0, w22;          bl puthex8   // STATUS
        ldr     w0, [x24, #0x0c]; bl puthex8   // DATA1
        ldr     w0, [x24, #0x10]; bl puthex8   // DATA0
        adr     x0, s_crlf; bl puts
        bic     w22, w22, #1
        str     w22, [x24, #0x00]              // clear data ready
        add     x28, x28, #1
        b       4b

        // 4. restore: CMD = 7, params back, CIR_ADDR = 0, STATUS = 0
6:      adr     x0, s_cnt;  bl puts
        mov     x0, x28;    bl puthex8
        adr     x0, s_crlf; bl puts
        mov     w0, #7
        str     w0, [x24, #0x14]
        adr     x21, saved
        mov     x9, #0
7:      ldr     w10, [x21, x9, lsl #2]
        str     w9, [x24, #0x18]
        str     w10, [x24, #0x1c]
        add     x9, x9, #1
        cmp     x9, #28
        b.lo    7b
        str     wzr, [x24, #0x18]
        str     wzr, [x24, #0x00]
        adr     x0, s_rest; bl puts
        ldr     w0, [x24, #0x00]; bl puthex8
        ldr     w0, [x24, #0x14]; bl puthex8
        ldr     w0, [x24, #0x1c]; bl puthex8
        ldr     w0, [x27];        bl puthex8
        adr     x0, s_crlf; bl puts
        adr     x0, s_done; bl puts
9:      wfe
        b       9b

        .balign 2048
vectors:
        .rept 16
        b       hang
        .balign 128
        .endr
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

        .ltorg
        .balign 4
// (index, value) pairs computed from nexus.ko necParam by BKIR_P_ConfigCir
necp:   .word  0, 0x0ff,   1, 0x000,   2, 0x012,   3, 0x384
        .word  4, 0x384,   5, 0x1c2,   6, 0x0e1,   7, 0x000
        .word  8, 0x000,   9, 0x000,  10, 0x000,  11, 0x80f
        .word 12, 0xc32,  13, 0x000,  14, 0x071,  15, 0x071
        .word 16, 0x10d,  17, 0x032,  18, 0x830,  19, 0x031
        .word 20, 0x016,  21, 0x006,  22, 0x258,  23, 0x118
        .word 25, 0x000,  26, 0x000,  27, 0x000
        .word 0xffffffff
saved:  .space  28 * 4
s_hdr:  .asciz "\r\nIR NEC probe kbd1 0xf0419900\r\n"
s_save: .asciz "saved params 0-27: "
s_cmd:  .asciz "CMD after enable: "
s_go:   .asciz "PRESS REMOTE KEYS NOW (90 s). cols: L2-STATUS KBD-STATUS DATA1 DATA0\r\n"
s_cnt:  .asciz "codes received: "
s_rest: .asciz "restored STATUS CMD CIR_DATA L2: "
s_hang: .asciz "\r\nUNEXPECTED EXCEPTION esr/elr: "
s_crlf: .asciz "\r\n"
s_done: .asciz "probe done\r\n"
