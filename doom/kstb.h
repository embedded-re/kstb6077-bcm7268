// kstb.h - KSTB6077 addresses and the memory layout of the Doom port.
// Sources: docs/hardware/{uart,display,ir,storage,memory-map}.md

#define UART0       0xf040c000      // serial console, 16550, registers 4 bytes apart

// RAM layout (all free after `go -64`, docs/hardware/memory-map.md)
#define LOAD_ADDR   0x01000000      // this program (code, data, bss, stack, page tables)
#define WAD_ADDR    0x10000000      // KWAD image, loaded by BOLT before `go -64`
#define HEAP_BASE   0x20000000      // malloc
#define HEAP_END    0x40000000

// Display (docs/hardware/display.md): RGB565, pitch 3840, flip at the next frame
#define FB_A        0x7db0b700      // BOLT's splash framebuffer
#define FB_B        0x7d600000      // second buffer
#define FB_W        1920
#define FB_H        1080
#define FB_PITCH    3840
#define RDC_FRAME   0xf0603484      // frame counter, one step per frame (59.94 Hz)
#define RDC_SURF0   0xf0603488      // surface address, write both to flip
#define RDC_SURF1   0xf060348c

// IR receiver channel kbd1 (docs/hardware/ir.md)
#define IR_BASE     0xf0419900

// Software reset (docs/hardware/system-blocks.md)
#define RESET_A     0xf0404304
#define RESET_B     0xf0404308

#define read32(addr)       (*(volatile unsigned int *)(uintptr_t)(addr))
#define write32(addr, val) (*(volatile unsigned int *)(uintptr_t)(addr) = (val))
