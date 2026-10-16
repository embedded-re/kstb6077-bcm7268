// kboot.c - the loader BOLT's STARTUP runs: picks a kmon image in
// flash0.splash, checks it and starts it (AArch64, EL2, MMU off).
//
//   SW1 or SW4 held        -> golden
//   slot A valid, CRC ok,
//   fewer than 3 tries     -> slot A, tries + 1, watchdog armed (30 s);
//                             kmon confirms it: tries = 0, watchdog off
//   otherwise              -> golden (started even if its CRC doesn't match:
//                             it is the last resort)
//
// The state (tries, which image, why) is in AON SRAM (slot.h), which
// survives the watchdog reset; kmon's `slot` shows it. Layout and recovery:
// kboot/README.md.

#include <stdint.h>
#include "../mon/kstb.h"
#include "../mon/mmc.h"
#include "slot.h"

#define GPIO_AON_DATA 0xf0419c84        // AON GPIO bank 0 DATA (docs/hardware/gpio.md)
#define SW1     14                      // front standby button, active-low
#define SW4     7                       // recovery button, active-low
#define LED_RED   17                    // power LED, active-low
#define LED_GREEN 18

void jump_to(uint64_t addr);

// ---------------- output (UART0 only) ----------------

static void putc_(char c)
{
    while (!(read32(UART0 + 0x14) & 0x20))
        ;
    write32(UART0, c);
}

static void puts_(const char *s)
{
    for (; *s; s++) {
        if (*s == '\n')
            putc_('\r');
        putc_(*s);
    }
}

static void hex(uint32_t v)
{
    for (int i = 28; i >= 0; i -= 4)
        putc_("0123456789abcdef"[(v >> i) & 15]);
}

static void dec(uint32_t v)
{
    char b[11];
    int n = 0;
    do
        b[n++] = '0' + v % 10;
    while (v /= 10);
    while (n)
        putc_(b[--n]);
}

// mmc.c uses these
void *memset(void *d, int c, unsigned long n)
{
    uint8_t *p = d;
    while (n--)
        *p++ = c;
    return d;
}

void *memcpy(void *d, const void *s, unsigned long n)
{
    uint8_t *p = d;
    const uint8_t *q = s;
    while (n--)
        *p++ = *q++;
    return d;
}

// ---------------- CRC32 (zlib) ----------------

static uint32_t crc_table[256];

static uint32_t crc32(const uint8_t *p, uint32_t n)
{
    if (!crc_table[1])
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++)
                c = c & 1 ? 0xedb88320 ^ (c >> 1) : c >> 1;
            crc_table[i] = c;
        }
    uint32_t c = ~0u;
    while (n--)
        c = crc_table[(c ^ *p++) & 0xff] ^ (c >> 8);
    return ~c;
}

// ---------------- hardware ----------------

static void led(int green, int red)
{
    uint32_t v = read32(GPIO_AON_DATA);
    v = green ? v & ~(1u << LED_GREEN) : v | 1u << LED_GREEN;
    v = red ? v & ~(1u << LED_RED) : v | 1u << LED_RED;
    write32(GPIO_AON_DATA, v);
}

static uint64_t ms(void)
{
    uint64_t t;
    __asm__ volatile("isb; mrs %0, cntpct_el0" : "=r"(t));
    return t / 27000;
}

static void halt(const char *why)
{
    puts_("kboot: ");
    puts_(why);
    puts_("\nkboot: stopped. Ctrl-C at power-on gives BOLT> (tools/kstb-bolt); see kboot/README.md\n");
    led(0, 1);
    for (;;)
        __asm__ volatile("wfe");
}

// ---------------- images ----------------

static uint8_t sector[512] __attribute__((aligned(16)));

static int read_hdr(uint32_t off, uint32_t max, struct slot_hdr *h)
{
    if (mmc_read(SPLASH_LBA + off / 512, 1, sector))
        return 0;
    memcpy(h, sector, sizeof *h);
    return h->magic == SLOT_MAGIC && h->version == 1 && h->size && h->size <= max &&
           h->hdr_crc == crc32((const uint8_t *)h, 60);
}

static void show(const char *what, const struct slot_hdr *h)
{
    puts_(what);
    puts_(": seq ");
    dec(h->seq);
    puts_(", ");
    dec(h->size);
    puts_(" bytes, CRC ");
    hex(h->crc);
    if (h->name[0]) {
        char name[41];
        memcpy(name, h->name, 40);
        name[40] = 0;
        puts_(", \"");
        puts_(name);
        puts_("\"");
    }
    puts_("\n");
}

// Read an image to IMAGE_ADDR; 1 if it was read and (with crc) matches.
static int load(uint32_t off, uint32_t size, const uint32_t *crc)
{
    uint64_t t = ms();
    if (mmc_read(SPLASH_LBA + off / 512, (size + 511) / 512, (void *)(uintptr_t)IMAGE_ADDR)) {
        puts_("  eMMC read error\n");
        return 0;
    }
    if (!crc)
        return 1;
    uint32_t got = crc32((const uint8_t *)(uintptr_t)IMAGE_ADDR, size);
    puts_("  read and checked in ");
    dec(ms() - t);
    puts_(" ms: CRC ");
    hex(got);
    puts_(got == *crc ? " OK\n" : " MISMATCH\n");
    return got == *crc;
}

static void start(uint32_t tries, uint32_t how)
{
    bootst_set(tries, how);
    led(1, 0);
    puts_("kboot: starting at 00100000\n");
    jump_to(IMAGE_ADDR);
}

static const char *const why_text[] = {
    [WHY_BUTTON]   = "button held",
    [WHY_NO_A]     = "no valid image in slot A",
    [WHY_A_BAD]    = "slot A failed its check",
    [WHY_A_FAILED] = "slot A did not confirm 3 times",
};

void kboot_main(void)
{
    uint32_t tries = 0, how = 0;
    puts_("\nkboot: kmon loader\n");
    led(1, 1);                                  // orange while loading
    if (!bootst_get(&tries, &how))
        tries = 0;
    if (mmc_init())
        halt("eMMC not ready");

    struct slot_hdr g, a;
    int gok = read_hdr(GOLDEN_HDR, GOLDEN_MAX, &g);
    int aok = read_hdr(SLOTA_HDR, SLOTA_MAX, &a);
    uint32_t gpio = read32(GPIO_AON_DATA);
    int why = !(gpio & 1u << SW1) || !(gpio & 1u << SW4) ? WHY_BUTTON :
              !aok ? WHY_NO_A : tries >= BOOT_TRIES_MAX ? WHY_A_FAILED : WHY_A;

    if (why == WHY_A) {
        show("slot A", &a);
        if (load(SLOTA_OFF, a.size, &a.crc)) {
            puts_("kboot: slot A, try ");
            dec(tries + 1);
            puts_(" of ");
            dec(BOOT_TRIES_MAX);
            puts_("; watchdog ");
            dec(BOOT_WDT_S);
            puts_(" s until kmon confirms\n");
            write32(WDT_TIMEOUT, BOOT_WDT_S * 27000000u);
            write32(WDT_CMD, 0xff00);
            write32(WDT_CMD, 0x00ff);
            start(tries + 1, BOOT_A);
        }
        why = WHY_A_BAD;
    }

    puts_("kboot: golden (");
    puts_(why_text[why]);
    puts_(")\n");
    if (gok) {
        show("golden", &g);
        if (!load(GOLDEN_OFF, g.size, &g.crc))
            puts_("kboot: golden image doesn't match its header; starting it anyway\n");
    } else {
        puts_("golden: no header; loading ");
        dec(GOLDEN_MAX);
        puts_(" bytes unchecked\n");
        if (!load(GOLDEN_OFF, GOLDEN_MAX, 0))
            halt("can't read the golden image");
    }
    start(tries, BOOT_GOLDEN | why << 8);
}
