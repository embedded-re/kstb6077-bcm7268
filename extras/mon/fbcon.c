// fbcon.c - text console on the HDMI framebuffer (hardware/display.md).
//
// Draws into BOLT's splash framebuffer (RGB565, 1920x1080, pitch 3840; the
// display is set up by BOLT's boot splash) and makes it the visible surface
// with the flip registers. Everything kmon prints goes here as well as to the
// UART (con_write, called from libc's output path).
//
// The text grid keeps a margin for TVs that overscan. Glyphs are anti-aliased
// (4-bit coverage, font.h from tools/make-font) and blended between two
// colours with a 16-entry table. Understands what the line editor sends:
// \r \n \b \t, ESC[K (erase to end of line), ESC[nD / ESC[nC (cursor left /
// right); other escape sequences are swallowed.
//
// Scrollback: the last SB_LINES lines stay in a ring in free RAM
// (SCROLLBACK, kstb.h); con_scroll() moves the view (kmon binds it to
// Alt+Up/Down and Shift+PgUp/PgDn), and any new output returns to the bottom.

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "kstb.h"
#include "mon.h"
#include "font.h"

#define FB          0x7db0b700          // BOLT's splash framebuffer
#define FB_W        1920
#define FB_H        1080
#define PITCH       1920                // in pixels
#define FLIP0       0xf0603488          // surface address, applied at the next frame
#define FLIP1       0xf060348c

#define MARGIN_X    48                  // overscan margins
#define MARGIN_Y    28
#define COLS        ((FB_W - 2 * MARGIN_X) / FONT_W)
#define ROWS        ((FB_H - 2 * MARGIN_Y) / FONT_H)
#define ORG_X       ((FB_W - COLS * FONT_W) / 2)
#define ORG_Y       ((FB_H - ROWS * FONT_H) / 2)

#define BG          0x10a3              // dark blue-grey
#define FG          0xd6ba              // light grey

static int on;
static int cx, cy;                      // cursor cell
static int cursor_shown;
static int esc, esc_n;                  // escape parser: 0 none, 1 after ESC, 2 in ESC[
static uint16_t shade[16];              // coverage -> colour
#define SB_LINES    1024
static char (*const sb)[COLS] = (char (*)[COLS])SCROLLBACK;
static uint64_t head;                   // ring line shown in screen row 0 at the bottom
static int back;                        // lines scrolled back from the bottom
#define LINE(n)     sb[(n) % SB_LINES]
#define TEXT(row)   LINE(head + (row))
static uint64_t scroll_ticks;           // last scroll, CNTPCT ticks

static volatile uint16_t *const fb = (volatile uint16_t *)FB;

static uint16_t mix(uint16_t a, uint16_t b, int t)     // t/15 of b over a
{
    int r = ((a >> 11) * (15 - t) + (b >> 11) * t) / 15;
    int g = (((a >> 5) & 63) * (15 - t) + ((b >> 5) & 63) * t) / 15;
    int bl = ((a & 31) * (15 - t) + (b & 31) * t) / 15;
    return r << 11 | g << 5 | bl;
}

static void fill(int x, int y, int w, int h, uint16_t c)
{
    uint64_t c4 = c * 0x0001000100010001ull;
    for (int j = 0; j < h; j++) {
        volatile uint16_t *p = fb + (y + j) * PITCH + x;
        int i = 0;
        for (; i < w && ((uintptr_t)(p + i) & 7); i++)
            p[i] = c;
        for (; i + 4 <= w; i += 4)
            *(volatile uint64_t *)(p + i) = c4;
        for (; i < w; i++)
            p[i] = c;
    }
}

static void draw_cell(int col, int row, char ch, int inverse)
{
    volatile uint16_t *p = fb + (ORG_Y + row * FONT_H) * PITCH + ORG_X + col * FONT_W;
    if (ch < 0x20 || ch > 0x7e)
        ch = ' ';
    const unsigned char *g = font_glyphs[ch - 0x20];
    for (int y = 0; y < FONT_H; y++, p += PITCH) {
        for (int x = 0; x < FONT_W; x += 4) {   // 4 pixels = one 64-bit store
            uint8_t b0 = *g++, b1 = *g++;
            int t[4] = { b0 & 15, b0 >> 4, b1 & 15, b1 >> 4 };
            uint64_t v = 0;
            for (int k = 0; k < 4; k++)
                v |= (uint64_t)shade[inverse ? 15 - t[k] : t[k]] << (16 * k);
            *(volatile uint64_t *)(p + x) = v;
        }
    }
}

static void cursor(int show)
{
    if (show == cursor_shown || !on || back)
        return;
    draw_cell(cx, cy, TEXT(cy)[cx], show);
    cursor_shown = show;
}

// Draw every row from the ring, as seen from `back` lines up; while scrolled
// back, a marker in the top right corner says how far.
static void redraw(void)
{
    for (int row = 0; row < ROWS; row++)
        for (int col = 0; col < COLS; col++)
            draw_cell(col, row, LINE(head + row - back)[col], 0);
    if (back) {
        char mark[24];
        int n = snprintf(mark, sizeof mark, " scrolled back %d ", back);
        for (int i = 0; i < n; i++)
            draw_cell(COLS - n + i, 0, mark[i], 1);
    }
    cursor_shown = 0;
}

// The framebuffer is uncached, so moving pixels by reading them back is slow
// (98 ms a scroll); redrawing the cells only writes (about 8 ms).
static void scroll(void)
{
    uint64_t t0;
    __asm__ volatile("isb; mrs %0, cntpct_el0" : "=r"(t0));
    head++;
    memset(TEXT(ROWS - 1), ' ', COLS);
    redraw();
    uint64_t t1;
    __asm__ volatile("isb; mrs %0, cntpct_el0" : "=r"(t1));
    scroll_ticks = t1 - t0;
}

static void newline(void)
{
    cx = 0;
    if (++cy == ROWS) {
        scroll();
        cy = ROWS - 1;
    }
}

static void put(char c)
{
    if (esc == 1) {
        esc = c == '[' ? 2 : 0;
        esc_n = 0;
        return;
    }
    if (esc == 2) {
        if (c >= '0' && c <= '9') {
            esc_n = esc_n * 10 + (c - '0');
            return;
        }
        int n = esc_n ? esc_n : 1;
        if (c == 'K') {                         // erase to end of line
            for (int x = cx; x < COLS; x++)
                TEXT(cy)[x] = ' ';
            fill(ORG_X + cx * FONT_W, ORG_Y + cy * FONT_H, (COLS - cx) * FONT_W, FONT_H, BG);
        } else if (c == 'D') {
            cx = cx > n ? cx - n : 0;
        } else if (c == 'C') {
            cx = cx + n < COLS ? cx + n : COLS - 1;
        } else if (c >= 0x40 && c <= 0x7e) {    // any other final byte: ignore
        } else {
            return;                             // ';' and such: still inside
        }
        esc = 0;
        return;
    }
    switch (c) {
    case 0x1b: esc = 1; return;
    case '\r': cx = 0; return;
    case '\n': newline(); return;
    case '\b': if (cx) cx--; return;
    case '\t': do put(' '); while (cx % 8); return;
    case 0x07: return;                          // bell
    }
    if (cx == COLS)
        newline();
    TEXT(cy)[cx] = c;
    draw_cell(cx, cy, c, 0);
    cx++;
}

// libc's output path calls this for everything printed (after the UART).
void con_write(const char *s, unsigned long n)
{
    if (!on)
        return;
    if (back) {                                 // new output: back to the bottom
        back = 0;
        redraw();
    }
    cursor(0);
    for (unsigned long i = 0; i < n; i++)
        put(s[i]);
    if (cx == COLS) {                           // keep the cursor on screen
        cx = COLS - 1;
    }
    cursor(1);
}

void con_clear(void)
{
    fill(0, 0, FB_W, FB_H, BG);
    memset(sb, ' ', SB_LINES * COLS);
    head = 0, back = 0;
    cx = cy = 0;
    cursor_shown = 0;
    esc = 0;
}

// Take over the screen: our framebuffer, cleared. Safe to call again (after
// Doom, which flips between its own buffers).
void con_init(void)
{
    for (int t = 0; t < 16; t++)
        shade[t] = mix(BG, FG, t);
    on = 1;
    con_clear();
    write32(FLIP0, FB);
    write32(FLIP1, FB);
}

// Move the view n lines up (n < 0: down), within what the ring holds.
void con_scroll(int n)
{
    if (!on)
        return;
    int max = head < SB_LINES - ROWS ? (int)head : SB_LINES - ROWS;
    int b = back + n;
    b = b < 0 ? 0 : b > max ? max : b;
    if (b == back)
        return;
    cursor(0);
    back = b;
    redraw();
    if (!back)
        cursor(1);
}

int con_rows(void)
{
    return ROWS;
}

void con_off(void)
{
    cursor(0);
    on = 0;
}

void con_status(void)
{
    printf("console %s: %dx%d characters of %dx%d px at (%d,%d), framebuffer %08x, shown %08x\n",
           on ? "on" : "off", COLS, ROWS, FONT_W, FONT_H, ORG_X, ORG_Y, FB, read32(0xf0641048));
    printf("last scroll took %lu us; scrollback %d lines at %08x, %lu used\n", scroll_ticks / 27,
           SB_LINES, SCROLLBACK, head + cy + 1 < SB_LINES ? head + cy + 1 : SB_LINES);
}
