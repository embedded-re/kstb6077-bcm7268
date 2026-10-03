// AArch64 probe (go -64, EL2, MMU off): run our own copy of BOLT's per-frame
// RDC list. BOLT's descriptor 0 (0xf0604000) runs the 73-word list at
// 0x7db09c60 every frame (count 0x48 = 73 words - 1). This probe:
//   1. copies those 73 words to free RAM at 0x02000000 and checks the copy
//   2. changes one word in the copy: the compositor background value
//      (f0645810 <- 0018b87b) becomes 00515af0
//   3. points descriptor 0 at the copy, bracketed by 0xf0605000 = 0 / 1 the
//      way BOLT's own list tails do; config and count stay as they are
//   4. after 10 s points descriptor 0 back at 0x7db09c60 the same way
// The list increments the frame counter 0xf0603484, so a counter that keeps
// counting shows the RDC is running our copy. Visible variant: GFD source
// width 0xf0641044 = 960 for the whole run (right half = background), own
// list after 3 s, restore to BOLT's running list 0x7db09ae0 (descriptor
// 0xf0604000 reads back the previously written value, not the current one). BOLT's lists in splash0 are
// only read, never written. Ends in wfe; use --watchdog.
        .equ SRC, 0x7db09c60
        .equ RUNNING, 0x7db09ae0               // BOLT's list actually in use
        .equ WIDTH, 0xf0641044
        .equ DST, 0x02000000
        .equ WORDS, 73
        .equ BGIDX, 4                          // word index of the bg value
        .equ NEWBG, 0x00515af0
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
        // 1. copy and verify
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
        ldr     w0, [x22, #BGIDX * 4]          // must be BOLT's bg value
        ldr     w1, =0x0018b87b
        cmp     w0, w1
        b.ne    bad
        adr     x0, s_copy; bl puts
        // 2. patch
        ldr     w0, =NEWBG
        str     w0, [x22, #BGIDX * 4]
        dsb     sy
        ldr     x23, =DESC0
        ldr     x24, =HOLD
        adr     x0, s_old;  bl puts
        bl      show
        ldr     x25, =WIDTH
        ldr     w26, [x25]                     // saved width
        mov     w0, #960
        str     w0, [x25]
        adr     x0, s_w;    bl puts
        mov     x0, #3;     bl wait
        // 3. switch to our copy
        str     wzr, [x24]
        str     w22, [x23]
        mov     w0, #1
        str     w0, [x24]
        adr     x0, s_on;   bl puts
        mov     x0, #1;     bl wait
        bl      show
        mov     x0, #9;     bl wait
        bl      show
        // 4. back to BOLT's list
        ldr     x0, =RUNNING
        str     wzr, [x24]
        str     w0, [x23]
        mov     w0, #1
        str     w0, [x24]
        mov     x0, #2;     bl wait
        str     w26, [x25]                     // width back
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
        ldr     x1, =DESC0
        ldr     w0, [x1];   bl puthex8
        ldr     x1, =FCNT
        ldr     w0, [x1];   bl puthex8
        ldr     x1, =BG
        ldr     w0, [x1];   bl puthex8
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
s_hdr:  .asciz "\r\nRDC own-list probe (visible). cols: desc0(f0604000) framecount(f0603484) bg(f0645810)\r\n"
s_copy: .asciz "copy of 73 words at 02000000 verified\r\n"
s_bad:  .asciz "COPY CHECK FAILED at word, nothing switched: "
s_old:  .asciz "before:     "
s_w:    .asciz "WIDTH 960 (blue right half) for 3 s\r\n"
s_on:   .asciz "OWN LIST ON for 10 s, after 1s and 10s:\r\n            "
s_rest: .asciz "restored:   "
s_hang: .asciz "\r\nUNEXPECTED EXCEPTION esr/elr: "
s_crlf: .asciz "\r\n"
s_done: .asciz "probe done\r\n"
