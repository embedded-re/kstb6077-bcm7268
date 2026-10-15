/* link.ld - the Doom port as one raw image at LOAD_ADDR (kstb.h).
   BOLT jumps to the first byte, so .text.start comes first. */
ENTRY(_start)
SECTIONS
{
    . = 0x01000000;
    .text : { *(.text.start) *(.text*) }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) }
    . = ALIGN(16);
    __bss_start = .;
    .bss (NOLOAD) : { *(.bss*) *(COMMON) }
    . = ALIGN(16);
    __bss_end = .;
}
