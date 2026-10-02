// AArch64 probe: vsync counter and double buffering through BOLT's RDC registers.
// Known from BOLT's display lists: each frame the RDC copies the value in 0xf0603488
// into the surface address register 0xf0641048. 0xf0603484 seems to count frames.
//   1. time 60 changes of 0xf0603484
//   2. copy the screen (buffer A, 0x7db0b700) to buffer B (0x7d600000), draw a magenta box on B
//   3. write B, then A, to 0xf0603488/0xf060348c and print 0xf0641048 after each
//   4. toggle A/B every 30 ticks, 8 times
//   5. scrolling bars, double-buffered, one frame per tick, 15 s
//   6. Doom path (320x200 -> palette -> x5), double-buffered, one frame per 2 ticks, 15 s
//   7. back to buffer A, idle (run with --watchdog)
// MMU map as video_probe_mmu.s, with 0x7d600000-0x7dffffff non-cacheable.
.global _start
.equ W,     1600
.equ H,     1000
.equ PITCH, 3840
.equ BUFA,  0x7db0b700                     // BOLT's framebuffer
.equ BUFB,  0x7d600000                     // second buffer, 1920x1080x2 = 0x3f4800 bytes
.equ AREA,  (40*PITCH + 160*2)             // top-left of the 1600x1000 area in a buffer
.equ ROW,   0x10400000
.equ SRC2,  0x10410000
.equ PAL,   0x10420000
.equ L1,    0x10600000
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
    movk    x1, #0xf064, lsl #16
    ldr     w0, [x1]
    bl      puthex
    mrs     x22, cntfrq_el0
    mov     x26, #0x3000
    movk    x26, #0xf060, lsl #16          // RDC: +0x484 counter, +0x488/+0x48c surface address

// 1: counter rate
    adr     x0, s_vs
    bl      puts
    bl      wait_vs
    mrs     x23, cntpct_el0
    mov     x24, #60
1:  bl      wait_vs
    subs    x24, x24, #1
    b.ne    1b
    mrs     x0, cntpct_el0
    sub     x0, x0, x23
    mov     x1, #60
    bl      report

// 2: B = copy of A, plus a magenta box in the centre
    ldr     x9, =BUFA
    ldr     x7, =BUFB
    ldr     x8, =(1920*1080*2/16)
1:  ldp     x2, x3, [x9], #16
    stp     x2, x3, [x7], #16
    subs    x8, x8, #1
    b.ne    1b
    ldr     x7, =(BUFB + 440*PITCH + 760*2)
    mov     x6, #200
    mov     w5, #0xf81f
1:  mov     x9, x7
    mov     x8, #400
2:  strh    w5, [x9], #2
    subs    x8, x8, #1
    b.ne    2b
    add     x7, x7, #PITCH
    subs    x6, x6, #1
    b.ne    1b
    dsb     sy

// 3: flip to B, then to A
    ldr     x0, =BUFB
    bl      flip
    mov     x24, #30
1:  bl      wait_vs
    subs    x24, x24, #1
    b.ne    1b
    adr     x0, s_b
    bl      puts
    mov     x1, #0x1048
    movk    x1, #0xf064, lsl #16
    ldr     w0, [x1]
    bl      puthex
    ldr     x0, =BUFA
    bl      flip
    mov     x24, #30
1:  bl      wait_vs
    subs    x24, x24, #1
    b.ne    1b
    adr     x0, s_a
    bl      puts
    mov     x1, #0x1048
    movk    x1, #0xf064, lsl #16
    ldr     w0, [x1]
    bl      puthex

// 4: toggle
    adr     x0, s_t
    bl      puts
    mov     x25, #8
1:  tst     x25, #1
    ldr     x0, =BUFA
    ldr     x1, =BUFB
    csel    x0, x0, x1, ne
    bl      flip
    mov     x24, #30
2:  bl      wait_vs
    subs    x24, x24, #1
    b.ne    2b
    subs    x25, x25, #1
    b.ne    1b                             // ends on A

// image and palette for the Doom path
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
3:  lsr     w0, w11, #3
    lsl     w0, w0, #11
    lsl     w1, w11, #2
    and     w1, w1, #0xfc
    orr     w0, w0, w1, lsl #3
    mov     w1, #255
    sub     w1, w1, w11
    orr     w0, w0, w1, lsr #3
    strh    w0, [x10], #2
    add     x11, x11, #1
    cmp     x11, #256
    b.lo    3b

// 5, 6: double-buffered animation
    adr     x0, s_p1
    bl      puts
    adr     x0, bars_frame
    mov     x1, #15
    mov     x2, #1
    bl      run_db
    adr     x0, s_p2
    bl      puts
    adr     x0, doom_frame
    mov     x1, #15
    mov     x2, #2
    bl      run_db

// 7: back to A
    ldr     x0, =BUFA
    bl      flip
    bl      wait_vs
    bl      wait_vs
    adr     x0, s_a
    bl      puts
    mov     x1, #0x1048
    movk    x1, #0xf064, lsl #16
    ldr     w0, [x1]
    bl      puthex
    adr     x0, s_done
    bl      puts
9:  wfe
    b       9b

wait_vs:                                   // wait until 0xf0603484 changes. Clobbers x0, x1.
    ldr     w0, [x26, #0x484]
1:  ldr     w1, [x26, #0x484]
    cmp     w1, w0
    b.eq    1b
    ret

flip:                                      // x0 = buffer address -> RDC surface registers
    str     w0, [x26, #0x488]
    str     w0, [x26, #0x48c]
    ret

run_db:                                    // x0 = frame function, x1 = seconds, x2 = ticks per frame
    stp     x29, x30, [sp, #-64]!
    stp     x19, x25, [sp, #16]
    stp     x23, x27, [sp, #32]
    stp     x17, x18, [sp, #48]
    mov     x19, x0
    mov     x25, x2
    ldr     x17, =BUFB                     // back buffer
    ldr     x18, =BUFA                     // front buffer
    mul     x27, x22, x1                   // duration in ticks (before wait_vs clobbers x1)
    bl      wait_vs
    mrs     x23, cntpct_el0
    add     x27, x27, x23                  // deadline
    mov     x24, #0
1:  ldr     x0, =AREA
    add     x21, x17, x0                   // draw into the back buffer
    blr     x19
    dsb     sy
    mov     x0, x17
    bl      flip                           // taken over at the next frame start
    mov     x0, x25
2:  str     x0, [sp, #-16]!
    bl      wait_vs
    ldr     x0, [sp], #16
    subs    x0, x0, #1
    b.ne    2b
    mov     x0, x17                        // swap
    mov     x17, x18
    mov     x18, x0
    add     x24, x24, #1
    mrs     x0, cntpct_el0
    cmp     x0, x27
    b.lo    1b
    sub     x0, x0, x23
    mov     x1, x24
    bl      report
    ldp     x17, x18, [sp, #48]
    ldp     x23, x27, [sp, #32]
    ldp     x19, x25, [sp, #16]
    ldp     x29, x30, [sp], #64
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
    cmp     x2, #491                       // 0x7d600000 ...
    b.lo    2f
    cmp     x2, #495                       // ... 0x7dffffff: buffer B + splash0 framebuffer
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
s_hdr:  .asciz "\r\n== vsync / double-buffer probe (EL2, MMU on) ==\r\nsurface @ "
s_us:   .asciz " us/frame, "
s_dot:  .asciz "."
s_fps:  .asciz " fps\r\n"
s_crlf: .asciz "\r\n"
s_exc:  .asciz "\r\nEXCEPTION esr/elr/far:\r\n"
s_vs:   .asciz "0xf0603484 ticks: "
s_b:    .asciz "surface reg after flip to B: "
s_a:    .asciz "surface reg after flip to A: "
s_t:    .asciz "toggling A/B every 0.5 s, 8 times\r\n"
s_p1:   .asciz "1 bars, double-buffered, 1 frame per tick, 15 s: "
s_p2:   .asciz "2 rainbow (Doom path), 2 ticks per frame, 15 s:   "
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
