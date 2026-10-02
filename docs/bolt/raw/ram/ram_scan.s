// AArch64 probe (go -64, EL2): read-only RAM scan.
// Pass A over all 2 GB in 1 MB blocks, wait 10 s, pass B. For each block:
// a rotate-xor hash of every 64-bit word, and the number of zero words.
// Then one line per block: "B mmm zzzzz hhhhhhhhhhhhhhhh" (A) and later the same with "C" (pass B)
// mmm = block number (MB), zzzzz = zero words (of 0x20000), h = hash. A block whose
// hash differs between A and C was written by something other than this program.
// Also prints the first 4 words of each MB below 16 MB.
// Skipped (never read): 0x064xxxxx (PSCI) and 0x7df00000-0x7fffffff (BL31, SRR).
// MMU map: RAM Normal NON-cacheable (every read goes to DRAM), except the 2 MB
// block holding this program (cached); 0x7e000000-0x7fffffff unmapped;
// 0xc0000000+ Device. Exceptions print ESR/ELR/FAR. Ends in wfe; use --watchdog.
        .equ L1,    0x01100000                 // page tables (in the program's own 2 MB block)
        .equ L2A,   0x01101000                 // 0x00000000-0x3fffffff
        .equ L2B,   0x01102000                 // 0x40000000-0x7fffffff
        .equ RES,   0x01110000                 // results: 2048 x {hash A, zeros A, hash B, zeros B}
        .equ NC,    0x709                      // block, attr 2 (Normal NC), inner shareable, AF
        .equ WB,    0x705                      // block, attr 1 (Normal WB)
        .global _start
_start:
        mov     x0, #0x01080000
        mov     sp, x0
        mov     x20, #0xc000
        movk    x20, #0xf040, lsl #16          // UART0
        adr     x0, vectors
        msr     VBAR_EL2, x0
        bl      mmu_on
        adr     x0, s_hdr;  bl puts

        // first 4 words of each MB below 16 MB (MB 0 starts at 0x1000: 0x0-0xfff is "nodma")
        mov     x21, #0
1:      adr     x0, s_low;  bl puts
        lsl     x22, x21, #20
        cbnz    x21, 2f
        mov     x22, #0x1000
2:      mov     x0, x22;    bl puthex8
        ldr     w0, [x22, #0];  bl puthex8
        ldr     w0, [x22, #4];  bl puthex8
        ldr     w0, [x22, #8];  bl puthex8
        ldr     w0, [x22, #12]; bl puthex8
        adr     x0, s_crlf; bl puts
        add     x21, x21, #1
        cmp     x21, #16
        b.lo    1b

        mrs     x23, CNTPCT_EL0
        mov     x24, #0                        // pass A -> RES + 0
        bl      pass
        mrs     x0, CNTPCT_EL0
        sub     x0, x0, x23
        mov     x1, #27000
        udiv    x0, x0, x1
        adr     x1, s_pass; mov x25, x0; mov x0, x1; bl puts
        mov     x0, x25;    bl puthex8         // pass time in ms (hex)
        adr     x0, s_crlf; bl puts

        ldr     x0, =270000000                 // 10 s
        mrs     x1, CNTPCT_EL0
        add     x0, x0, x1
3:      mrs     x1, CNTPCT_EL0
        cmp     x1, x0
        b.lo    3b

        mov     x24, #16                       // pass B -> RES + 16
        bl      pass

        // print: one line per block, "A" and "C" values
        mov     x21, #0
4:      ldr     x26, =RES
        add     x26, x26, x21, lsl #5
        adr     x0, s_a;    bl puts
        mov     x0, x21;    bl puthex3
        ldr     x0, [x26, #8];  bl puthex5
        ldr     x0, [x26, #0];  bl puthex16
        ldr     x1, [x26, #0]
        ldr     x2, [x26, #16]
        cmp     x1, x2
        b.eq    5f
        adr     x0, s_chg;  bl puts
        ldr     x0, [x26, #24]; bl puthex5
        ldr     x0, [x26, #16]; bl puthex16
5:      adr     x0, s_crlf; bl puts
        add     x21, x21, #1
        cmp     x21, #2048
        b.lo    4b
        adr     x0, s_done; bl puts
9:      wfe
        b       9b

// pass: hash every block into RES + block*32 + x24. Clobbers x0-x15.
pass:
        mov     x10, #0                        // block number
1:      ldr     x11, =RES
        add     x11, x11, x10, lsl #5
        add     x11, x11, x24
        mov     x12, #0                        // hash
        mov     x13, #0                        // zero words
        cmp     x10, #0x064                    // PSCI MB: skip
        b.eq    8f
        cmp     x10, #0x7df                    // BL31 + SRR: skip
        b.hs    8f
        lsl     x14, x10, #20                  // block start
        mov     x15, #0x8000                   // 0x8000 x 32 bytes = 1 MB
2:      ldp     x2, x3, [x14], #16
        ldp     x4, x5, [x14], #16
        eor     x12, x12, x2
        ror     x12, x12, #59
        eor     x12, x12, x3
        ror     x12, x12, #59
        eor     x12, x12, x4
        ror     x12, x12, #59
        eor     x12, x12, x5
        ror     x12, x12, #59
        cmp     x2, #0
        cinc    x13, x13, eq
        cmp     x3, #0
        cinc    x13, x13, eq
        cmp     x4, #0
        cinc    x13, x13, eq
        cmp     x5, #0
        cinc    x13, x13, eq
        subs    x15, x15, #1
        b.ne    2b
        b       9f
8:      mov     x12, #0x5ced                   // marker for "skipped"
        mov     x13, #0xfffff
9:      str     x12, [x11, #0]
        str     x13, [x11, #8]
        add     x10, x10, #1
        cmp     x10, #2048
        b.lo    1b
        ret

mmu_on:
        ldr     x0, =L1
        ldr     x1, =(L2A | 3)
        str     x1, [x0, #0]
        ldr     x1, =(L2B | 3)
        str     x1, [x0, #8]
        str     xzr, [x0, #16]
        ldr     x1, =(0xc0000000 | 0x0040000000000405 | (3 << 2))
        str     x1, [x0, #24]                  // Device-nGnRE, XN
        // L2A: 512 x 2 MB, NC, except block 8 (0x01000000: this program) WB
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
        // L2B: 512 x 2 MB from 0x40000000, NC; blocks 496-511 (0x7e000000+, SRR) invalid
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
        ldr     x0, =0x0444ff00                // 0 Dev-nGnRnE, 1 WB, 2 NC, 3 Dev-nGnRE
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

// puts: x0 -> string. Clobbers x0-x2.
puts:   ldrb    w1, [x0], #1
        cbz     w1, 9f
8:      ldr     w2, [x20, #0x14]
        tbz     w2, #5, 8b
        str     w1, [x20]
        b       puts
9:      ret
// puthexN: x0 as N hex digits + space. Clobbers x0-x4.
puthex3:  mov x4, #8;  b puthexn
puthex5:  mov x4, #16; b puthexn
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
s_hdr:  .asciz "\r\nRAM scan (EL2, RAM non-cacheable)\r\n"
s_low:  .asciz "L "
s_pass: .asciz "P pass A ms "
s_a:    .asciz "B "
s_chg:  .asciz "C "
s_exc:  .asciz "\r\nEXCEPTION esr/elr/far:\r\n"
s_crlf: .asciz "\r\n"
s_done: .asciz "RAM scan done\r\n"
