// AArch64 probe: read the GFD surface address, draw a 400x200 magenta box in the centre.
.global _start
_start:
    mov     x20, #0xc000
    movk    x20, #0xf040, lsl #16          // UART0
    adr     x0, s_hdr
    bl      puts
    mov     x1, #0x1048
    movk    x1, #0xf064, lsl #16           // GFD surface address register
    ldr     w21, [x1]                      // x21 = framebuffer base
    mov     x0, x21
    bl      puthex
    mov     w5, #0xf81f                    // RGB565 magenta
    mov     x6, #440                       // y start
    mov     x10, #3840                     // pitch
1:  madd    x7, x6, x10, x21               // row = base + y*pitch
    add     x7, x7, #(760*2)               // + x start
    mov     x8, #400                       // width
2:  strh    w5, [x7], #2
    subs    x8, x8, #1
    b.ne    2b
    add     x6, x6, #1
    cmp     x6, #640
    b.lo    1b
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

s_hdr:  .asciz "\r\n== fbdraw probe ==\r\nsurface @ "
s_done: .asciz "box drawn, idling\r\n"
