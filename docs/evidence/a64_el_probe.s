// AArch64 probe: prints "A64 OK, EL<n>" on the UART, then spins.
.global _start
_start:
    mov     x1, #0xc000
    movk    x1, #0xf040, lsl #16      // x1 = 0xf040c000 (UART)
    adr     x2, msg
1:  ldrb    w0, [x2], #1
    cbz     w0, 3f
2:  ldr     w3, [x1, #0x14]           // LSR
    tbz     w3, #5, 2b                // wait THRE
    str     w0, [x1]
    b       1b
3:  mrs     x4, CurrentEL             // EL in bits [3:2]
    ubfx    x4, x4, #2, #2
    add     w0, w4, #'0'
4:  ldr     w3, [x1, #0x14]
    tbz     w3, #5, 4b
    str     w0, [x1]
    adr     x2, tail
5:  ldrb    w0, [x2], #1
    cbz     w0, 7f
6:  ldr     w3, [x1, #0x14]
    tbz     w3, #5, 6b
    str     w0, [x1]
    b       5b
7:  wfe
    b       7b
msg:  .asciz "\r\nA64 OK, EL"
tail: .asciz "\r\n"
