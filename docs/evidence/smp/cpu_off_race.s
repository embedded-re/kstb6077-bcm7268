// Loaded at 0x01000000 (kmon: start with cpu on, which cleans the cache). Mailbox 0x01200000.
// Started from kmon (2026-10-16, kboot EL3 monitor 4b423303): cpu on 1 0x01000000 0x11 (stub_off);
// cpu on 1 0x01000080 and cpu on 2 0x01000080 (race). Read with: call 0x010001f0, md 0x01200000.
// Results: docs/hardware/cpu-cores.md "Our own CPU_ON and CPU_OFF".
//  stub_off (0x01000000): +0 += 1, +8 = x0 (context), then CPU_OFF; if it returns, +0x10 = result
//  stub_race (0x01000080): N times: CPU_ON(core 3, stub_off), tally result, wait for AFFINITY_INFO(3) = off.
//     per core n at 0x01200100 + n*0x40: +0 ok, +8 -4, +16 -5, +24 other, +32 done, +40 wait timeouts
        .equ MB, 0x01200000
        .equ N, 200
        .org 0
stub_off:
        mov     x1, #MB
        ldr     x2, [x1]
        add     x2, x2, #1
        str     x2, [x1]
        str     x0, [x1, #8]
        dsb     sy
        mov     x0, #2
        movk    x0, #0x8400, lsl #16
        smc     #0
        str     x0, [x1, #0x10]
1:      b       1b
        .org 0x80
stub_race:
        mrs     x19, mpidr_el1
        and     x19, x19, #0xff
        mov     x21, #MB
        add     x19, x21, x19, lsl #6
        add     x19, x19, #0x100
        mov     x20, #N
2:      mov     x0, #3
        movk    x0, #0xc400, lsl #16
        mov     x1, #3
        adr     x2, stub_off
        mov     x3, x20
        smc     #0
        mov     x4, #24
        cmp     x0, #0
        b.ne    3f
        mov     x4, #0
        b       5f
3:      cmn     x0, #4
        b.ne    4f
        mov     x4, #8
        b       5f
4:      cmn     x0, #5
        b.ne    5f
        mov     x4, #16
5:      ldr     x5, [x19, x4]
        add     x5, x5, #1
        str     x5, [x19, x4]
        mov     x22, #0x100000
6:      mov     x0, #4
        movk    x0, #0xc400, lsl #16
        mov     x1, #3
        mov     x2, #0
        smc     #0
        cmp     x0, #1
        b.eq    7f
        subs    x22, x22, #1
        b.ne    6b
        ldr     x5, [x19, #40]
        add     x5, x5, #1
        str     x5, [x19, #40]
7:      subs    x20, x20, #1
        b.ne    2b
        mov     x5, #1
        str     x5, [x19, #32]
        dsb     sy
        mov     x0, #2
        movk    x0, #0x8400, lsl #16
        smc     #0
8:      b       8b
        .org 0x1f0
flush:  ret                             // kmon: call 0x010001f0 = clean+invalidate, then md
