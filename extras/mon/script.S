// script.S - BOLT's display script (SplashData and everything it points to),
// built into kmon so video.c never reads BOLT's RAM. bolt_splashdata.dat is
// BOLT's RAM 0x07054318-0x07059347 (0x5030 bytes, CRC 0ed66aea), copied with
// kmon `tftp put` on 2026-10-17.
        .section .rodata
        .balign 4
        .global bolt_script
bolt_script:
        .incbin "bolt_splashdata.dat"
        .global bolt_script_end
bolt_script_end:
