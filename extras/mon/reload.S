// reload.S - run a new monitor build from RAM, without flashing or TFTP:
//   tools/kstb-send --go extras/mon/build/mon-ram.bin
// kmon's `go` turns the MMU off and jumps here (at 0x01000000). This copies
// the monitor that follows (MON_MAX bytes) to MON_ADDR and starts it fresh.
// Position-independent; the old monitor is overwritten.

        .section .text
        .global _reload
_reload:
        adr     x0, payload
        mov     x1, #0x00100000                 // MON_ADDR
        mov     x2, #0x00040000                 // MON_MAX
1:      ldp     x3, x4, [x0], #16
        stp     x3, x4, [x1], #16
        subs    x2, x2, #16
        b.ne    1b
        dsb     sy
        ic      iallu
        dsb     sy
        isb
        mov     x0, #0
        mov     x1, #0x00100000
        br      x1

        .balign 256
payload:
