// AArch64 demo: video_probe_mmu.s as something to watch.
// MMU on (same map as video_probe_mmu.s), then four phases on the 1600x1000 area:
//   1 scrolling bars at 35 fps (Doom's frame rate), 15 s
//   2 Doom path (320x200 8-bit -> palette -> x5) at 35 fps, 15 s
//   3 Doom path at full speed, 10 s
//   4 scrolling bars at full speed, 10 s
// Prints the frame rate of each phase, then idles (run with --watchdog).
.global _start
.equ W,     1600
.equ H,     1000
.equ PITCH, 3840
.equ ROW,   0x10400000                     // one 3200-byte row
.equ SRC2,  0x10410000                     // 320x200 8-bit image
.equ PAL,   0x10420000                     // 256 x RGB565 palette
.equ L1,    0x10600000                     // page tables (4 KB aligned)
.equ L2,    0x10601000

_start:
    mov     x20, #0xc000
    movk    x20, #0xf040, lsl #16          // UART0
    adr     x0, vectors
    msr     vbar_el2, x0
    bl      mmu_on
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
    mov     x0, #35
    udiv    x28, x22, x0                   // ticks per frame at 35 fps

// image (x + y) & 255 and a palette for the Doom path
    ldr     x10, =SRC2
    mov     x12, #0
1:  mov     x11, #0
2:  add     w0, w11, w12
    strb    w0, [x10], #1
    add     x11, x11, #1
    cmp     x11, #320
    b.lo    2b
    add     x12, x12, #1
    cmp     x12, #200
    b.lo    1b
    ldr     x10, =PAL
    mov     x11, #0
3:  lsr     w0, w11, #3                    // red   = i / 8
    lsl     w0, w0, #11
    lsl     w1, w11, #2                    // green = (i * 4) & 0xfc, top 6 bits
    and     w1, w1, #0xfc
    orr     w0, w0, w1, lsl #3
    mov     w1, #255
    sub     w1, w1, w11
    orr     w0, w0, w1, lsr #3             // blue  = (255 - i) / 8
    strh    w0, [x10], #2
    add     x11, x11, #1
    cmp     x11, #256
    b.lo    3b

    adr     x0, s_p1
    bl      puts
    adr     x0, bars_frame
    mov     x1, #15
    mov     x2, #1
    bl      run
    adr     x0, s_p2
    bl      puts
    adr     x0, doom_frame
    mov     x1, #15
    mov     x2, #1
    bl      run
    adr     x0, s_p3
    bl      puts
    adr     x0, doom_frame
    mov     x1, #10
    mov     x2, #0
    bl      run
    adr     x0, s_p4
    bl      puts
    adr     x0, bars_frame
    mov     x1, #10
    mov     x2, #0
    bl      run
    adr     x0, s_done
    bl      puts
9:  wfe
    b       9b

run:                                       // x0 = frame function, x1 = seconds, x2 = 1: pace to 35 fps
    stp     x29, x30, [sp, #-48]!
    stp     x19, x25, [sp, #16]
    stp     x23, x27, [sp, #32]
    mov     x19, x0
    mov     x25, x2
    mrs     x23, cntpct_el0
    mul     x27, x22, x1
    add     x27, x27, x23                  // deadline
    mov     x24, #0                        // frame number, read by the frame functions
1:  blr     x19
    add     x24, x24, #1
    cbz     x25, 3f
    mul     x0, x24, x28                   // wait for the next 1/35 s slot
    add     x0, x0, x23
2:  mrs     x1, cntpct_el0
    cmp     x1, x0
    b.lo    2b
3:  mrs     x0, cntpct_el0
    cmp     x0, x27
    b.lo    1b
    sub     x0, x0, x23
    mov     x1, x24
    bl      report
    ldp     x23, x27, [sp, #32]
    ldp     x19, x25, [sp, #16]
    ldp     x29, x30, [sp], #48
    ret

bars_frame:                                // bars 64 px wide, moved 8 px per frame. Clobbers x0-x3, x5-x11.
    stp     x29, x30, [sp, #-16]!
    ldr     x10, =ROW
    mov     x11, #0
1:  add     x0, x11, x24
    lsr     x0, x0, #3
    and     x0, x0, #7
    bl      colour
    stp     x0, x0, [x10], #16
    add     x11, x11, #1
    cmp     x11, #(W/8)
    b.lo    1b
    ldr     x0, =ROW
    mov     x1, #0                         // same row for every line
    bl      copy_rows
    ldp     x29, x30, [sp], #16
    ret

doom_frame:                                // 320x200 indexed -> palette (offset by frame) -> x5. Clobbers x0, x2, x3, x5, x7-x15.
    ldr     x14, =PAL
    mov     x5, x21
    ldr     x13, =SRC2
    mov     x12, #0
1:  ldr     x10, =ROW                      // expand one source row x5 into ROW
    mov     x11, #0
2:  ldrb    w0, [x13], #1
    add     w0, w0, w24
    and     w0, w0, #0xff
    ldrh    w0, [x14, x0, lsl #1]
    strh    w0, [x10]
    strh    w0, [x10, #2]
    strh    w0, [x10, #4]
    strh    w0, [x10, #6]
    strh    w0, [x10, #8]
    add     x10, x10, #10
    add     x11, x11, #1
    cmp     x11, #320
    b.lo    2b
    mov     x15, #5                        // copy ROW to 5 screen lines
3:  ldr     x9, =ROW
    mov     x7, x5
    mov     x8, #(W/8)
4:  ldp     x2, x3, [x9], #16
    stp     x2, x3, [x7], #16
    subs    x8, x8, #1
    b.ne    4b
    add     x5, x5, #PITCH
    subs    x15, x15, #1
    b.ne    3b
    add     x12, x12, #1
    cmp     x12, #200
    b.lo    1b
    ret

mmu_on:                                    // identity map, then MMU + caches on. Clobbers x0-x4.
    ldr     x0, =L1
    ldr     x1, =0x705                     // block: Normal WB (attr 1), inner shareable, AF
    str     x1, [x0]                       // 0x00000000-0x3fffffff
    ldr     x1, =(L2 | 3)
    str     x1, [x0, #8]                   // 0x40000000-0x7fffffff: 2 MB blocks in L2
    str     xzr, [x0, #16]                 // 0x80000000-0xbfffffff: unmapped
    ldr     x1, =(0xc0000000 | 0x0040000000000405 | (3 << 2))
    str     x1, [x0, #24]                  // 0xc0000000-0xffffffff: Device-nGnRE (attr 3), XN
    ldr     x0, =L2
    ldr     x1, =(0x40000000 | 0x705)
    mov     x2, #0
1:  ldr     x3, =0x709                     // Normal non-cacheable (attr 2)
    cmp     x2, #493                       // 0x7da00000 ...
    b.lo    2f
    cmp     x2, #495                       // ... 0x7dffffff: splash0 framebuffer
    b.hi    2f
    bic     x4, x1, #0xfff
    orr     x4, x4, x3
    b       3f
2:  mov     x4, x1
3:  str     x4, [x0, x2, lsl #3]
    add     x1, x1, #0x200000
    add     x2, x2, #1
    cmp     x2, #512
    b.lo    1b
    ldr     x0, =0x0444ff00                // MAIR: 0 Device-nGnRnE, 1 Normal WB, 2 Normal NC, 3 Device-nGnRE
    msr     mair_el2, x0
    ldr     x0, =0x80800020                // TCR: T0SZ 32 (4 GB), 4 KB pages, non-cacheable walks
    msr     tcr_el2, x0
    ldr     x0, =L1
    msr     ttbr0_el2, x0
    dsb     sy
    tlbi    alle2
    dsb     sy
    ic      iallu
    dsb     sy
    isb
    mrs     x0, sctlr_el2
    orr     x0, x0, #(1 << 0)              // M
    orr     x0, x0, #(1 << 2)              // C
    orr     x0, x0, #(1 << 12)             // I
    msr     sctlr_el2, x0
    isb
    ret

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

exc:                                       // any exception: print ESR, ELR, FAR and stop
    adr     x0, s_exc
    bl      puts
    mrs     x0, esr_el2
    bl      puthex
    mrs     x0, elr_el2
    bl      puthex
    mrs     x0, far_el2
    bl      puthex
1:  wfe
    b       1b

    .ltorg
    .balign 2
tbl:    .hword 0xf800, 0xfd20, 0xffe0, 0x07e0, 0x07ff, 0x001f, 0xf81f, 0xffff
s_hdr:  .asciz "\r\n== video demo (EL2, MMU on) ==\r\nsurface @ "
s_a:    .asciz "A stp fill 1600x1000:   "
s_b:    .asciz "B strh fill 1600x1000:  "
s_c:    .asciz "C copy 3.2 MB from RAM: "
s_d:    .asciz "D scrolling bars 10 s:  "
s_us:   .asciz " us/frame, "
s_dot:  .asciz "."
s_fps:  .asciz " fps\r\n"
s_crlf: .asciz "\r\n"
s_e:    .asciz "E Doom path 320x200 x5: "
s_exc:  .asciz "\r\nEXCEPTION esr/elr/far:\r\n"
s_p1:   .asciz "1 bars, 35 fps, 15 s:          "
s_p2:   .asciz "2 rainbow (Doom path), 35 fps: "
s_p3:   .asciz "3 rainbow, full speed, 10 s:   "
s_p4:   .asciz "4 bars, full speed, 10 s:      "
s_done: .asciz "done, idling\r\n"
    .balign 8
numbuf: .space 24
numbuf_end:

    .balign 2048
vectors:                                   // 16 entries x 0x80, all go to exc
    .rept 16
    b       exc
    .balign 128
    .endr
