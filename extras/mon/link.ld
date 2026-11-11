/* link.ld - the monitor at MON_ADDR (kstb.h). BOLT and `go` jump to the first byte. */
ENTRY(_start)
SECTIONS
{
    . = 0x00100000;
    .text : { *(.text.start) *(.text*) }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) }
    . = ALIGN(16);
    __bss_start = .;
    .bss (NOLOAD) : { *(.bss*) *(COMMON) }
    . = ALIGN(16);
    __bss_end = .;
    ASSERT(__bss_end <= 0x00140000, "monitor too big: it must end before DOOM_IMG")
}
