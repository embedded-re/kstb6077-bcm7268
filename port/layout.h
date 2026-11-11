// layout.h - where kmon's `tftp get` puts each piece (all below 0x06000000,
// kmon's tftp limit; the kernel needs 2 MB alignment).
#define KERNEL_ADDR  0x02000000     // Image; runs in place, up to the initramfs
#define INITRD_ADDR  0x04800000     // initramfs.cpio.gz
#define DTB_ADDR     0x05e00000     // kstb6077.dtb
#define BOOT_ADDR    0x05f00000     // boot.bin, started with `go`
