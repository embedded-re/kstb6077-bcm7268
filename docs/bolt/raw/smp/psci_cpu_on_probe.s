// AArch64 probe (go -64, EL2, MMU off): start cores 1-3 with PSCI CPU_ON.
// For each core n (MPIDR affinity 0 = n):
//   AFFINITY_INFO (0xc4000004) before -> CPU_ON (0xc4000003, entry = sec_entry,
//   context = n) -> wait up to 100 ms for its mailbox -> print the mailbox ->
//   counter moving? -> AFFINITY_INFO after.
// Mailbox n at 0x01200000 + n*64: +0 magic 0xc0de000n (written last), +8 MPIDR,
// +16 CurrentEL, +24 SCTLR of its EL, +32 counter (incremented forever), +40 x0.
// n comes from MPIDR. The stub's first store (0x01200100 = x0, 0x01200108 =
// arrivals) shows whether it runs at all. At the end core 0 dumps the mailbox.
// Stacks: 0x01300000 + n*64 KB (top). PSCI codes: 0 ok, -2 invalid params,
// -4 already on, -5 on pending, -6 internal failure, -9 invalid address.
// AFFINITY_INFO: 0 on, 1 off, 2 on pending. Ends in wfe; use --watchdog.
        .equ MBOX, 0x01200000
        .global _start
_start:
        mov     x0, #0x01080000
        mov     sp, x0
        mov     x20, #0xc000
        movk    x20, #0xf040, lsl #16
        adr     x0, vectors
        msr     VBAR_EL2, x0
        adr     x0, s_hdr;  bl puts
        adr     x0, s_me;   bl puts
        mrs     x0, MPIDR_EL1; bl puthex16

        // clear the mailboxes
        ldr     x1, =MBOX
        mov     x2, #0
1:      str     xzr, [x1, x2]
        add     x2, x2, #8
        cmp     x2, #0x110
        b.lo    1b

        mov     x21, #1                        // core n
core:   adr     x0, s_core; bl puts
        mov     x0, x21;    bl puthex16
        adr     x0, s_aff;  bl puts
        mov     x1, x21
        bl      affinity
        bl      puthex16

        adr     x0, s_on;   bl puts
        ldr     x0, =0xc4000003                // CPU_ON (SMC64)
        mov     x1, x21                        // target MPIDR (aff0 = n)
        adr     x2, sec_entry                  // where the core starts
        mov     x3, x21                        // context id -> its x0
        smc     #0
        bl      puthex16

        // wait up to 100 ms for the magic
        ldr     x22, =MBOX
        add     x22, x22, x21, lsl #6
        mov     x0, #0xc0de0000
        orr     x23, x0, x21
        mrs     x24, CNTPCT_EL0
        ldr     x0, =2700000                   // 100 ms
        add     x24, x24, x0
2:      ldr     x0, [x22]
        cmp     x0, x23
        b.eq    3f
        mrs     x1, CNTPCT_EL0
        cmp     x1, x24
        b.lo    2b
        adr     x0, s_none; bl puts
        b       4f
3:      adr     x0, s_mb;   bl puts
        ldr     x0, [x22, #8];  bl puthex16    // MPIDR
        ldr     x0, [x22, #16]; bl puthex16    // CurrentEL
        ldr     x0, [x22, #24]; bl puthex16    // SCTLR
        adr     x0, s_cnt;  bl puts
        ldr     x25, [x22, #32]
        ldr     x0, =270000                    // 10 ms
        mrs     x1, CNTPCT_EL0
        add     x0, x0, x1
5:      mrs     x1, CNTPCT_EL0
        cmp     x1, x0
        b.lo    5b
        ldr     x0, [x22, #32]
        sub     x0, x0, x25
        bl      puthex16                       // counts in 10 ms
4:      adr     x0, s_aff;  bl puts
        mov     x1, x21
        bl      affinity
        bl      puthex16
        add     x21, x21, #1
        cmp     x21, #4
        b.lo    core

        adr     x0, s_dump; bl puts
        ldr     x22, =MBOX
        mov     x21, #0
6:      ldr     x0, [x22, x21, lsl #3]
        bl      puthex16
        add     x21, x21, #1
        cmp     x21, #(0x110 / 8)
        b.lo    6b
        adr     x0, s_done; bl puts
9:      wfe
        b       9b

affinity:                                      // x1 = MPIDR -> x0 = state
        ldr     x0, =0xc4000004
        mov     x2, #0                         // lowest affinity level: this core
        mov     x3, #0
        smc     #0
        ret

// ---- secondary cores start here (x0 = context = n), MMU off
sec_entry:
        ldr     x10, =(MBOX + 0x100)           // first: prove we run, keep x0 as received
        str     x0, [x10]                      // +0x100: x0 at entry (last core to arrive)
        ldr     x11, [x10, #8]
        add     x11, x11, #1
        str     x11, [x10, #8]                 // +0x108: number of arrivals
        mrs     x9, MPIDR_EL1
        and     x9, x9, #0xff                  // n = affinity 0, not the context id
        ldr     x10, =MBOX
        add     x10, x10, x9, lsl #6           // my mailbox
        ldr     x11, =0x01310000
        add     x11, x11, x9, lsl #16          // stack top 0x01300000 + (n+1)*64 KB
        mov     sp, x11
        mrs     x1, MPIDR_EL1
        str     x1, [x10, #8]
        mrs     x2, CurrentEL
        str     x2, [x10, #16]
        cmp     x2, #8
        b.ne    1f
        mrs     x3, SCTLR_EL2
        b       2f
1:      mrs     x3, SCTLR_EL1
2:      str     x3, [x10, #24]
        mov     x4, #0xc0de0000
        orr     x4, x4, x9
        dsb     sy
        str     x4, [x10]                      // magic last: "I'm alive"
        dsb     sy
        str     x0, [x10, #40]                 // +40: x0 as received
        mov     x5, #0
3:      add     x5, x5, #1
        str     x5, [x10, #32]
        b       3b

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
s_hdr:  .asciz "\r\nPSCI CPU_ON probe\r\n"
s_me:   .asciz "core 0 MPIDR: "
s_core: .asciz "--- core "
s_aff:  .asciz "AFFINITY_INFO: "
s_on:   .asciz "CPU_ON returned: "
s_none: .asciz "no reply within 100 ms\r\n"
s_mb:   .asciz "alive. MPIDR / CurrentEL / SCTLR:\r\n"
s_cnt:  .asciz "counter steps in 10 ms: "
s_exc:  .asciz "\r\nEXCEPTION esr/elr/far:\r\n"
s_crlf: .asciz "\r\n"
s_dump: .asciz "mailbox 0x01200000-0x0120010f (8-byte words):\r\n"
s_done: .asciz "probe done\r\n"
