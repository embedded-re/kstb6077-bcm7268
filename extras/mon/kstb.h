// kstb.h - KSTB6077 addresses and the monitor's memory layout.
// Sources: hardware/registers.md and the docs it points to.

#include <stdint.h>

#define UART0       0xf040c000      // serial console, 16550, registers 4 bytes apart

// RAM layout (hardware/memory-map.md: 0x00001000-0x063fffff is free after go)
#define MON_ADDR    0x00100000      // the monitor (code, data, bss, page tables)
#define MON_MAX     0x00040000      //   at most 256 KB, checked by link.ld
#define DOOM_IMG    0x00140000      // KWAD image of doom.bin, loaded with the monitor (the bundle)
#define BUNDLE_END  0x00400000
#define DMA_BASE    0x00600000      // 2 MB, mapped non-cacheable: USB descriptors and buffers
#define NET_BUF     0x00700000      //   Ethernet frame buffers (net.c): 64 x 2 KB receive, 4 x 2 KB transmit
#define SCROLLBACK  0x00800000      // HDMI console scrollback (fbcon.c), 1024 lines x 114 = 114 KB
#define FAT_BUF     0x00820000      // FAT32 work buffers (../fat), 64 KB
#define HISTORY     0x00830000      // command line history (mon.c), 32 x 256 bytes = 8 KB
#define STACK       0x00840000      // the monitor's stack, 64 KB, top 0x00850000 (start.S)
#define HEAP_BASE   0x00f00000      // malloc (libc), 1 MB
#define HEAP_END    0x01000000
#define PROG_ADDR   0x01000000      // where programs run (doom.bin, kstb-run/build-c programs)
#define VIDEO_LISTS 0x7d9f8000      // our RDC display lists (video.c), 32 KB, non-cacheable, after the
#define VIDEO_LISTS_LEN 0x8000      //   second framebuffer 0x7d600000-0x7d9f47ff
#define AUDIO_RING  0x7da00000      // our audio ring buffer (audio_hw.c), 0.5 s, non-cacheable; used
#define AUDIO_RING_LEN 0x2ee00     //   only when BOLT's splash didn't start the audio DMA
#define WAD_ADDR    0x10000000      // the WAD's KWAD image (loaded by BOLT, or by `doom` itself)
#define WAD_OFFSET  0x100000        // where it is in flash0.splash (hardware/storage.md)
#define BUNDLE_OFFSET 0xf00000      // the kmon bundle in flash0.splash

#define MON_MAGIC   0x4b4d4f4e      // 'KMON' in x1 at a program's entry; x2 = resume address

// Software reset (hardware/system-blocks.md)
#define RESET_A     0xf0404304
#define RESET_B     0xf0404308

#define read32(addr)       (*(volatile uint32_t *)(uintptr_t)(addr))
#define write32(addr, val) (*(volatile uint32_t *)(uintptr_t)(addr) = (val))
