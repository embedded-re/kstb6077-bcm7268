// AArch64 probe (go -64, EL2): pattern test of the free RAM, non-cacheable.
// Pass 1 writes every 64-bit word with (address ^ 0x5a5a5a5aa5a5a5a5), pass 2
// checks it; passes 3-4 do the same with the inverted value. A word that holds
// its own address catches stuck bits and address-line faults.
// Ranges (writes ONLY these): 0x00100000-0x00ffffff, 0x01200000-0x063fffff,
// 0x06500000-0x06efffff, 0x09200000-0x7d9fffff. Never written: MB 0, this
// program (0x01000000-0x011fffff), PSCI, BOLT 0x06f00000-0x091fffff, display
// 0x7da00000+, BL31/SRR.
// Prints the time of each pass, the error count, and the first 8 errors.
        .equ L1,    0x01100000
        .equ L2A,   0x01101000
        .equ L2B,   0x01102000
        .equ NC,    0x709
        .equ WB,    0x705
        .global _start
_start:
        mov     x0, #0x01080000
        mov     sp, x0
        mov     x20, #0xc000
        movk    x20, #0xf040, lsl #16
        adr     x0, vectors
        msr     VBAR_EL2, x0
        bl      mmu_on
        adr     x0, s_hdr;  bl puts
        ldr     x27, =0x5a5a5a5aa5a5a5a5       // pattern key
        mov     x26, #0                        // error count
        mov     x25, #0                        // errors printed

        mov     x24, #0                        // pass 0: write key
        bl      all
        mov     x24, #1                        // pass 1: check key
        bl      all
        mvn     x27, x27                       // inverted
        mov     x24, #0
        bl      all
        mov     x24, #1
        bl      all

        adr     x0, s_err;  bl puts
        mov     x0, x26;    bl puthex16
        adr     x0, s_crlf; bl puts
        adr     x0, s_done; bl puts
9:      wfe
        b       9b

// all: run the pass (x24: 0 = write, 1 = check) over every range, print its time.
all:    stp     x29, x30, [sp, #-16]!
        mrs     x23, CNTPCT_EL0
        adr     x22, ranges
1:      ldp     x10, x11, [x22], #16           // start, end (exclusive)
        cbz     x11, 2f
        bl      range
        b       1b
2:      mrs     x0, CNTPCT_EL0
        sub     x0, x0, x23
        mov     x1, #27000
        udiv    x21, x0, x1
        adr     x0, s_pass; bl puts
        mov     x0, x24;    bl puthex1
        mov     x0, x21;    bl puthex8         // ms, hex
        adr     x0, s_crlf; bl puts
        ldp     x29, x30, [sp], #16
        ret

// range: x10..x11. Write: [a] = a ^ key. Check: compare, record errors.
range:  stp     x29, x30, [sp, #-16]!
        cbnz    x24, 3f
1:      eor     x2, x10, x27
        add     x3, x10, #8
        eor     x3, x3, x27
        stp     x2, x3, [x10], #16
        cmp     x10, x11
        b.lo    1b
        b       9f
3:      ldp     x2, x3, [x10]
        eor     x4, x10, x27
        add     x5, x10, #8
        eor     x5, x5, x27
        cmp     x2, x4
        b.ne    5f
4:      cmp     x3, x5
        b.ne    6f
7:      add     x10, x10, #16
        cmp     x10, x11
        b.lo    3b
        b       9f
5:      mov     x6, x10;  mov x7, x2;  mov x8, x4;  bl error;  b 4b
6:      add     x6, x10, #8;  mov x7, x3;  mov x8, x5;  bl error;  b 7b
9:      ldp     x29, x30, [sp], #16
        ret

// error: x6 = address, x7 = got, x8 = expected. Prints the first 8.
error:  add     x26, x26, #1
        cmp     x25, #8
        b.hs    1f
        add     x25, x25, #1
        stp     x29, x30, [sp, #-16]!
        stp     x2, x3, [sp, #-16]!
        stp     x4, x5, [sp, #-16]!
        adr     x0, s_bad;  bl puts
        mov     x0, x6;     bl puthex16
        mov     x0, x7;     bl puthex16
        mov     x0, x8;     bl puthex16
        adr     x0, s_crlf; bl puts
        ldp     x4, x5, [sp], #16
        ldp     x2, x3, [sp], #16
        ldp     x29, x30, [sp], #16
1:      ret

mmu_on:
        ldr     x0, =L1
        ldr     x1, =(L2A | 3)
        str     x1, [x0, #0]
        ldr     x1, =(L2B | 3)
        str     x1, [x0, #8]
        str     xzr, [x0, #16]
        ldr     x1, =(0xc0000000 | 0x0040000000000405 | (3 << 2))
        str     x1, [x0, #24]
        ldr     x0, =L2A
        mov     x2, #0
1:      lsl     x1, x2, #21
        ldr     x3, =NC
        cmp     x2, #8
        b.ne    2f
        ldr     x3, =WB
2:      orr     x1, x1, x3
        str     x1, [x0, x2, lsl #3]
        add     x2, x2, #1
        cmp     x2, #512
        b.lo    1b
        ldr     x0, =L2B
        mov     x2, #0
3:      lsl     x1, x2, #21
        orr     x1, x1, #0x40000000
        ldr     x3, =NC
        orr     x1, x1, x3
        cmp     x2, #496
        csel    x1, xzr, x1, hs
        str     x1, [x0, x2, lsl #3]
        add     x2, x2, #1
        cmp     x2, #512
        b.lo    3b
        ldr     x0, =0x0444ff00
        msr     MAIR_EL2, x0
        ldr     x0, =0x80800020
        msr     TCR_EL2, x0
        ldr     x0, =L1
        msr     TTBR0_EL2, x0
        dsb     sy
        tlbi    alle2
        dsb     sy
        ic      iallu
        dsb     sy
        isb
        mrs     x0, SCTLR_EL2
        orr     x0, x0, #1
        orr     x0, x0, #(1 << 2)
        orr     x0, x0, #(1 << 12)
        msr     SCTLR_EL2, x0
        isb
        ret

        .balign 2048
vectors:
        .rept 16
        b       exc
        .balign 128
        .endr
exc:    adr     x0, s_exc;  bl puts
        mrs     x0, ESR_EL2; bl puthex16
        mrs     x0, ELR_EL2; bl puthex16
        mrs     x0, FAR_EL2; bl puthex16
1:      wfe
        b       1b

puts:   ldrb    w1, [x0], #1
        cbz     w1, 9f
8:      ldr     w2, [x20, #0x14]
        tbz     w2, #5, 8b
        str     w1, [x20]
        b       puts
9:      ret
puthex1:  mov x4, #0;  b puthexn
puthex8:  mov x4, #28; b puthexn
puthex16: mov x4, #60
puthexn:
        mov     x3, x0
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

        .ltorg
        .balign 16
ranges: .quad 0x00100000, 0x01000000
        .quad 0x01200000, 0x06400000
        .quad 0x06500000, 0x06f00000
        .quad 0x09200000, 0x7da00000
        .quad 0, 0
s_hdr:  .asciz "\r\nRAM test (EL2, non-cacheable, free RAM only)\r\n"
s_pass: .asciz "pass write/check="
s_err:  .asciz "errors: "
s_bad:  .asciz "BAD addr/got/expected: "
s_exc:  .asciz "\r\nEXCEPTION esr/elr/far:\r\n"
s_crlf: .asciz "\r\n"
s_done: .asciz "RAM test done\r\n"
