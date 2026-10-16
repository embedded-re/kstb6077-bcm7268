// video.c - bring up HDMI video ourselves, the way BOLT's boot splash does
// (docs/hardware/display.md, "SplashData and the script runner").
//
// BOLT always runs before us, so its RAM image (with SplashData, the display
// script compiled into it) is there even when its splash was skipped
// (env NO_SPLASH). kmon copies the script out of BOLT's RAM at start-up
// (video_keep_script) into its own SCRIPT_COPY, so a program that overwrites
// BOLT's RAM doesn't take the display set-up with it. We replay BOLT's
// runner 0x07010374:
//   1. write the register table, in order, no delays;
//   2. point each display's flip registers at the framebuffer;
//   3. copy each list entry's RDC lists into our own memory, chain them with
//      18-word tails (each points the descriptor at the next list), and start
//      the first list; the last list has no tail and then runs every frame;
//   4. arm the even descriptors (write 1 to descriptor + 0x20).
// The framebuffer is BOLT's (0x7db0b700, what fbcon and Doom draw into); the
// lists live in our own memory, VIDEO_LISTS, not in BOLT's 0x7db08000.

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "kstb.h"
#include "mon.h"
#include "video.h"

#define SPLASHDATA  0x07056f30      // in BOLT's RAM image
#define SD_CHECK    0x07054318      // register table .. end of the last list's words (0x07059347)
#define SD_CHECK_LEN 0x5030
#define SD_CRC      0x0ed66aea      // of SD_CHECK, from the BOLT RAM image of 2026-10-16

#define HDMI_HPD    0xf06fa1a8      // bit 0: hot-plug detect, 1 = a TV is connected (tested by replugging)
#define HDMI_RXSENSE 0xf06fa86c     // 3 = the TV's receiver terminates the lines, 0 = not (tested)
#define FRAME_CNT   0xf0603484      // counts up once per frame while the display runs
#define RDC_DESC    0xf0604000      // descriptor d: + d * 0x40
#define RDC_LOCK    0xf0605000      // + (d / 2) * 8: 0 while a descriptor pair is changed
#define FB          0x7db0b700      // BOLT's splash framebuffer (fbcon.c)

#define TAIL_MAX    18              // words added after a list for the chain to the next one

struct sd_display { uint32_t surface, format, flip0, flip1; };
struct sd_list    { uint32_t trigger, desc, count, offsets, words; };

// Pointers inside SplashData are BOLT addresses in SD_CHECK; the copy holds
// the same bytes at SCRIPT_COPY.
static int words_in(uint32_t a, uint32_t n)     // n words at a lie in the script
{
    return a >= SD_CHECK && a + n * 4 <= SD_CHECK + SD_CHECK_LEN && !(a & 3);
}

static const void *at(uint32_t a)               // a BOLT script address in our copy
{
    return (const void *)(uintptr_t)(a - SD_CHECK + SCRIPT_COPY);
}

static int copy_ok(void)
{
    return crc32((const uint8_t *)SCRIPT_COPY, SD_CHECK_LEN) == SD_CRC;
}

// Keep a copy of the script, if BOLT's RAM still holds it. 0 = copy good.
int video_keep_script(void)
{
    if (copy_ok())
        return 0;
    if (crc32((const uint8_t *)SD_CHECK, SD_CHECK_LEN) != SD_CRC)
        return -1;
    memcpy((void *)SCRIPT_COPY, (const void *)SD_CHECK, SD_CHECK_LEN);
    return copy_ok() ? 0 : -1;
}

static inline uint32_t reg(uint32_t bus)        // table addresses are bus addresses
{
    return bus | 0xd0000000;
}

// BOLT's 64-bit store (strd, high word 0), as two 32-bit writes
static inline void write64(uint32_t addr, uint32_t v)
{
    write32(addr, v);
    write32(addr + 4, 0);
}

static uint64_t now_us(void)
{
    uint64_t t;
    __asm__ volatile("isb; mrs %0, cntpct_el0" : "=r"(t));
    return t / 27;
}

// 1 when the frame counter moves (the display runs), 0 when not, -1 when it
// can't be read.
int video_running(void)
{
    uint64_t a, b;
    if (safe_read(FRAME_CNT, 4, &a))
        return -1;
    uint64_t end = now_us() + 50000;            // three frames
    do {
        if (safe_read(FRAME_CNT, 4, &b))
            return -1;
        if (b != a)
            return 1;
    } while (now_us() < end);
    return 0;
}

void video_status(void)
{
    int r = video_running();
    uint64_t v;
    printf("video: display %s", r < 0 ? "registers unreadable" : r ? "running" : "not running");
    if (r >= 0) {
        safe_read(FRAME_CNT, 4, &v);
        printf(", frame counter %08lx", v);
    }
    printf("\n");
    uint64_t rx = 0;
    int hpd = video_hdmi_connected();
    safe_read(HDMI_RXSENSE, 4, &rx);
    printf("  HDMI: %s (hot-plug %08x bit 0), receiver sense %lx\n",
           hpd < 0 ? "unreadable" : hpd ? "TV connected" : "no TV", HDMI_HPD, rx);
    for (int d = 0; d < 2; d++) {
        uint64_t addr = 0, cnt = 0, cfg = 0;
        safe_read(RDC_DESC + d * 0x40, 4, &addr);
        safe_read(RDC_DESC + d * 0x40 + 0x08, 4, &cnt);
        safe_read(RDC_DESC + d * 0x40 + 0x10, 4, &cfg);
        printf("  RDC descriptor %d: list %08lx, count %lx, config %08lx\n", d, addr, cnt, cfg);
    }
    uint32_t crc = crc32((const uint8_t *)SD_CHECK, SD_CHECK_LEN);
    printf("  SplashData in BOLT's RAM: CRC %08x (%s); our copy at %08x: %s\n", crc,
           crc == SD_CRC ? "as decoded" : "DIFFERENT", SCRIPT_COPY, copy_ok() ? "good" : "missing");
}

// HDMI cable: 1 = TV connected, 0 = not, -1 = registers unreadable
int video_hdmi_connected(void)
{
    uint64_t v;
    return safe_read(HDMI_HPD, 4, &v) ? -1 : (int)(v & 1);
}

// Called from the console's idle loop: print a line when the TV comes or goes.
void video_hdmi_poll(void)
{
    static int last = -2;
    static uint64_t next;
    uint64_t t = now_us();
    if (t < next)
        return;
    next = t + 200000;
    int c = video_hdmi_connected();
    if (last != -2 && c != last && c >= 0)
        printf("\nhdmi: TV %s\n", c ? "connected" : "disconnected");
    last = c;
}

// Build and start the lists of one list entry, as BOLT's runner does.
// *mem: where the next list goes (advanced); lim: end of our list memory.
static int start_lists(const struct sd_list *e, uint32_t format, uint32_t *mem, uint32_t lim)
{
    uint32_t d = e->desc, n = e->count;
    const uint32_t *off = at(e->offsets);

    // The format check at 0x07010406: with these formats, descriptors -1..1
    // get the lock bracket (RDC_LOCK = 1) at the end of each tail.
    int fmt = format == 0x1c || (format >= 0x29 && format <= 0x2d) ||
              (format >= 0x16 && format <= 0x19) || (format >= 0x22 && format <= 0x25) ||
              format == 0x1e || format == 0x1f || format == 0x3e || format == 0x7d || format == 0x87;
    int lock_end = fmt && d + 1 <= 2;
    int lock_start = 1;
    if (!lock_end && (d & 1)) {
        lock_end = 1;
        lock_start = 0;
    }
    uint32_t extra = 12 + 3 * lock_start + 3 * lock_end;

    uint32_t desc = RDC_DESC + d * 0x40, lock = RDC_LOCK + (d / 2) * 8;
    uint32_t config = 0x06000000 | e->trigger << 16 | (d / 2);
    uint32_t list[16];
    if (n == 0 || n > 16 || !words_in(e->offsets, n + 1))
        return -1;
    for (uint32_t k = 0; k < n; k++) {
        uint32_t len = off[k + 1] - off[k];
        if (off[k + 1] < off[k] || !words_in(e->words + off[k] * 4, len))
            return -1;
        uint32_t a = (*mem + 31) & ~31u;
        if (a + (len + extra) * 4 > lim)
            return -2;
        list[k] = a;
        *mem = a + (len + extra) * 4;
        memcpy((void *)(uintptr_t)a, at(e->words + off[k] * 4), len * 4);
    }
    // The tail at the end of list k-1 starts list k
    for (uint32_t k = 1; k < n; k++) {
        uint32_t prev = off[k] - off[k - 1], len = off[k + 1] - off[k];
        uint32_t count = (k == n - 1 ? len : len + extra) - 1;
        volatile uint32_t *t = (volatile uint32_t *)(uintptr_t)(list[k - 1] + prev * 4);
        if (lock_start)
            *t++ = 0x01000000, *t++ = lock, *t++ = 0;
        *t++ = 0x01000000, *t++ = desc, *t++ = list[k];
        *t++ = 0x01000000, *t++ = desc + 0x10, *t++ = config;
        *t++ = 0x01000000, *t++ = desc + 0x08, *t++ = count;
        *t++ = 0x01000000, *t++ = desc + 0x18, *t++ = count;
        if (lock_end)
            *t++ = 0x01000000, *t++ = lock, *t++ = 1;
    }
    __asm__ volatile("dsb sy" ::: "memory");     // lists are in non-cacheable RAM

    uint32_t count0 = off[1] - off[0] + (n > 1 ? extra : 0) - 1;
    if (lock_start)
        write32(lock, 0);
    write32(desc + 0x10, config);
    write32(desc + 0x08, count0);
    write32(desc + 0x18, count0);
    write64(desc, list[0]);
    if (lock_end)
        write32(lock, 1);
    printf("video: descriptor %u (trigger %02x): %u lists at %08x-%08x\n",
           d, e->trigger, n, list[0], *mem - 1);
    return 0;
}

int video_init(int force)
{
    if (video_keep_script()) {
        printf("video: no good copy of BOLT's display script (BOLT's RAM: CRC %08x, not %08x)\n",
               crc32((const uint8_t *)SD_CHECK, SD_CHECK_LEN), SD_CRC);
        return -1;
    }
    const uint32_t *sd = at(SPLASHDATA);
    int r = video_running();
    if (r != 0 && !force) {
        printf("video: the display %s; not touching it ('video! init' to do it anyway)\n",
               r > 0 ? "already runs" : "registers can't be read");
        return -1;
    }
    uint32_t ndisp = sd[4], nlist = sd[6], nreg = sd[8];
    const struct sd_display *disp = at(sd[5]);
    const struct sd_list *lists = at(sd[7]);
    const uint32_t *table = at(sd[9]);
    if (ndisp < 1 || ndisp > 4 || nlist > 16 || nreg > 4096 || !words_in(sd[5], ndisp * 4) ||
        !words_in(sd[7], nlist * 5) || !words_in(sd[9], nreg * 2)) {
        printf("video: SplashData doesn't look right (%u displays, %u lists, %u registers)\n",
               ndisp, nlist, nreg);
        return -1;
    }

    uint64_t t0 = now_us();
    for (uint32_t i = 0; i < nreg; i++)
        write32(reg(table[2 * i]), table[2 * i + 1]);
    uint64_t t1 = now_us();
    for (uint32_t i = 0; i < ndisp; i++) {
        write64(reg(disp[i].flip0), FB);
        if (disp[i].flip1)
            write64(reg(disp[i].flip1), FB);
    }
    printf("video: %u register writes in %lu us, format %u, framebuffer %08x\n",
           nreg, t1 - t0, disp[0].format, FB);

    uint32_t mem = VIDEO_LISTS;
    for (uint32_t i = 0; i < nlist; i++) {
        if (lists[i].trigger == 0xffffffff)
            continue;
        int e = start_lists(&lists[i], disp[0].format, &mem, VIDEO_LISTS + VIDEO_LISTS_LEN);
        if (e) {
            printf("video: list entry %u: %s\n", i, e == -2 ? "no room" : "bad offsets");
            return -1;
        }
    }
    for (uint32_t i = 0; i < nlist; i++)
        if (lists[i].trigger != 0xffffffff && !(lists[i].desc & 1))
            write32(RDC_DESC + lists[i].desc * 0x40 + 0x20, 1);

    uint64_t end = now_us() + 200000;           // the first frames take a moment
    while ((r = video_running()) == 0 && now_us() < end)
        ;
    printf("video: display %s\n", r > 0 ? "running (frame counter moves)" : "NOT running");
    return r > 0 ? 0 : -1;
}
