// AArch64 probe: is BOLT's splash audio still running after go -64?
// Prints the read pointer of audio ring buffer 0 (0xf0ca0800) every 0.25 s, 12 times,
// then idles (run with --watchdog so the board reboots by itself).
.global _start
_start:
    mov     x20, #0xc000
    movk    x20, #0xf040, lsl #16          // UART0
    adr     x0, s_hdr
    bl      puts
    mov     x21, #0x0800
    movk    x21, #0xf0ca, lsl #16          // ring buffer 0: +0 read, +4 write, +8 start, +0xc end
    mrs     x22, cntfrq_el0
    lsr     x22, x22, #2                   // ticks per 0.25 s
    mov     x23, #12
1:  ldr     w0, [x21]
    bl      puthex
    mrs     x5, cntpct_el0
    add     x5, x5, x22
2:  mrs     x6, cntpct_el0
    cmp     x6, x5
    b.lo    2b
    subs    x23, x23, #1
    b.ne    1b
    adr     x0, s_done
    bl      puts
3:  wfe
    b       3b

puts:                                      // x0 -> string. Clobbers x0-x2.
    ldrb    w1, [x0], #1
    cbz     w1, 9f
8:  ldr     w2, [x20, #0x14]
    tbz     w2, #5, 8b
    str     w1, [x20]
    b       puts
9:  ret

puthex:                                    // prints w0 as 8 hex digits + CRLF. Clobbers x0-x4.
    mov     x3, x0
    mov     x4, #28
1:  lsr     x1, x3, x4
    and     x1, x1, #0xf
    cmp     x1, #10
    add     x2, x1, #'0'
    add     x1, x1, #('a' - 10)
    csel    x1, x2, x1, lo
2:  ldr     w2, [x20, #0x14]
    tbz     w2, #5, 2b
    str     w1, [x20]
    subs    x4, x4, #4
    b.pl    1b
    mov     w1, #'\r'
3:  ldr     w2, [x20, #0x14]
    tbz     w2, #5, 3b
    str     w1, [x20]
    mov     w1, #'\n'
4:  ldr     w2, [x20, #0x14]
    tbz     w2, #5, 4b
    str     w1, [x20]
    ret

s_hdr:  .asciz "\r\n== audio ring buffer probe ==\r\n"
s_done: .asciz "done, idling\r\n"
