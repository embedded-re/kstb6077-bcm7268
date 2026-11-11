// slot.h - where kboot (the loader) and kmon keep the monitor's images in
// flash0.splash, and the boot state kept in AON SRAM. Shared by bootloader/kboot/ and extras/mon/.
// Layout: hardware/storage.md, "Layout of flash0.splash".

#include <stdint.h>

#define SPLASH_LBA      15239168u   // flash0.splash offset 0 (= SPLASH_FIRST in mmc.h)
#define SPLASH_SIZE     0x10cf600u  // 34,427 sectors

#define LOADER_OFF      0x080000u   // kboot.bin, raw; STARTUP loads it to KBOOT_ADDR
#define LOADER_MAX      0x10000u
#define KBOOT_ADDR      0x03000000u
#define GOLDEN_HDR      0xeffe00u   // header sector of the golden image
#define GOLDEN_OFF      0xf00000u   // golden kmon bundle, raw (what the old STARTUP loads)
#define GOLDEN_MAX      0xe0000u
#define SLOTA_HDR       0xfe0000u   // header sector of slot A (updates)
#define SLOTA_OFF       0xfe0200u
#define SLOTA_MAX       (SPLASH_SIZE - SLOTA_OFF)
#define IMAGE_ADDR      0x00100000u // where a bundle runs (MON_ADDR)

#define SLOT_MAGIC      0x544c534bu // 'KSLT'
struct slot_hdr {           // first 64 bytes of the header sector, the rest is zeros
    uint32_t magic;
    uint32_t version;       // 1
    uint32_t size;          // image bytes
    uint32_t crc;           // CRC32 (zlib) of the image
    uint32_t seq;           // +1 with every write, to tell images apart
    char     name[40];      // free text
    uint32_t hdr_crc;       // CRC32 of the 60 bytes before it
};

// Boot state at the end of AON SRAM, which survives software and watchdog
// resets (hardware/system-blocks.md). Valid only when the check word
// matches; after a power-on it is treated as empty.
#define BOOTST          0xf04105f0u
#define BOOTST_MAGIC    0x5453424bu // 'KBST'
#define BOOT_TRIES_MAX  3           // unconfirmed starts of slot A before golden is used
#define BOOT_WDT_S      30          // watchdog while slot A starts; kmon stops it

enum { BOOT_GOLDEN = 0, BOOT_A = 1 };
enum { WHY_A = 0, WHY_BUTTON, WHY_NO_A, WHY_A_BAD, WHY_A_FAILED };
#define BOOT_SEEN       0x10000u    // in how: kmon has reported (and confirmed) this start

// tries: starts of slot A since it was written or last confirmed.
// how:   slot started (BOOT_*) | why golden (WHY_*) << 8.
static inline int bootst_get(uint32_t *tries, uint32_t *how)
{
    volatile uint32_t *s = (volatile uint32_t *)(uintptr_t)BOOTST;
    if (s[0] != BOOTST_MAGIC || s[3] != ~(s[0] ^ s[1] ^ s[2]))
        return 0;
    *tries = s[1];
    *how = s[2];
    return 1;
}

static inline void bootst_set(uint32_t tries, uint32_t how)
{
    volatile uint32_t *s = (volatile uint32_t *)(uintptr_t)BOOTST;
    s[0] = BOOTST_MAGIC;
    s[1] = tries;
    s[2] = how;
    s[3] = ~(BOOTST_MAGIC ^ tries ^ how);
}

#define WDT_TIMEOUT     0xf040a6a8u // 27 MHz ticks
#define WDT_CMD         0xf040a6acu // start ff00, 00ff; stop ee00, 00ee
