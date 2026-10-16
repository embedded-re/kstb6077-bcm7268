// AArch64 probe for EL3 and EL2 (MMU off): read-only dump of the security-
// related blocks, the same binary at both levels so the outputs diff.
//   EL3: boot -64 -el3 -raw -tftp -addr=0x03000000 <PC>:el3_dump_probe.bin
//   EL2: boot -64 -raw -tftp -addr=0x03000000 <PC>:el3_dump_probe.bin
// One abort-safe 32-bit read per word. Output per range: "range start end",
// then one line per 32 bytes, "addr w0 .. w7", an aborted word as xxxxxxxx,
// lines where all 8 words read 0 skipped; then "aborted words n".
// Exceptions as in el3_read_probe.s. Reads only. Ends in wfe; arm the watchdog.
        .global _start
        .equ    LINK, 0x03000000
_start:
        msr     DAIFSet, #0xf
        mov     x0, #LINK
        sub     sp, x0, #0x1000                // stack below the program
        mov     x20, #0xc000
        movk    x20, #0xf040, lsl #16          // UART0
        mrs     x27, CurrentEL
        ubfx    x27, x27, #2, #2               // x27 = EL
        cmp     x27, #3
        b.ne    1f
        mrs     x26, VBAR_EL3                  // entry values, printed later
        mrs     x25, SCR_EL3
        adr     x0, vectors3
        msr     VBAR_EL3, x0
        orr     x0, x25, #8                    // SCR_EL3.EA
        msr     SCR_EL3, x0
        b       2f
1:      mrs     x26, VBAR_EL2
        mrs     x25, HCR_EL2
        adr     x0, vectors2
        msr     VBAR_EL2, x0
        orr     x0, x25, #(1 << 5)             // HCR_EL2.AMO
        msr     HCR_EL2, x0
2:      isb
        adr     x0, s_hdr;  bl puts
        mov     x0, x27;    bl puthex8
        adr     x0, s_crlf; bl puts
        adr     x0, s_vbar; bl puts
        mov     x0, x26;    bl puthex16
        adr     x0, s_crlf; bl puts
        adr     x0, s_ctl;  bl puts
        mov     x0, x25;    bl puthex16
        adr     x0, s_crlf; bl puts

// ---- ranges: list of (start, end)
        adr     x21, list
1:      ldp     x26, x27, [x21], #16
        cbz     x26, 9f
        adr     x0, s_rng;  bl puts
        mov     x0, x26;    bl puthex8
        mov     x0, x27;    bl puthex8
        adr     x0, s_crlf; bl puts
        mov     x24, #0                        // aborted words in this range
row:    mov     x9, #0                         // word index
        mov     x10, #0                        // "anything non-zero or aborted"
        adr     x11, line                      // 8 values + 8 flags
2:      add     x22, x26, x9, lsl #2
        mov     x19, #0
        ldr     w23, [x22]                     // THE read (sync handler skips it)
        dsb     sy
        isb
        msr     DAIFClr, #4
        isb
        nop
        msr     DAIFSet, #4
        str     w23, [x11, x9, lsl #2]
        add     x12, x11, #32
        strb    w19, [x12, x9]
        orr     x10, x10, x23
        orr     x10, x10, x19
        cbz     x19, 3f
        add     x24, x24, #1
3:      add     x9, x9, #1
        cmp     x9, #8
        b.lo    2b
        cbz     x10, 6f                        // all zero: skip the line
        mov     x0, x26;    bl puthex8
        mov     x13, #0
4:      adr     x11, line
        add     x12, x11, #32
        ldrb    w0, [x12, x13]
        cbz     w0, 5f
        adr     x0, s_xx;   bl puts
        b       7f
5:      ldr     w0, [x11, x13, lsl #2]
        bl      puthex8
7:      add     x13, x13, #1
        cmp     x13, #8
        b.lo    4b
        adr     x0, s_crlf; bl puts
6:      add     x26, x26, #32
        cmp     x26, x27
        b.lo    row
        adr     x0, s_abt;  bl puts
        mov     x0, x24;    bl puthex8
        adr     x0, s_crlf; bl puts
        b       1b
9:      adr     x0, s_done; bl puts
8:      wfe
        b       8b

// ---- exceptions: sync -> skip the instruction; SError -> just note it
        .macro  VECTORS el
        .balign 2048
vectors\el:
        .rept 4                                // current EL with SP_EL0 (unused)
        b       hang\el
        .balign 128
        .endr
        b       sync\el                        // 0x200 current EL, SP_ELx: synchronous
        .balign 128
        b       hang\el                        // 0x280 IRQ
        .balign 128
        b       hang\el                        // 0x300 FIQ
        .balign 128
        b       serr\el                        // 0x380 SError
        .balign 128
        .rept 8                                // lower ELs (unused)
        b       hang\el
        .balign 128
        .endr
sync\el:
        mrs     x28, ESR_EL\el
        mov     x19, #1
        mrs     x15, ELR_EL\el
        add     x15, x15, #4                   // skip the faulting instruction
        msr     ELR_EL\el, x15
        mov     x23, #0
        eret
serr\el:
        mrs     x28, ESR_EL\el
        mov     x19, #2
        eret
hang\el:
        adr     x0, s_hang; bl puts
        mrs     x0, ESR_EL\el; bl puthex8
        mrs     x0, ELR_EL\el; bl puthex16
1:      wfe
        b       1b
        .endm
        VECTORS 3
        VECTORS 2

puts:   ldrb    w1, [x0], #1                   // clobbers x0-x2
        cbz     w1, 9f
8:      ldr     w2, [x20, #0x14]
        tbz     w2, #5, 8b
        str     w1, [x20]
        b       puts
9:      ret
puthex16:                                      // x0 (16 digits) + space. Clobbers x0-x4.
        mov     x3, x0
        mov     x4, #60
        b       1f
puthex8:                                       // w0 (8 digits) + space. Clobbers x0-x4.
        mov     w3, w0
        mov     x4, #28
1:      lsr     x1, x3, x4
        and     x1, x1, #0xf
        cmp     x1, #10
        add     x2, x1, #'0'
        add     x1, x1, #('a' - 10)
        csel    x1, x2, x1, lo
2:      ldr     w2, [x20, #0x14]
        tbz     w2, #5, 2b
        str     w1, [x20]
        subs    x4, x4, #4
        b.pl    1b
        mov     w1, #' '
3:      ldr     w2, [x20, #0x14]
        tbz     w2, #5, 3b
        str     w1, [x20]
        ret

        .balign 8
list:   .quad 0xffd01000, 0xffd02000           // GIC distributor (all of it)
        .quad 0xf032c800, 0xf0330000           // BSP (DTB bsp reg)
        .quad 0xf1100000, 0xf1108000           // memory controller: gen, arb, DDR, rest
        .quad 0xf1109000, 0xf1109500           // DTU config
        .quad 0xf1110000, 0xf1118000           // DTU map
        .quad 0, 0
line:   .space  40

s_hdr:  .asciz "\r\nEL3/EL2 dump probe (aborts caught)\r\nCurrentEL "
s_vbar: .asciz "VBAR_at_entry "
s_ctl:  .asciz "SCR_or_HCR_at_entry "
s_rng:  .asciz "range "
s_abt:  .asciz "aborted words "
s_xx:   .asciz "xxxxxxxx "
s_hang: .asciz "\r\nUNEXPECTED EXCEPTION esr/elr: "
s_crlf: .asciz "\r\n"
s_done: .asciz "dump done\r\n"
