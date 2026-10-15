// dg_kstb.c - doomgeneric platform layer for the KSTB6077 (bare metal, EL2).
//
//   video   Doom's 320x200 8-bit screen -> palette -> RGB565, scaled x5 to
//           1600x1000 in the middle of the 1920x1080 HDMI framebuffer.
//           Two buffers, flipped at the frame boundary (docs/hardware/display.md).
//   timer   CNTPCT_EL0, 27 MHz.
//   input   UART0 (a raw terminal, e.g. `kstb -i`) and the IR remote (kbd1, NEC).
//           Neither reports key releases: a key counts as held while it repeats,
//           and is released when no repeat arrives for a while.
//   WAD     loaded by BOLT to WAD_ADDR as a KWAD image (docs/hardware/storage.md).

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "kstb.h"
#include "doomgeneric/doomgeneric/doomgeneric.h"
#include "doomgeneric/doomgeneric/doomkeys.h"
#include "doomgeneric/doomgeneric/i_video.h"

void file_register(const char *name, void *data, size_t size);
void uart_putc(char c);
void sound_mix(void);

// ---------------- timer ----------------

#define TICKS_PER_MS 27000

static inline uint64_t ticks(void)
{
    uint64_t t;
    __asm__ volatile("isb; mrs %0, cntpct_el0" : "=r"(t));
    return t;
}

uint32_t DG_GetTicksMs(void)
{
    return ticks() / TICKS_PER_MS;
}

void DG_SleepMs(uint32_t ms)
{
    uint64_t end = ticks() + (uint64_t)ms * TICKS_PER_MS;
    while (ticks() < end)
        ;
}

// ---------------- video ----------------

#define SCALE   5
#define OUT_W   (DOOMGENERIC_RESX * SCALE)          // 1600
#define OUT_H   (DOOMGENERIC_RESY * SCALE)          // 1000
#define OUT_X   ((FB_W - OUT_W) / 2)                // 160
#define OUT_Y   ((FB_H - OUT_H) / 2)                // 40

typedef uint64_t u128 __attribute__((vector_size(16)));

static uint16_t pal565[256];
static uint16_t row[OUT_W] __attribute__((aligned(64)));
static uintptr_t back = FB_B;                       // the buffer not on screen

static void fb_clear(uintptr_t fb)
{
    u128 zero = { 0, 0 };
    for (u128 *p = (u128 *)fb; p < (u128 *)(fb + FB_H * FB_PITCH); p++)
        *p = zero;
}

static void flip(void)
{
    uint32_t frame = read32(RDC_FRAME);
    write32(RDC_SURF0, back);
    write32(RDC_SURF1, back);
    while (read32(RDC_FRAME) == frame)              // wait until the new buffer is shown
        ;
    back = back == FB_A ? FB_B : FB_A;
}

void DG_Init(void)
{
    fb_clear(FB_A);
    fb_clear(FB_B);
}

void DG_DrawFrame(void)
{
    if (palette_changed) {
        for (int i = 0; i < 256; i++)
            pal565[i] = (colors[i].r >> 3) << 11 | (colors[i].g >> 2) << 5 | colors[i].b >> 3;
        palette_changed = false;
    }

    const uint8_t *src = (const uint8_t *)DG_ScreenBuffer;
    uintptr_t line = back + OUT_Y * FB_PITCH + OUT_X * 2;

    for (int y = 0; y < DOOMGENERIC_RESY; y++) {
        uint16_t *r = row;
        for (int x = 0; x < DOOMGENERIC_RESX; x++) {
            uint16_t c = pal565[*src++];
            r[0] = r[1] = r[2] = r[3] = r[4] = c;
            r += SCALE;
        }
        for (int k = 0; k < SCALE; k++, line += FB_PITCH) {   // 16-byte stores to the screen
            const u128 *s = (const u128 *)row;
            u128 *d = (u128 *)line;
            for (int i = 0; i < OUT_W * 2 / 16; i++)
                d[i] = s[i];
        }
    }
    sound_mix();                                    // also runs during wipes and loads
    flip();
}

void DG_SetWindowTitle(const char *title)
{
    printf("title: %s\n", title);
}

// ---------------- input ----------------

#define QUEUE 64

static struct { unsigned char key, pressed; } queue[QUEUE];
static unsigned q_head, q_tail;

static void post(unsigned char key, int pressed)
{
    if (q_head - q_tail < QUEUE) {
        queue[q_head % QUEUE].key = key;
        queue[q_head % QUEUE].pressed = pressed;
        q_head++;
    }
}

// One held key (or two, for the remote's OK) per input source.
struct held {
    unsigned char keys[2];
    int n;
    uint32_t code;          // what produced it (character or IR command)
    uint64_t release;       // CNTPCT time of the automatic release
};

static void held_release(struct held *h)
{
    for (int i = 0; i < h->n; i++)
        post(h->keys[i], 0);
    h->n = 0;
}

// A repeat of the held code only moves the release time; a new code
// releases the old keys first.
static void held_press(struct held *h, uint32_t code, const unsigned char *keys, int n, uint32_t hold_ms)
{
    if (h->n && h->code == code) {
        h->release = ticks() + (uint64_t)hold_ms * TICKS_PER_MS;
        return;
    }
    held_release(h);
    for (int i = 0; i < n; i++) {
        h->keys[i] = keys[i];
        post(keys[i], 1);
    }
    h->n = n;
    h->code = code;
    h->release = ticks() + (uint64_t)hold_ms * TICKS_PER_MS;
}

static void held_check(struct held *h)
{
    if (h->n && ticks() >= h->release)
        held_release(h);
}

// UART: the terminal sends a key once, then repeats it after ~250-500 ms
// while it is held, so a held key is released in between (no fix possible
// without key-up events).
#define UART_HOLD_MS 200

static struct held uart_held;

static int uart_getc(uint32_t wait_ms)
{
    uint64_t end = ticks() + (uint64_t)wait_ms * TICKS_PER_MS;
    do {
        if (read32(UART0 + 0x14) & 1)               // LSR bit 0: byte received
            return read32(UART0) & 0xff;
    } while (ticks() < end);
    return -1;
}

static int uart_key(int c)
{
    switch (c) {
    case '\r': case '\n': return KEY_ENTER;
    case '\t': return KEY_TAB;
    case 0x7f: case 0x08: return KEY_BACKSPACE;
    case ' ': return KEY_USE;
    case 'z': return KEY_FIRE;
    case ',': return KEY_STRAFE_L;
    case '.': return KEY_STRAFE_R;
    case 0x1b: {                                    // Escape, or an escape sequence
        int c1 = uart_getc(3);                      // the rest arrives within 1 ms at 115200
        if (c1 < 0)
            return KEY_ESCAPE;
        int c2 = uart_getc(3);
        if (c1 == 'O' && c2 >= 'P' && c2 <= 'S')   // ESC O P..S = F1..F4
            return KEY_F1 + (c2 - 'P');
        if (c1 != '[')
            return 0;
        switch (c2) {
        case 'A': return KEY_UPARROW;
        case 'B': return KEY_DOWNARROW;
        case 'C': return KEY_RIGHTARROW;
        case 'D': return KEY_LEFTARROW;
        }
        int n = 0;                                  // ESC [ n ~
        while (c2 >= '0' && c2 <= '9') {
            n = n * 10 + c2 - '0';
            c2 = uart_getc(3);
        }
        static const unsigned char fkeys[] = {
            [11] = KEY_F1, [12] = KEY_F2, [13] = KEY_F3, [14] = KEY_F4, [15] = KEY_F5,
            [17] = KEY_F6, [18] = KEY_F7, [19] = KEY_F8, [20] = KEY_F9, [21] = KEY_F10,
            [23] = KEY_F11, [24] = KEY_F12,
        };
        return c2 == '~' && n < (int)sizeof fkeys ? fkeys[n] : 0;
    }
    }
    if (c >= 'A' && c <= 'Z')
        return c + 32;
    return c >= 0x20 && c < 0x7f ? c : 0;
}

static void uart_poll(void)
{
    int c;
    while ((c = uart_getc(0)) >= 0) {
        unsigned char k = uart_key(c);
        if (k)
            held_press(&uart_held, k, &k, 1, UART_HOLD_MS);
    }
    held_check(&uart_held);
}

// IR remote: NEC frames repeat every ~108 ms while a key is held
// (docs/hardware/ir.md), so 150 ms without a frame = released.
#define IR_HOLD_MS 150

static struct held ir_held;

// (index, value) pairs from nexus.ko necParam, as in docs/evidence/ir/ir_nec_probe.s
static const uint16_t nec_params[][2] = {
    {0, 0x0ff}, {1, 0x000}, {2, 0x012}, {3, 0x384}, {4, 0x384}, {5, 0x1c2},
    {6, 0x0e1}, {7, 0x000}, {8, 0x000}, {9, 0x000}, {10, 0x000}, {11, 0x80f},
    {12, 0xc32}, {13, 0x000}, {14, 0x071}, {15, 0x071}, {16, 0x10d}, {17, 0x032},
    {18, 0x830}, {19, 0x031}, {20, 0x016}, {21, 0x006}, {22, 0x258}, {23, 0x118},
    {25, 0x000}, {26, 0x000}, {27, 0x000},
};

static void ir_init(void)
{
    write32(IR_BASE + 0x00, 0);                                 // STATUS
    write32(IR_BASE + 0x14, read32(IR_BASE + 0x14) | 0x20);     // CMD: interrupt enable
    write32(IR_BASE + 0x14, read32(IR_BASE + 0x14) | 0x10);     // CMD: CIR decoder on
    write32(IR_BASE + 0x08, 0);                                 // FILTER1
    for (unsigned i = 0; i < sizeof nec_params / sizeof nec_params[0]; i++) {
        write32(IR_BASE + 0x18, nec_params[i][0]);              // CIR_ADDR
        write32(IR_BASE + 0x1c, nec_params[i][1]);              // CIR_DATA
    }
}

// Kaon remote, custom code 0x0820: command byte (bits 16-23 of DATA0) -> Doom keys
static int ir_keys(unsigned cmd, unsigned char *k)
{
    switch (cmd) {
    case 0x12: k[0] = KEY_UPARROW; return 1;
    case 0x13: k[0] = KEY_DOWNARROW; return 1;
    case 0x14: k[0] = KEY_LEFTARROW; return 1;
    case 0x16: k[0] = KEY_RIGHTARROW; return 1;
    case 0x15: k[0] = KEY_FIRE; k[1] = KEY_ENTER; return 2;    // OK: fire in the game, select in menus
    case 0x10: case 0x11: k[0] = KEY_ESCAPE; return 1;          // BACK, MENU
    case 0x18: k[0] = KEY_USE; return 1;                        // RED: open doors, switches
    case 0x19: k[0] = 'y'; return 1;                            // GREEN: yes
    case 0x1a: k[0] = KEY_TAB; return 1;                        // YELLOW: automap
    case 0x1b: k[0] = 'n'; return 1;                            // BLUE: no
    case 0x02: k[0] = KEY_STRAFE_R; return 1;                   // VOL+
    case 0x03: k[0] = KEY_STRAFE_L; return 1;                   // VOL-
    case 0x43: k[0] = KEY_PAUSE; return 1;                      // PLAY/PAUSE
    case 0x1c: k[0] = '1'; return 1;
    case 0x1d: k[0] = '2'; return 1;
    case 0x1e: k[0] = '3'; return 1;
    case 0x40: k[0] = '4'; return 1;
    case 0x41: k[0] = '5'; return 1;
    case 0x42: k[0] = '6'; return 1;
    case 0x44: k[0] = '7'; return 1;
    }
    return 0;
}

static void ir_poll(void)
{
    uint32_t status = read32(IR_BASE + 0x00);
    if (status & 1) {
        uint32_t code = read32(IR_BASE + 0x10);                 // DATA0
        write32(IR_BASE + 0x00, status & ~1u);                  // clear "code ready"
        unsigned char k[2];
        int n;
        if ((code & 0xffff) == 0x0820 && (n = ir_keys((code >> 16) & 0xff, k)))
            held_press(&ir_held, code, k, n, IR_HOLD_MS);
    }
    held_check(&ir_held);
}

int DG_GetKey(int *pressed, unsigned char *key)
{
    uart_poll();
    ir_poll();
    if (q_tail == q_head)
        return 0;
    *key = queue[q_tail % QUEUE].key;
    *pressed = queue[q_tail % QUEUE].pressed;
    q_tail++;
    return 1;
}

// ---------------- WAD ----------------

struct kwad {               // header written by tools/make-wad-image
    uint32_t magic;         // 'KWAD'
    uint32_t version;
    uint32_t size;
    uint32_t crc;
    uint32_t offset;
    char name[32];
};

static uint32_t crc32(const uint8_t *p, size_t n)
{
    static uint32_t table[256];
    if (table[1] == 0)
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++)
                c = c & 1 ? 0xedb88320 ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
    uint32_t c = 0xffffffff;
    while (n--)
        c = table[(c ^ *p++) & 0xff] ^ (c >> 8);
    return ~c;
}

static const char *wad_register(void)
{
    static char name[33];
    const struct kwad *h = (const struct kwad *)WAD_ADDR;
    if (h->magic != 0x4441574b) {
        printf("no KWAD image at 0x%x (magic %08x). Load it first:\n"
               "  load -raw -rawfs -offset=0x100000 -addr=0x%x flash0.splash\n",
               WAD_ADDR, h->magic, WAD_ADDR);
        return NULL;
    }
    memcpy(name, h->name, 32);
    const uint8_t *data = (const uint8_t *)WAD_ADDR + h->offset;
    uint64_t t = ticks();
    uint32_t crc = crc32(data, h->size);
    printf("WAD %s: %u bytes at %p, CRC %08x (%s, %u ms)\n", name, h->size, data, crc,
           crc == h->crc ? "ok" : "BAD", (unsigned)((ticks() - t) / TICKS_PER_MS));
    if (crc != h->crc)
        return NULL;
    file_register(name, (void *)data, h->size);
    return name;
}

// ---------------- missing M_EPI4 ----------------
//
// The stored WAD has the episode 4 maps (so Doom runs as Ultimate Doom) but
// not the menu graphic M_EPI4, and the episode menu stops with
// "W_GetNumForName: M_EPI4 not found!". This builds the missing graphic from
// the WAD's own small font (STCFNnnn) into a one-lump PWAD in RAM, which
// main() adds with -file.

static uint32_t rd32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static uint16_t rd16(const uint8_t *p) { uint16_t v; memcpy(&v, p, 2); return v; }

static const uint8_t *wad_lump(const uint8_t *wad, const char *name)
{
    uint32_t n = rd32(wad + 4);
    const uint8_t *dir = wad + rd32(wad + 8);
    for (uint32_t i = n; i-- > 0; )                 // last one wins, as in Doom
        if (strncmp((const char *)dir + 16 * i + 8, name, 8) == 0)
            return wad + rd32(dir + 16 * i);
    return NULL;
}

#define EPI_SCALE 2
#define EPI_W     240
#define EPI_H     16

static const char *epi4_wad(const uint8_t *wad)
{
    if (wad_lump(wad, "M_EPI4"))
        return NULL;

    static int16_t canvas[EPI_H][EPI_W];           // palette index, -1 = transparent
    memset(canvas, 0xff, sizeof canvas);
    int x = 0;
    for (const char *s = "THY FLESH CONSUMED"; *s; s++) {
        char name[9];
        snprintf(name, sizeof name, "STCFN%.3d", *s);
        const uint8_t *g = *s == ' ' ? NULL : wad_lump(wad, name);
        if (g == NULL) {
            x += 4 * EPI_SCALE;
            continue;
        }
        int w = rd16(g);
        for (int c = 0; c < w; c++) {               // decode the glyph's columns
            const uint8_t *p = g + rd32(g + 8 + 4 * c);
            for (; p[0] != 0xff; p += p[1] + 4)
                for (int i = 0; i < p[1]; i++)
                    for (int dy = 0; dy < EPI_SCALE; dy++)
                        for (int dx = 0; dx < EPI_SCALE; dx++) {
                            int cx = x + c * EPI_SCALE + dx, cy = (p[0] + i) * EPI_SCALE + dy;
                            if (cx < EPI_W && cy < EPI_H)
                                canvas[cy][cx] = p[3 + i];
                        }
        }
        x += w * EPI_SCALE;
    }
    int width = x < EPI_W ? x : EPI_W;

    // PWAD: header, patch (width, height, offsets, column offsets, posts), directory
    static uint8_t out[16 * 1024];
    uint8_t *patch = out + 12, *p = patch + 8 + 4 * width;
    uint16_t hdr[4] = { width, EPI_H, 0, 0 };
    memcpy(patch, hdr, 8);
    for (int c = 0; c < width; c++) {
        uint32_t ofs = p - patch;
        memcpy(patch + 8 + 4 * c, &ofs, 4);
        for (int y = 0; y < EPI_H; ) {
            if (canvas[y][c] < 0) {
                y++;
                continue;
            }
            int len = 0;
            while (y + len < EPI_H && canvas[y + len][c] >= 0)
                len++;
            *p++ = y;
            *p++ = len;
            *p++ = 0;                               // unused byte before the pixels
            for (int i = 0; i < len; i++)
                *p++ = canvas[y + i][c];
            *p++ = 0;                               // unused byte after them
            y += len;
        }
        *p++ = 0xff;
    }
    uint32_t size = p - patch;
    while ((p - out) & 3)
        *p++ = 0;
    uint32_t wadhdr[3] = { 0x44415750, 1, p - out };   // 'PWAD', 1 lump, directory offset
    memcpy(out, wadhdr, 12);
    uint32_t entry[2] = { 12, size };
    memcpy(p, entry, 8);
    memcpy(p + 8, "M_EPI4\0\0", 8);
    p += 16;

    file_register("kstbfix.wad", out, p - out);
    printf("M_EPI4 missing from the WAD: made one (%dx%d) in kstbfix.wad\n", width, EPI_H);
    return "kstbfix.wad";
}

// ---------------- start ----------------

void exception(uint64_t esr, uint64_t elr, uint64_t far)
{
    printf("\nEXCEPTION ESR %08lx ELR %016lx FAR %016lx\n", esr, elr, far);
    for (;;)
        __asm__ volatile("wfe");
}

int main(void)
{
    printf("\nKSTB6077 doomgeneric, EL2 bare metal\n");
    const char *wad = wad_register();
    if (wad == NULL)
        return 1;
    ir_init();

    static char *argv[] = { "doom", "-iwad", NULL, "-mb", "32", NULL, NULL, NULL };
    int argc = 5;
    argv[2] = (char *)wad;
    const char *fix = epi4_wad((const uint8_t *)WAD_ADDR + ((const struct kwad *)WAD_ADDR)->offset);
    if (fix) {
        argv[argc++] = "-file";
        argv[argc++] = (char *)fix;
    }
    doomgeneric_Create(argc, argv);
    for (;;)
        doomgeneric_Tick();
}
