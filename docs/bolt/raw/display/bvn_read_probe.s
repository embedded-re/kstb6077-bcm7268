// AArch64 probe (go -64, EL2, MMU off): read-only dump of every display
// register that BOLT's RDC display lists (splash0 0x7db08fa0-0x7db0b6f7)
// write or read, as decoded by tools/re/rdc.py. 590 addresses; the HDMI
// registers 0xf06fa828/884/888/898 are left out on purpose.
// One abort-safe 32-bit read per address; an aborted read prints xxxxxxxx.
// Ends in wfe; use --watchdog.
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
        adr     x26, addrs
next:   ldr     w22, [x26], #4
        cbz     w22, done
        mov     x0, x22;    bl puthex8
        mov     x19, #0
        ldr     w23, [x22]
        dsb     sy
        isb
        msr     DAIFClr, #4
        isb
        nop
        msr     DAIFSet, #4
        cbz     x19, 1f
        adr     x0, s_x;    bl puts
        b       2f
1:      mov     w0, w23;    bl puthex8
2:      adr     x0, s_crlf; bl puts
        b       next
done:   adr     x0, s_done; bl puts
9:      wfe
        b       9b

        .balign 2048
vectors:
        .rept 4
        b       hang
        .balign 128
        .endr
        b       sync_h
        .balign 128
        b       hang
        .balign 128
        b       hang
        .balign 128
        b       serr_h
        .balign 128
        .rept 8
        b       hang
        .balign 128
        .endr
sync_h: mov     x19, #1
        mrs     x25, ELR_EL2
        add     x25, x25, #4
        msr     ELR_EL2, x25
        mov     w23, #0
        eret
serr_h: mov     x19, #2
        mov     w23, #0
        eret
hang:   adr     x0, s_hang; bl puts
        mrs     x0, ESR_EL2; bl puthex8
        mrs     x0, ELR_EL2; bl puthex8
1:      wfe
        b       1b

// puts / puthex8 do not touch x9-x12, x19-x28.
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

        .balign 4
addrs:
        .word   0xf0603480
        .word   0xf0603484
        .word   0xf0603488
        .word   0xf060348c
        .word   0xf0603498
        .word   0xf0604000
        .word   0xf0604008
        .word   0xf0604010
        .word   0xf0604018
        .word   0xf0604040
        .word   0xf0604048
        .word   0xf0604050
        .word   0xf0604058
        .word   0xf0605000
        .word   0xf0641008
        .word   0xf0641010
        .word   0xf0641014
        .word   0xf0641018
        .word   0xf064101c
        .word   0xf0641020
        .word   0xf0641024
        .word   0xf0641028
        .word   0xf0641030
        .word   0xf0641034
        .word   0xf0641038
        .word   0xf064103c
        .word   0xf0641040
        .word   0xf0641044
        .word   0xf0641048
        .word   0xf0641050
        .word   0xf0641058
        .word   0xf064106c
        .word   0xf06410f0
        .word   0xf06410f4
        .word   0xf06410f8
        .word   0xf06410fc
        .word   0xf0641158
        .word   0xf0641170
        .word   0xf0641174
        .word   0xf0641178
        .word   0xf064117c
        .word   0xf0641180
        .word   0xf0641184
        .word   0xf0641188
        .word   0xf064118c
        .word   0xf0641230
        .word   0xf0641244
        .word   0xf0641248
        .word   0xf064124c
        .word   0xf0641250
        .word   0xf0641254
        .word   0xf0641258
        .word   0xf064125c
        .word   0xf0641260
        .word   0xf0641264
        .word   0xf0641268
        .word   0xf064126c
        .word   0xf0641270
        .word   0xf0641274
        .word   0xf0641278
        .word   0xf064127c
        .word   0xf0641280
        .word   0xf0641284
        .word   0xf0641288
        .word   0xf064128c
        .word   0xf0641290
        .word   0xf0641294
        .word   0xf0641298
        .word   0xf064129c
        .word   0xf06412a0
        .word   0xf06412a4
        .word   0xf06412a8
        .word   0xf06412ac
        .word   0xf06412b0
        .word   0xf06412b4
        .word   0xf06412b8
        .word   0xf06412bc
        .word   0xf06412c0
        .word   0xf06412c4
        .word   0xf06412c8
        .word   0xf06412cc
        .word   0xf06412d0
        .word   0xf06412d4
        .word   0xf06412d8
        .word   0xf06412dc
        .word   0xf06412e0
        .word   0xf06412e4
        .word   0xf06412e8
        .word   0xf06412ec
        .word   0xf06412f0
        .word   0xf06412f4
        .word   0xf06412f8
        .word   0xf06412fc
        .word   0xf0641300
        .word   0xf0641314
        .word   0xf0641318
        .word   0xf064131c
        .word   0xf0641320
        .word   0xf0641324
        .word   0xf0641328
        .word   0xf064132c
        .word   0xf0641330
        .word   0xf0641334
        .word   0xf0641338
        .word   0xf064133c
        .word   0xf0641340
        .word   0xf0641344
        .word   0xf0641348
        .word   0xf064134c
        .word   0xf0641350
        .word   0xf0645808
        .word   0xf064580c
        .word   0xf0645810
        .word   0xf0645814
        .word   0xf0645818
        .word   0xf064581c
        .word   0xf0645828
        .word   0xf064582c
        .word   0xf0645830
        .word   0xf0645834
        .word   0xf0645838
        .word   0xf0645840
        .word   0xf0645844
        .word   0xf0645860
        .word   0xf0645864
        .word   0xf0645980
        .word   0xf0645984
        .word   0xf0645988
        .word   0xf064598c
        .word   0xf0645990
        .word   0xf0645994
        .word   0xf0645a20
        .word   0xf0645a24
        .word   0xf0645a28
        .word   0xf0645a2c
        .word   0xf0645f20
        .word   0xf0645f24
        .word   0xf0645f28
        .word   0xf0645f2c
        .word   0xf065001c
        .word   0xf06e0004
        .word   0xf06e0008
        .word   0xf06e000c
        .word   0xf06e0010
        .word   0xf06e0014
        .word   0xf06e0018
        .word   0xf06e001c
        .word   0xf06e0020
        .word   0xf06e0024
        .word   0xf06e0028
        .word   0xf06e002c
        .word   0xf06e0030
        .word   0xf06e0034
        .word   0xf06e0038
        .word   0xf06e003c
        .word   0xf06e0040
        .word   0xf06e0044
        .word   0xf06e0048
        .word   0xf06e004c
        .word   0xf06e0058
        .word   0xf06e005c
        .word   0xf06e006c
        .word   0xf06e02b4
        .word   0xf06e02c0
        .word   0xf06e0400
        .word   0xf06e0404
        .word   0xf06e0408
        .word   0xf06e040c
        .word   0xf06e0410
        .word   0xf06e0414
        .word   0xf06e0418
        .word   0xf06e041c
        .word   0xf06e0420
        .word   0xf06e0424
        .word   0xf06e0428
        .word   0xf06e042c
        .word   0xf06e0430
        .word   0xf06e0434
        .word   0xf06e0438
        .word   0xf06e043c
        .word   0xf06e0440
        .word   0xf06e0444
        .word   0xf06e0448
        .word   0xf06e044c
        .word   0xf06e0450
        .word   0xf06e0454
        .word   0xf06e0458
        .word   0xf06e045c
        .word   0xf06e0460
        .word   0xf06e0464
        .word   0xf06e0468
        .word   0xf06e046c
        .word   0xf06e0470
        .word   0xf06e0474
        .word   0xf06e0478
        .word   0xf06e047c
        .word   0xf06e0480
        .word   0xf06e0484
        .word   0xf06e0488
        .word   0xf06e048c
        .word   0xf06e0490
        .word   0xf06e0494
        .word   0xf06e0498
        .word   0xf06e049c
        .word   0xf06e04a0
        .word   0xf06e04a4
        .word   0xf06e04a8
        .word   0xf06e04ac
        .word   0xf06e04b0
        .word   0xf06e04b4
        .word   0xf06e04b8
        .word   0xf06e04bc
        .word   0xf06e04c0
        .word   0xf06e04c4
        .word   0xf06e04c8
        .word   0xf06e04cc
        .word   0xf06e04d0
        .word   0xf06e04d4
        .word   0xf06e04d8
        .word   0xf06e04dc
        .word   0xf06e04e0
        .word   0xf06e04e4
        .word   0xf06e04e8
        .word   0xf06e04ec
        .word   0xf06e04f0
        .word   0xf06e04f4
        .word   0xf06e04f8
        .word   0xf06e04fc
        .word   0xf06e0500
        .word   0xf06e0504
        .word   0xf06e0508
        .word   0xf06e050c
        .word   0xf06e0510
        .word   0xf06e0514
        .word   0xf06e0518
        .word   0xf06e051c
        .word   0xf06e0520
        .word   0xf06e0524
        .word   0xf06e0528
        .word   0xf06e052c
        .word   0xf06e0530
        .word   0xf06e0534
        .word   0xf06e0538
        .word   0xf06e053c
        .word   0xf06e0540
        .word   0xf06e0544
        .word   0xf06e0548
        .word   0xf06e054c
        .word   0xf06e0550
        .word   0xf06e0554
        .word   0xf06e0558
        .word   0xf06e055c
        .word   0xf06e0560
        .word   0xf06e0564
        .word   0xf06e0568
        .word   0xf06e056c
        .word   0xf06e0570
        .word   0xf06e0574
        .word   0xf06e0578
        .word   0xf06e057c
        .word   0xf06e0580
        .word   0xf06e0584
        .word   0xf06e0588
        .word   0xf06e058c
        .word   0xf06e0590
        .word   0xf06e0594
        .word   0xf06e0598
        .word   0xf06e059c
        .word   0xf06e05a0
        .word   0xf06e05a4
        .word   0xf06e05a8
        .word   0xf06e05ac
        .word   0xf06e05b0
        .word   0xf06e05b4
        .word   0xf06e05b8
        .word   0xf06e05bc
        .word   0xf06e05c0
        .word   0xf06e05c4
        .word   0xf06e05c8
        .word   0xf06e05cc
        .word   0xf06e05d0
        .word   0xf06e05d4
        .word   0xf06e05d8
        .word   0xf06e05dc
        .word   0xf06e05e0
        .word   0xf06e05e4
        .word   0xf06e05e8
        .word   0xf06e05ec
        .word   0xf06e05f0
        .word   0xf06e05f4
        .word   0xf06e05f8
        .word   0xf06e05fc
        .word   0xf06e0600
        .word   0xf06e0604
        .word   0xf06e0608
        .word   0xf06e060c
        .word   0xf06e0610
        .word   0xf06e0614
        .word   0xf06e0618
        .word   0xf06e061c
        .word   0xf06e0620
        .word   0xf06e0624
        .word   0xf06e0628
        .word   0xf06e062c
        .word   0xf06e0630
        .word   0xf06e0634
        .word   0xf06e0638
        .word   0xf06e063c
        .word   0xf06e0640
        .word   0xf06e0644
        .word   0xf06e0648
        .word   0xf06e064c
        .word   0xf06e0650
        .word   0xf06e0654
        .word   0xf06e0658
        .word   0xf06e065c
        .word   0xf06e0660
        .word   0xf06e0664
        .word   0xf06e0668
        .word   0xf06e066c
        .word   0xf06e0670
        .word   0xf06e0674
        .word   0xf06e0678
        .word   0xf06e067c
        .word   0xf06e0680
        .word   0xf06e0684
        .word   0xf06e0688
        .word   0xf06e068c
        .word   0xf06e0690
        .word   0xf06e0694
        .word   0xf06e0698
        .word   0xf06e069c
        .word   0xf06e06a0
        .word   0xf06e06a4
        .word   0xf06e06a8
        .word   0xf06e06ac
        .word   0xf06e06b0
        .word   0xf06e06b4
        .word   0xf06e06b8
        .word   0xf06e06bc
        .word   0xf06e06c0
        .word   0xf06e06c4
        .word   0xf06e06c8
        .word   0xf06e06cc
        .word   0xf06e06d0
        .word   0xf06e06d4
        .word   0xf06e06d8
        .word   0xf06e06dc
        .word   0xf06e06e0
        .word   0xf06e06e4
        .word   0xf06e06e8
        .word   0xf06e06ec
        .word   0xf06e06f0
        .word   0xf06e06f4
        .word   0xf06e06f8
        .word   0xf06e06fc
        .word   0xf06e0700
        .word   0xf06e0704
        .word   0xf06e0708
        .word   0xf06e070c
        .word   0xf06e0710
        .word   0xf06e0714
        .word   0xf06e0718
        .word   0xf06e071c
        .word   0xf06e0720
        .word   0xf06e0724
        .word   0xf06e0728
        .word   0xf06e072c
        .word   0xf06e0730
        .word   0xf06e0734
        .word   0xf06e0738
        .word   0xf06e073c
        .word   0xf06e0740
        .word   0xf06e0744
        .word   0xf06e0748
        .word   0xf06e074c
        .word   0xf06e0750
        .word   0xf06e0754
        .word   0xf06e0758
        .word   0xf06e075c
        .word   0xf06e0760
        .word   0xf06e0764
        .word   0xf06e0768
        .word   0xf06e076c
        .word   0xf06e0770
        .word   0xf06e0774
        .word   0xf06e0778
        .word   0xf06e077c
        .word   0xf06e0780
        .word   0xf06e0784
        .word   0xf06e0788
        .word   0xf06e078c
        .word   0xf06e0790
        .word   0xf06e0794
        .word   0xf06e0798
        .word   0xf06e079c
        .word   0xf06e07a0
        .word   0xf06e07a4
        .word   0xf06e07a8
        .word   0xf06e07ac
        .word   0xf06e07b0
        .word   0xf06e07b4
        .word   0xf06e07b8
        .word   0xf06e07bc
        .word   0xf06e07c0
        .word   0xf06e07c4
        .word   0xf06e07c8
        .word   0xf06e07cc
        .word   0xf06e07d0
        .word   0xf06e07d4
        .word   0xf06e07d8
        .word   0xf06e07dc
        .word   0xf06e07e0
        .word   0xf06e07e4
        .word   0xf06e07e8
        .word   0xf06e07ec
        .word   0xf06e07f0
        .word   0xf06e07f4
        .word   0xf06e07f8
        .word   0xf06e07fc
        .word   0xf06e2400
        .word   0xf06e2404
        .word   0xf06e2408
        .word   0xf06e240c
        .word   0xf06e2410
        .word   0xf06e2414
        .word   0xf06e4004
        .word   0xf06e4008
        .word   0xf06e400c
        .word   0xf06e4010
        .word   0xf06e4014
        .word   0xf06e4018
        .word   0xf06e401c
        .word   0xf06e4020
        .word   0xf06e4024
        .word   0xf06e4028
        .word   0xf06e402c
        .word   0xf06e4030
        .word   0xf06e4034
        .word   0xf06e4038
        .word   0xf06e403c
        .word   0xf06e4040
        .word   0xf06e4044
        .word   0xf06e4048
        .word   0xf06e404c
        .word   0xf06e4050
        .word   0xf06e4054
        .word   0xf06e4058
        .word   0xf06e405c
        .word   0xf06e4060
        .word   0xf06e4064
        .word   0xf06e4068
        .word   0xf06e406c
        .word   0xf06e4070
        .word   0xf06e4074
        .word   0xf06e4078
        .word   0xf06e407c
        .word   0xf06e4080
        .word   0xf06e4084
        .word   0xf06e4088
        .word   0xf06e408c
        .word   0xf06e4090
        .word   0xf06e4094
        .word   0xf06e40b8
        .word   0xf06e40bc
        .word   0xf06e40c0
        .word   0xf06e40c4
        .word   0xf06e40c8
        .word   0xf06e40cc
        .word   0xf06e40dc
        .word   0xf06e4140
        .word   0xf06e4144
        .word   0xf06e4148
        .word   0xf06e414c
        .word   0xf06e4150
        .word   0xf06e4154
        .word   0xf06e4158
        .word   0xf06e415c
        .word   0xf06e4160
        .word   0xf06e4164
        .word   0xf06e4168
        .word   0xf06e416c
        .word   0xf06e4170
        .word   0xf06e4174
        .word   0xf06e4178
        .word   0xf06e417c
        .word   0xf06e4180
        .word   0xf06e4184
        .word   0xf06e6008
        .word   0xf06e6010
        .word   0xf06e6014
        .word   0xf06e6018
        .word   0xf06e601c
        .word   0xf06e6020
        .word   0xf06e602c
        .word   0xf06e6030
        .word   0xf06e60f0
        .word   0xf06e60fc
        .word   0xf06e6180
        .word   0xf06e618c
        .word   0xf06e6190
        .word   0xf06e6194
        .word   0xf06e6198
        .word   0xf06e619c
        .word   0xf06e61a0
        .word   0xf06e61a4
        .word   0xf06e61a8
        .word   0xf06e61ac
        .word   0xf06e61b0
        .word   0xf06e61b4
        .word   0xf06e61b8
        .word   0xf06e61bc
        .word   0xf06e61c0
        .word   0xf06e61c4
        .word   0xf06e61c8
        .word   0xf06e61cc
        .word   0xf06e61d0
        .word   0xf06e61d4
        .word   0xf06e61d8
        .word   0xf06e61dc
        .word   0xf06e61e0
        .word   0xf06e61e4
        .word   0xf06e61e8
        .word   0xf06e61ec
        .word   0xf06e61f0
        .word   0xf06e61f4
        .word   0xf06e61f8
        .word   0xf06e61fc
        .word   0xf06e6200
        .word   0xf06e6204
        .word   0xf06e6208
        .word   0xf06e620c
        .word   0xf06e6210
        .word   0xf06e6214
        .word   0xf06e6218
        .word   0xf06e621c
        .word   0xf06e6220
        .word   0xf06e6224
        .word   0xf06e6228
        .word   0xf06e622c
        .word   0xf06e6230
        .word   0xf06e6234
        .word   0xf06e6238
        .word   0xf06e623c
        .word   0xf06e6240
        .word   0xf06e6244
        .word   0xf06e6248
        .word   0xf06e624c
        .word   0xf06e6250
        .word   0xf06e6254
        .word   0xf06e6258
        .word   0xf06e625c
        .word   0xf06e6260
        .word   0xf06e6264
        .word   0xf06e6268
        .word   0xf06e626c
        .word   0xf06e6270
        .word   0xf06e6274
        .word   0xf06e6278
        .word   0xf06e627c
        .word   0xf06e6280
        .word   0xf06e6284
        .word   0xf06e6288
        .word   0xf06e6900
        .word   0xf06e6a0c
        .word   0xf06e6a10
        .word   0xf06e6c00
        .word   0xf06e7004
        .word   0xf06e700c
        .word   0xf06e7014
        .word   0xf06e7030
        .word   0xf06e7034
        .word   0xf06e743c
        .word   0xf06e7450
        .word   0xf06e7514
        .word   0xf06e751c
        .word   0xf06e7520
        .word   0xf06e7524
        .word   0xf06fa074
        .word   0xf06fa800
        .word   0xf06fa810
        .word   0xf06fa814
        .word   0xf06fa81c
        .word   0xf06fa820
        .word   0xf06fa834
        .word   0xf06fa850
        .word   0xf06fa854
        .word   0xf06fa85c
        .word   0xf06fa880
        .word   0xf06fa89c
        .word   0
s_hdr:  .asciz "\r\ndisplay register read probe (read-only)\r\n"
s_x:    .asciz "xxxxxxxx "
s_hang: .asciz "\r\nUNEXPECTED EXCEPTION esr/elr: "
s_crlf: .asciz "\r\n"
s_done: .asciz "probe done\r\n"
