// AArch64 probe: how fast can bare-metal code update the HDMI framebuffer?
// Runs at EL2 straight from `go -64` (MMU and caches off, so every access is Device memory).
// Area: 1600x1000 (Doom's 320x200 scaled x5), centred at (160, 40).
//   A: solid fills with 16-byte stores (stp)          30 frames
//   B: solid fills with 2-byte stores (strh)           5 frames
//   C: copy a 3.2 MB image from RAM (ldp/stp)          10 frames
//   D: scrolling colour bars for 10 s (build a row in RAM, copy it to every line)
// Prints microseconds per frame and frames per second for each, then idles
// (run with --watchdog so the board reboots by itself).
.global _start
.equ W,     1600
.equ H,     1000
.equ PITCH, 3840
.equ SRC,   0x10000000                     // free DRAM (BOLT rmem), 3.2 MB test image
.equ ROW,   0x10400000                     // one 3200-byte row for test D

_start:
    mov     x20, #0xc000
    movk    x20, #0xf040, lsl #16          // UART0
    mov     sp, #0x01800000
    adr     x0, s_hdr
    bl      puts
    mov     x1, #0x1048
    movk    x1, #0xf064, lsl #16           // GFD surface address register
    ldr     w21, [x1]
    mov     x0, x21
    bl      puthex
    ldr     x2, =(40*PITCH + 160*2)
    add     x21, x21, x2                   // x21 = top-left of the 1600x1000 area
    mrs     x22, cntfrq_el0                // 27 MHz

// A: stp fills
    adr     x0, s_a
    bl      puts
    mrs     x23, cntpct_el0
    mov     x24, #0
1:  and     x0, x24, #7
    bl      colour
    bl      fill_stp
    add     x24, x24, #1
    cmp     x24, #30
    b.lo    1b
    mrs     x0, cntpct_el0
    sub     x0, x0, x23
    mov     x1, x24
    bl      report

// B: strh fills
    adr     x0, s_b
    bl      puts
    mrs     x23, cntpct_el0
    mov     x24, #0
2:  and     x0, x24, #7
    bl      colour
    bl      fill_strh
    add     x24, x24, #1
    cmp     x24, #5
    b.lo    2b
    mrs     x0, cntpct_el0
    sub     x0, x0, x23
    mov     x1, x24
    bl      report

// C: build diagonal stripes in RAM (not timed), then copy them to the screen
    ldr     x10, =SRC
    mov     x12, #0
3:  mov     x11, #0
4:  add     x0, x12, x11, lsl #2
    lsr     x0, x0, #5
    and     x0, x0, #7
    bl      colour
    stp     x0, x0, [x10], #16
    add     x11, x11, #1
    cmp     x11, #(W/8)
    b.lo    4b
    add     x12, x12, #1
    cmp     x12, #H
    b.lo    3b
    adr     x0, s_c
    bl      puts
    mrs     x23, cntpct_el0
    mov     x24, #0
5:  ldr     x0, =SRC
    mov     x1, #(W*2)
    bl      copy_rows
    add     x24, x24, #1
    cmp     x24, #10
    b.lo    5b
    mrs     x0, cntpct_el0
    sub     x0, x0, x23
    mov     x1, x24
    bl      report

// D: scrolling bars (64 px wide, 8 px per frame) for 10 s
    adr     x0, s_d
    bl      puts
    mrs     x23, cntpct_el0
    mov     x2, #10
    mul     x27, x22, x2
    add     x27, x27, x23                  // deadline
    mov     x24, #0
6:  ldr     x10, =ROW
    mov     x11, #0
7:  add     x0, x11, x24
    lsr     x0, x0, #3
    and     x0, x0, #7
    bl      colour
    stp     x0, x0, [x10], #16
    add     x11, x11, #1
    cmp     x11, #(W/8)
    b.lo    7b
    ldr     x0, =ROW
    mov     x1, #0                         // same row for every line
    bl      copy_rows
    add     x24, x24, #1
    mrs     x0, cntpct_el0
    cmp     x0, x27
    b.lo    6b
    sub     x0, x0, x23
    mov     x1, x24
    bl      report

    adr     x0, s_done
    bl      puts
9:  wfe
    b       9b

colour:                                    // x0 = index 0-7 -> x0 = RGB565 colour repeated 4 times
    adr     x1, tbl
    ldrh    w0, [x1, x0, lsl #1]
    mov     x1, #0x0001000100010001
    mul     x0, x0, x1
    ret

fill_stp:                                  // x0 = colour x4. Clobbers x5-x8.
    mov     x5, x21
    mov     x6, #H
1:  mov     x7, x5
    mov     x8, #(W/8)
2:  stp     x0, x0, [x7], #16
    subs    x8, x8, #1
    b.ne    2b
    add     x5, x5, #PITCH
    subs    x6, x6, #1
    b.ne    1b
    ret

fill_strh:                                 // w0 = colour. Clobbers x5-x8.
    mov     x5, x21
    mov     x6, #H
1:  mov     x7, x5
    mov     x8, #W
2:  strh    w0, [x7], #2
    subs    x8, x8, #1
    b.ne    2b
    add     x5, x5, #PITCH
    subs    x6, x6, #1
    b.ne    1b
    ret

copy_rows:                                 // x0 = source, x1 = source stride. Clobbers x0-x3, x5-x9.
    mov     x5, x21
    mov     x6, #H
1:  mov     x7, x5
    mov     x9, x0
    mov     x8, #(W/8)
2:  ldp     x2, x3, [x9], #16
    stp     x2, x3, [x7], #16
    subs    x8, x8, #1
    b.ne    2b
    add     x5, x5, #PITCH
    add     x0, x0, x1
    subs    x6, x6, #1
    b.ne    1b
    ret

report:                                    // x0 = ticks, x1 = frames. Prints "N us/frame, N.N fps".
    stp     x29, x30, [sp, #-32]!
    stp     x25, x26, [sp, #16]
    mov     x25, x0
    mov     x26, x1
    ldr     x2, =1000000
    mul     x0, x25, x2
    mul     x3, x22, x26
    udiv    x0, x0, x3
    bl      putdec
    adr     x0, s_us
    bl      puts
    mul     x0, x26, x22
    mov     x2, #10
    mul     x0, x0, x2
    udiv    x0, x0, x25                    // fps x 10
    udiv    x3, x0, x2
    msub    x25, x3, x2, x0
    mov     x0, x3
    bl      putdec
    adr     x0, s_dot
    bl      puts
    mov     x0, x25
    bl      putdec
    adr     x0, s_fps
    bl      puts
    ldp     x25, x26, [sp, #16]
    ldp     x29, x30, [sp], #32
    ret

putdec:                                    // x0 = unsigned number. Clobbers x0-x4.
    adr     x1, numbuf_end
    strb    wzr, [x1, #-1]!
    mov     x2, #10
1:  udiv    x3, x0, x2
    msub    x4, x3, x2, x0
    add     w4, w4, #'0'
    strb    w4, [x1, #-1]!
    mov     x0, x3
    cbnz    x0, 1b
    mov     x0, x1
    // falls through to puts

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
    adr     x0, s_crlf
    b       puts

    .ltorg
    .balign 2
tbl:    .hword 0xf800, 0xfd20, 0xffe0, 0x07e0, 0x07ff, 0x001f, 0xf81f, 0xffff
s_hdr:  .asciz "\r\n== video probe (EL2, MMU off) ==\r\nsurface @ "
s_a:    .asciz "A stp fill 1600x1000:   "
s_b:    .asciz "B strh fill 1600x1000:  "
s_c:    .asciz "C copy 3.2 MB from RAM: "
s_d:    .asciz "D scrolling bars 10 s:  "
s_us:   .asciz " us/frame, "
s_dot:  .asciz "."
s_fps:  .asciz " fps\r\n"
s_crlf: .asciz "\r\n"
s_done: .asciz "done, idling\r\n"
    .balign 8
numbuf: .space 24
numbuf_end:
