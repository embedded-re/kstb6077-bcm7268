// kstb.h - KSTB6077 addresses and the monitor's memory layout.
// Sources: docs/hardware/registers.md and the docs it points to.

#include <stdint.h>

#define UART0       0xf040c000      // serial console, 16550, registers 4 bytes apart

// RAM layout (docs/hardware/memory-map.md: 0x00001000-0x063fffff is free after go)
#define MON_ADDR    0x00100000      // the monitor (code, data, bss, stacks, page tables)
#define MON_MAX     0x00040000      //   at most 256 KB, checked by link.ld
#define DOOM_IMG    0x00140000      // KWAD image of doom.bin, loaded with the monitor (the bundle)
#define BUNDLE_END  0x00400000
#define DMA_BASE    0x00600000      // 2 MB, mapped non-cacheable: USB descriptors and buffers
#define SCROLLBACK  0x00800000      // HDMI console scrollback (fbcon.c), 1024 lines x 114 = 114 KB
#define FAT_BUF     0x00820000      // FAT32 work buffers (../fat), 64 KB
#define HEAP_BASE   0x00f00000      // malloc (libc), 1 MB
#define HEAP_END    0x01000000
#define PROG_ADDR   0x01000000      // where programs run (doom.bin, kstb-run/build-c programs)
#define WAD_ADDR    0x10000000      // the WAD's KWAD image (loaded by BOLT, or by `doom` itself)
#define WAD_OFFSET  0x100000        // where it is in flash0.splash (docs/hardware/storage.md)
#define BUNDLE_OFFSET 0xf00000      // the kmon bundle in flash0.splash

#define MON_MAGIC   0x4b4d4f4e      // 'KMON' in x1 at a program's entry; x2 = resume address

// Software reset (docs/hardware/system-blocks.md)
#define RESET_A     0xf0404304
#define RESET_B     0xf0404308

#define read32(addr)       (*(volatile uint32_t *)(uintptr_t)(addr))
#define write32(addr, val) (*(volatile uint32_t *)(uintptr_t)(addr) = (val))
