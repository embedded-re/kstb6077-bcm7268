/* kboot: linked at KBOOT_ADDR (slot.h); STARTUP loads it there. */
ENTRY(_start)
SECTIONS
{
    . = 0x03000000;
    .text   : { *(.text.start) *(.text*) }
    .rodata : { *(.rodata*) }
    .data   : { *(.data*) }
    . = ALIGN(16);
    __bss_start = .;
    .bss    : { *(.bss*) *(COMMON) }
    . = ALIGN(16);
    __bss_end = .;
    . += 0x4000;
    stack_top = .;
    ASSERT(__bss_start - 0x03000000 <= 0x10000, "kboot.bin is larger than LOADER_MAX")
}
