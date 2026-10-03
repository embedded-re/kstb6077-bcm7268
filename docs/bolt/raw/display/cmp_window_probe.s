// AArch64 probe (go -64, EL2, MMU off): own copy of BOLT's per-frame RDC
// list (as rdc_ownlist_probe.s, background red), then three phases that
// each change one word in the copy, to find out what the compositor
// registers 0xf0645980-8c do (BOLT: 07800438 0 07800438 0):
//   A: 0xf0645988 <- 03c0021c   B: 0xf064598c <- 01e0010e
//   C: 0xf0645980 <- 03c0021c   (each undone before the next, 6 s each)
// Then descriptor 0 goes back to BOLT's running list 0x7db09ae0.
// BOLT's lists in splash0 are only read. Ends in wfe; use --watchdog.
        .equ SRC, 0x7db09c60
        .equ RUNNING, 0x7db09ae0
        .equ DST, 0x02000000
        .equ WORDS, 73
        .equ BGIDX, 4
        .equ NEWBG, 0x00515af0
        .equ W80, 28                            // copy word: value for f0645980
        .equ DESC0, 0xf0604000
        .equ HOLD, 0xf0605000
        .equ FCNT, 0xf0603484
        .equ BG, 0xf0645810
        .equ SEC, 27000000
        .global _start
_start:
        mov     x0, #0x01080000
        mov     sp, x0
        mov     x20, #0xc000
        movk    x20, #0xf040, lsl #16
        adr     x0, vectors
        msr     VBAR_EL2, x0
        isb
        adr     x0, s_hdr;  bl puts
        ldr     x21, =SRC
        ldr     x22, =DST
        mov     x9, #0
1:      ldr     w0, [x21, x9, lsl #2]
        str     w0, [x22, x9, lsl #2]
        add     x9, x9, #1
        cmp     x9, #WORDS
        b.lo    1b
        dsb     sy
        mov     x9, #0
2:      ldr     w0, [x21, x9, lsl #2]
        ldr     w1, [x22, x9, lsl #2]
        cmp     w0, w1
        b.ne    bad
        add     x9, x9, #1
        cmp     x9, #WORDS
        b.lo    2b
        mov     x9, #BGIDX                     // expected values in the copy
        ldr     w0, [x22, #BGIDX * 4]
        ldr     w1, =0x0018b87b
        cmp     w0, w1
        b.ne    bad
        mov     x9, #W80 - 1                   // header: IMMS_TO_REGS n=4, f0645980
        ldr     w0, [x22, #(W80 - 2) * 4]
        ldr     w1, =0x06000003
        cmp     w0, w1
        b.ne    bad
        ldr     w0, [x22, #(W80 - 1) * 4]
        ldr     w1, =0xf0645980
        cmp     w0, w1
        b.ne    bad
        mov     x9, #W80
        ldr     w0, [x22, #W80 * 4]
        ldr     w1, =0x07800438
        cmp     w0, w1
        b.ne    bad
        ldr     w0, [x22, #(W80 + 2) * 4]
        cmp     w0, w1
        b.ne    bad
        adr     x0, s_copy; bl puts
        ldr     w0, =NEWBG
        str     w0, [x22, #BGIDX * 4]
        dsb     sy
        ldr     x23, =DESC0
        ldr     x24, =HOLD
        adr     x0, s_old;  bl puts
        bl      show
        str     wzr, [x24]
        str     w22, [x23]
        mov     w0, #1
        str     w0, [x24]
        adr     x0, s_on;   bl puts
        mov     x0, #2;     bl wait
        bl      show
        // phase A: f0645988
        ldr     w0, =0x03c0021c
        str     w0, [x22, #(W80 + 2) * 4]
        dsb     sy
        adr     x0, s_a;    bl puts
        mov     x0, #1;     bl wait
        bl      show
        mov     x0, #5;     bl wait
        ldr     w0, =0x07800438
        str     w0, [x22, #(W80 + 2) * 4]
        // phase B: f064598c
        ldr     w0, =0x01e0010e
        str     w0, [x22, #(W80 + 3) * 4]
        dsb     sy
        adr     x0, s_b;    bl puts
        mov     x0, #1;     bl wait
        bl      show
        mov     x0, #5;     bl wait
        str     wzr, [x22, #(W80 + 3) * 4]
        // phase C: f0645980
        ldr     w0, =0x03c0021c
        str     w0, [x22, #W80 * 4]
        dsb     sy
        adr     x0, s_c;    bl puts
        mov     x0, #1;     bl wait
        bl      show
        mov     x0, #5;     bl wait
        ldr     w0, =0x07800438
        str     w0, [x22, #W80 * 4]
        dsb     sy
        adr     x0, s_n;    bl puts
        mov     x0, #2;     bl wait
        bl      show
        // back to BOLT's list
        ldr     x0, =RUNNING
        str     wzr, [x24]
        str     w0, [x23]
        mov     w0, #1
        str     w0, [x24]
        mov     x0, #1;     bl wait
        adr     x0, s_rest; bl puts
        bl      show
        adr     x0, s_done; bl puts
9:      wfe
        b       9b
bad:    adr     x0, s_bad;  bl puts
        mov     x0, x9;     bl puthex8
        adr     x0, s_crlf; bl puts
        adr     x0, s_done; bl puts
        b       9b

show:   mov     x28, x30
        ldr     x1, =FCNT
        ldr     w0, [x1];   bl puthex8
        ldr     x1, =BG
        ldr     w0, [x1];   bl puthex8
        ldr     x1, =0xf0645980
        ldr     w0, [x1];        bl puthex8
        ldr     w0, [x1, #4];    bl puthex8
        ldr     w0, [x1, #8];    bl puthex8
        ldr     w0, [x1, #12];   bl puthex8
        adr     x0, s_crlf; bl puts
        ret     x28
wait:   ldr     x1, =SEC                       // x0 seconds
        mul     x1, x1, x0
        mrs     x2, CNTPCT_EL0
        add     x2, x2, x1
1:      mrs     x3, CNTPCT_EL0
        cmp     x3, x2
        b.lo    1b
        ret

        .balign 2048
vectors:
        .rept 16
        b       hang
        .balign 128
        .endr
hang:   adr     x0, s_hang; bl puts
        mrs     x0, ESR_EL2; bl puthex8
        mrs     x0, ELR_EL2; bl puthex8
1:      wfe
        b       1b

puts:   ldrb    w1, [x0], #1
        cbz     w1, 9f
8:      ldr     w2, [x20, #0x14]
        tbz     w2, #5, 8b
        str     w1, [x20]
        b       puts
9:      ret
puthex8:
        mov     w3, w0
        mov     w4, #28
1:      lsr     w1, w3, w4
        and     w1, w1, #0xf
        cmp     w1, #10
        add     w2, w1, #'0'
        add     w1, w1, #('a' - 10)
        csel    w1, w2, w1, lo
2:      ldr     w2, [x20, #0x14]
        tbz     w2, #5, 2b
        str     w1, [x20]
        subs    w4, w4, #4
        b.pl    1b
        mov     w1, #' '
3:      ldr     w2, [x20, #0x14]
        tbz     w2, #5, 3b
        str     w1, [x20]
        ret

        .ltorg
s_hdr:  .asciz "\r\nCMP window probe. cols: framecount bg f0645980 984 988 98c\r\n"
s_copy: .asciz "copy of 73 words at 02000000 verified\r\n"
s_bad:  .asciz "COPY CHECK FAILED at word, nothing switched: "
s_old:  .asciz "before:      "
s_on:   .asciz "own list, red bg: "
s_a:    .asciz "PHASE A f0645988=03c0021c: "
s_b:    .asciz "PHASE B f064598c=01e0010e: "
s_c:    .asciz "PHASE C f0645980=03c0021c: "
s_n:    .asciz "all undone:  "
s_rest: .asciz "restored:    "
s_hang: .asciz "\r\nUNEXPECTED EXCEPTION esr/elr: "
s_crlf: .asciz "\r\n"
s_done: .asciz "probe done\r\n"
