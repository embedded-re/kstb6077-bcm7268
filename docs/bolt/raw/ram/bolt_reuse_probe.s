// AArch64 probe (go -64, EL2, MMU off): is BOLT's RAM free after go, and does
// PSCI (EL3 monitor at 0x06400000) still answer once it is overwritten?
//   1. smc PSCI_VERSION (0x84000000), print x0
//   2. write 0x06f00000-0x091fffff (BOLT: FSBL info, page table, code, heap,
//      stack, the DTB at 0x07613000) with address ^ key, then check it
//   3. smc PSCI_VERSION again, and PSCI_FEATURES(CPU_ON_64 = 0xc4000003)
// Ends in wfe; use --watchdog. A wrong SMC can hang: the watchdog recovers.
        .equ START, 0x06f00000
        .equ END,   0x09200000
        .global _start
_start:
        mov     x0, #0x01080000
        mov     sp, x0
        mov     x20, #0xc000
        movk    x20, #0xf040, lsl #16
        adr     x0, vectors
        msr     VBAR_EL2, x0
        adr     x0, s_hdr;  bl puts

        adr     x0, s_ver;  bl puts
        bl      psci_version
        bl      puthex16

        // overwrite BOLT
        ldr     x27, =0x5a5a5a5aa5a5a5a5
        ldr     x10, =START
        ldr     x11, =END
1:      eor     x2, x10, x27
        str     x2, [x10], #8
        cmp     x10, x11
        b.lo    1b
        ldr     x10, =START
        mov     x26, #0
2:      ldr     x2, [x10]
        eor     x3, x10, x27
        cmp     x2, x3
        cinc    x26, x26, ne
        add     x10, x10, #8
        cmp     x10, x11
        b.lo    2b
        adr     x0, s_ow;   bl puts
        mov     x0, x26;    bl puthex16

        adr     x0, s_ver;  bl puts
        bl      psci_version
        bl      puthex16
        adr     x0, s_feat; bl puts
        ldr     x0, =0x8400000a                // PSCI_FEATURES
        ldr     x1, =0xc4000003                // CPU_ON (SMC64)
        mov     x2, #0
        mov     x3, #0
        smc     #0
        bl      puthex16                       // >= 0: supported; -1: not supported
        adr     x0, s_done; bl puts
9:      wfe
        b       9b

psci_version:
        ldr     x0, =0x84000000
        mov     x1, #0
        mov     x2, #0
        mov     x3, #0
        smc     #0                             // -> EL3 (smm64); result in x0
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
// puthex16: x0 as 16 hex digits + CRLF. Clobbers x0-x4.
puthex16:
        mov     x3, x0
        mov     x4, #60
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
        adr     x0, s_crlf
        b       puts

        .ltorg
s_hdr:  .asciz "\r\nBOLT-RAM reuse + PSCI probe\r\n"
s_ver:  .asciz "PSCI_VERSION: "
s_ow:   .asciz "overwrote 0x06f00000-0x091fffff, mismatches: "
s_feat: .asciz "PSCI_FEATURES(CPU_ON_64): "
s_exc:  .asciz "\r\nEXCEPTION esr/elr/far:\r\n"
s_crlf: .asciz "\r\n"
s_done: .asciz "probe done\r\n"
