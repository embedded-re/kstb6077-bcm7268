// mon.c - kmon, a memory monitor for the KSTB6077 (AArch64, EL2, bare metal).
//
// Started from BOLT with `go -64 0x00100000` (see mon/README.md). Reads and
// writes memory and registers by address or by name, survives aborts and
// crashes, receives programs over the serial line, starts them and gets
// control back when they return (Doom does).
//
// Every access to an address the user typed goes through safe_read /
// safe_write (start.S): an abort prints "abort" instead of hanging the board.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <stdint.h>

#include "kstb.h"
#include "mon.h"
#include "mmc.h"

void uart_putc(char c);

// ---------------- console ----------------

static int uart_ready(void)
{
    return read32(UART0 + 0x14) & 1;            // LSR bit 0: byte received
}

static int uart_getc(void)
{
    while (!uart_ready())
        ;
    return read32(UART0) & 0xff;
}

// Wait up to ms for a byte; -1 if none came.
static int uart_getc_timeout(uint32_t ms);

// A key was pressed (and is consumed): used to stop long commands.
static int key_pressed(void)
{
    if (!uart_ready())
        return 0;
    (void)(read32(UART0) & 0xff);
    return 1;
}

// ---------------- time ----------------

#define TICKS_PER_MS 27000

static uint64_t ticks(void)
{
    uint64_t t;
    __asm__ volatile("isb; mrs %0, cntpct_el0" : "=r"(t));
    return t;
}

static void delay_ms(uint32_t ms)
{
    uint64_t end = ticks() + (uint64_t)ms * TICKS_PER_MS;
    while (ticks() < end)
        ;
}

static int uart_getc_timeout(uint32_t ms)
{
    uint64_t end = ticks() + (uint64_t)ms * TICKS_PER_MS;
    do {
        if (uart_ready())
            return read32(UART0) & 0xff;
    } while (ticks() < end);
    return -1;
}

// ---------------- CRC32 ----------------

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

// ---------------- line editor ----------------

#define LINE    256
#define HIST    32

static char history[HIST][LINE];
static int hist_n;                              // lines stored (total, may exceed HIST)

static const char *const commands[];            // for Tab completion, defined below

static void redraw(const char *prompt, const char *buf, int len, int cur)
{
    printf("\r%s%.*s\x1b[K", prompt, len, buf);
    if (len > cur)
        printf("\x1b[%dD", len - cur);
}

// Complete the word that ends at the cursor: command names for the first
// word, register names after it.
static void complete(const char *prompt, char *buf, int *len, int *cur)
{
    int start = *cur;
    while (start > 0 && buf[start - 1] != ' ')
        start--;
    int first = 1;
    for (int i = 0; i < start; i++)
        if (buf[i] != ' ')
            first = 0;
    char word[LINE];
    int wl = *cur - start;
    memcpy(word, buf + start, wl);
    word[wl] = 0;

    const char *match[64];
    int n = 0;
    if (first) {
        for (int i = 0; commands[i] && n < 64; i++)
            if (strncmp(commands[i], word, wl) == 0)
                match[n++] = commands[i];
    } else {
        for (int i = 0; regs[i].name && n < 64; i++)
            if (strncmp(regs[i].name, word, wl) == 0)
                match[n++] = regs[i].name;
    }
    if (n == 0)
        return;

    int common = strlen(match[0]);              // longest common prefix of the matches
    for (int i = 1; i < n; i++) {
        int k = 0;
        while (k < common && match[i][k] == match[0][k])
            k++;
        common = k;
    }
    char add[LINE];
    int an = common - wl;
    memcpy(add, match[0] + wl, an);
    if (n == 1)
        add[an++] = ' ';
    if (n > 1 && an == 0) {                     // nothing to add: show the choices
        printf("\n");
        for (int i = 0; i < n; i++)
            printf("%s%s", match[i], i + 1 < n ? "  " : "\n");
    }
    if (*len + an >= LINE)
        return;
    memmove(buf + *cur + an, buf + *cur, *len - *cur);
    memcpy(buf + *cur, add, an);
    *len += an;
    *cur += an;
    redraw(prompt, buf, *len, *cur);
}

// Read a line with editing: arrows, Home/End, Delete, Backspace, Ctrl-A/E/B/F,
// Ctrl-K/U (kill), Ctrl-C (cancel), Up/Down (history), Tab (complete).
static void read_line(const char *prompt, char *buf)
{
    int len = 0, cur = 0, h = hist_n;           // h = history line shown (hist_n = the new one)
    char saved[LINE] = "";
    printf("%s", prompt);

    for (;;) {
        int c = uart_getc();
        if (c == '\r' || c == '\n') {
            buf[len] = 0;
            printf("\n");
            break;
        } else if (c == 3) {                    // Ctrl-C
            printf("^C\n");
            buf[0] = 0;
            return;
        } else if (c == 0x7f || c == 8) {       // Backspace
            if (cur > 0) {
                memmove(buf + cur - 1, buf + cur, len - cur);
                len--, cur--;
            }
        } else if (c == 1) {
            cur = 0;
        } else if (c == 5) {
            cur = len;
        } else if (c == 2) {
            if (cur > 0) cur--;
        } else if (c == 6) {
            if (cur < len) cur++;
        } else if (c == 0x0b) {
            len = cur;
        } else if (c == 0x15) {
            memmove(buf, buf + cur, len - cur);
            len -= cur, cur = 0;
        } else if (c == '\t') {
            complete(prompt, buf, &len, &cur);
            continue;
        } else if (c == 0x1b) {                 // escape sequence
            int c1 = uart_getc_timeout(20), c2 = uart_getc_timeout(20);
            if (c1 != '[' && c1 != 'O')
                continue;
            if (c2 >= '0' && c2 <= '9') {       // ESC [ n ~
                int n = c2 - '0', c3;
                while ((c3 = uart_getc_timeout(20)) >= '0' && c3 <= '9')
                    n = n * 10 + c3 - '0';
                if (n == 3 && cur < len) {      // Delete
                    memmove(buf + cur, buf + cur + 1, len - cur - 1);
                    len--;
                } else if (n == 1 || n == 7) {
                    cur = 0;
                } else if (n == 4 || n == 8) {
                    cur = len;
                }
            } else if (c2 == 'A' || c2 == 'B') { // history
                int oldest = hist_n > HIST ? hist_n - HIST : 0;
                int nh = c2 == 'A' ? h - 1 : h + 1;
                if (nh < oldest || nh > hist_n)
                    continue;
                if (h == hist_n) {
                    memcpy(saved, buf, len);
                    saved[len] = 0;
                }
                h = nh;
                const char *s = h == hist_n ? saved : history[h % HIST];
                len = cur = strlen(s);
                memcpy(buf, s, len);
            } else if (c2 == 'C') {
                if (cur < len) cur++;
            } else if (c2 == 'D') {
                if (cur > 0) cur--;
            } else if (c2 == 'H') {
                cur = 0;
            } else if (c2 == 'F') {
                cur = len;
            }
        } else if (c >= 0x20 && c < 0x7f && len < LINE - 1) {
            memmove(buf + cur + 1, buf + cur, len - cur);
            buf[cur++] = c;
            len++;
            if (cur == len) {                   // typing at the end: just echo
                putchar(c);
                continue;
            }
        } else {
            continue;
        }
        redraw(prompt, buf, len, cur);
    }

    if (len && (hist_n == 0 || strcmp(history[(hist_n - 1) % HIST], buf) != 0)) {
        strcpy(history[hist_n % HIST], buf);
        hist_n++;
    }
}

// ---------------- parsing ----------------

#define MAXARG 8

struct cmd {
    char name[32];      // without size suffix and '!'
    int size;           // .b .h .w .q = 1 2 4 8; 0 if not given
    int force;          // name ended with '!'
    int argc;
    char *argv[MAXARG];
};

static const struct reg *reg_by_name(const char *s, int len)
{
    for (int i = 0; regs[i].name; i++)
        if ((int)strlen(regs[i].name) == len && strncasecmp(regs[i].name, s, len) == 0)
            return &regs[i];
    return NULL;
}

static const struct reg *reg_by_addr(uint64_t a)
{
    for (int i = 0; regs[i].name; i++)
        if (regs[i].addr == a)
            return &regs[i];
    return NULL;
}

// Number: hex by default ("f0404000", "0x10"), decimal with '#' ("#100"),
// a register name ("temp"), and + / - between them ("fb+#3840").
// A name wins over hex: "fb" is the framebuffer, "0xfb" the number.
static int parse_num(const char *s, uint64_t *out)
{
    uint64_t total = 0;
    int sign = 1;
    if (*s == 0)
        return 0;
    while (*s) {
        uint64_t v = 0;
        int n = strcspn(s, "+-");
        const struct reg *r = reg_by_name(s, n);
        if (r) {
            v = r->addr;
        } else if (*s == '#') {
            if (n < 2)
                return 0;
            for (int i = 1; i < n; i++) {
                if (!isdigit(s[i]))
                    return 0;
                v = v * 10 + (s[i] - '0');
            }
        } else {
            int i = s[0] == '0' && n > 2 && (s[1] | 32) == 'x' ? 2 : 0;
            if (i == n)
                return 0;
            for (; i < n; i++) {
                if (!isxdigit(s[i]))
                    return 0;
                v = v * 16 + (isdigit(s[i]) ? s[i] - '0' : (s[i] | 32) - 'a' + 10);
            }
        }
        total += sign * v;
        s += n;
        if (*s == 0)
            break;
        sign = *s++ == '-' ? -1 : 1;
        if (*s == 0)
            return 0;
    }
    *out = total;
    return 1;
}

static int arg_num(struct cmd *c, int i, uint64_t *out)
{
    if (i >= c->argc)
        return 0;
    if (!parse_num(c->argv[i], out)) {
        printf("bad number or name: %s\n", c->argv[i]);
        return -1;
    }
    return 1;
}

// Refuse addresses on the "do not touch" list (registers.md) unless forced.
static int allowed(uint64_t start, uint64_t len, int what, int force)
{
    uint64_t end = start + (len ? len : 1) - 1;
    for (int i = 0; dangers[i].why; i++) {
        if ((dangers[i].what & what) && start <= dangers[i].end && end >= dangers[i].start) {
            if (force) {
                printf("(forced: %08x-%08x %s)\n", dangers[i].start, dangers[i].end, dangers[i].why);
                continue;
            }
            printf("refused: %08x-%08x %s. Add '!' to the command to do it anyway.\n",
                   dangers[i].start, dangers[i].end, dangers[i].why);
            return 0;
        }
    }
    return 1;
}

static const char *esr_class(uint64_t esr)
{
    switch (esr >> 26) {
    case 0x00: return "unknown instruction";
    case 0x0e: return "illegal execution state";
    case 0x15: return "SVC";
    case 0x16: return "HVC";
    case 0x17: return "SMC";
    case 0x20: case 0x21: return "instruction abort";
    case 0x22: return "PC alignment";
    case 0x24: case 0x25: return "data abort";
    case 0x26: return "SP alignment";
    case 0x2f: return "SError";
    case 0x3c: return "BRK";
    }
    return "?";
}

// Print the result of a failed access and return 0.
static int report_abort(uint64_t addr, uint32_t esr)
{
    printf("%08lx: abort (ESR %08x, %s%s)\n", addr, esr, esr_class((uint32_t)esr),
           (esr >> 26) == 0x25 && (esr & 0x3f) == 0x10 ? ", external" :
           (esr >> 26) == 0x25 && (esr & 0x3c) == 0x04 ? ", translation fault: not mapped" : "");
    return 0;
}

static int rd(uint64_t addr, int size, uint64_t *v)
{
    int esr = safe_read(addr, size, v);
    return esr ? report_abort(addr, esr) : 1;
}

static int wr(uint64_t addr, int size, uint64_t v)
{
    int esr = safe_write(addr, size, v);
    return esr ? report_abort(addr, esr) : 1;
}

static int check_align(uint64_t addr, int size)
{
    if (addr & (size - 1)) {
        printf("address %lx is not aligned to %d bytes\n", addr, size);
        return 0;
    }
    return 1;
}

// ---------------- decoding known registers ----------------

static void decode(uint64_t addr, uint64_t v)
{
    switch (addr) {
    case 0xf04d1500:
        if (v & (1 << 11)) {
            int mc = 410040 - (int)((v >> 1) & 0x3ff) * 487;
            printf("    = %d.%d C\n", mc / 1000, (mc % 1000) / 100);
        } else {
            printf("    (not valid)\n");
        }
        break;
    case 0xf041a084:
        printf("    = %luh %02lum %02lus since power-on\n", v / 3600, v / 60 % 60, v % 60);
        break;
    case 0xf0419c84:
        printf("    green %s, red %s, blue %s, SW1 %s, SW4 %s\n",
               v & (1 << 18) ? "off" : "ON", v & (1 << 17) ? "off" : "ON",
               v & (1 << 16) ? "off" : "ON", v & (1 << 14) ? "up" : "PRESSED",
               v & (1 << 7) ? "up" : "PRESSED");
        break;
    case 0xf041006c: case 0x07069844:
        printf("   %s%s%s\n", v & 1 ? " power-on" : "", v & (1 << 6) ? " watchdog" : "",
               v & (1 << 9) ? " software" : "");
        break;
    case 0xf040a6ac:
        printf("    = %lu.%03lu s left\n", v / 27000000, v / 27000 % 1000);
        break;
    }
}

// ---------------- commands ----------------

static uint64_t last_addr = PROG_ADDR;          // md continues here
static uint64_t load_addr = PROG_ADDR, load_size;

static void cmd_help(struct cmd *c);

static void cmd_r(struct cmd *c)
{
    uint64_t a, v;
    int size = c->size ? c->size : 4;
    if (arg_num(c, 0, &a) <= 0) {
        printf("usage: r[.b|.h|.w|.q] addr|name\n");
        return;
    }
    if (!check_align(a, size) || !allowed(a, size, DANGER_R, c->force) || !rd(a, size, &v))
        return;
    const struct reg *r = reg_by_addr(a);
    printf("%08lx: %0*lx", a, size * 2, v);
    if (size >= 2)
        printf("  (%lu)", v);
    if (r)
        printf("  %s: %s", r->name, r->desc);
    printf("\n");
    decode(a, v);
}

static void cmd_w(struct cmd *c)
{
    uint64_t a, v, back;
    int size = c->size ? c->size : 4;
    if (arg_num(c, 0, &a) <= 0 || arg_num(c, 1, &v) <= 0) {
        printf("usage: w[.b|.h|.w|.q] addr|name value\n");
        return;
    }
    if (!check_align(a, size) || !allowed(a, size, DANGER_W, c->force) || !wr(a, size, v))
        return;
    if (rd(a, size, &back))
        printf("%08lx: wrote %0*lx, reads %0*lx\n", a, size * 2, v, size * 2, back);
}

static void cmd_md(struct cmd *c)
{
    uint64_t a = last_addr, count;
    int size = c->size ? c->size : 4;
    int r = arg_num(c, 0, &a);
    if (r < 0)
        return;
    r = arg_num(c, 1, &count);
    if (r < 0)
        return;
    if (r == 0)
        count = 64 / size;
    a &= ~(uint64_t)(size - 1);
    if (!allowed(a, count * size, DANGER_R, c->force))
        return;

    uint64_t end = a + count * size;
    while (a < end) {
        uint8_t bytes[16];
        int n = 0;
        printf("%08lx:", a);
        for (; n < 16 && a + n < end; n += size) {
            uint64_t v;
            int esr = safe_read(a + n, size, &v);
            if (esr) {
                printf("\n");
                report_abort(a + n, esr);
                last_addr = a + n + size;
                return;
            }
            printf(" %0*lx", size * 2, v);
            memcpy(bytes + n, &v, size);
        }
        printf("%*s  ", (16 - n) / size * (size * 2 + 1), "");
        for (int i = 0; i < n; i++)
            putchar(isprint(bytes[i]) ? bytes[i] : '.');
        printf("\n");
        a += n;
        if (key_pressed())
            break;
    }
    last_addr = a;
}

static void cmd_mw(struct cmd *c)
{
    uint64_t a, v, count = 1;
    int size = c->size ? c->size : 4;
    if (arg_num(c, 0, &a) <= 0 || arg_num(c, 1, &v) <= 0 || arg_num(c, 2, &count) < 0) {
        printf("usage: mw[.b|.h|.w|.q] addr value [count]\n");
        return;
    }
    if (!check_align(a, size) || !allowed(a, count * size, DANGER_W, c->force))
        return;
    for (uint64_t i = 0; i < count; i++)
        if (!wr(a + i * size, size, v))
            return;
    printf("wrote %lu x %0*lx at %08lx\n", count, size * 2, v, a);
}

// setb / clrb / togb addr bit [bit...]: 32-bit read-modify-write
static void cmd_bits(struct cmd *c)
{
    uint64_t a, v, mask = 0, bit;
    if (arg_num(c, 0, &a) <= 0 || c->argc < 2) {
        printf("usage: %s addr bit [bit...]   (bit numbers in decimal)\n", c->name);
        return;
    }
    for (int i = 1; i < c->argc; i++) {
        bit = strtol(c->argv[i], NULL, 10);
        if (bit > 31) {
            printf("bit %lu: must be 0-31\n", bit);
            return;
        }
        mask |= 1u << bit;
    }
    if (!check_align(a, 4) || !allowed(a, 4, DANGER_R | DANGER_W, c->force) || !rd(a, 4, &v))
        return;
    uint64_t n = c->name[0] == 's' ? v | mask : c->name[0] == 'c' ? v & ~mask : v ^ mask;
    if (!wr(a, 4, n) || !rd(a, 4, &n))
        return;
    printf("%08lx: %08lx -> %08lx\n", a, v, n);
}

static void cmd_watch(struct cmd *c)
{
    uint64_t a, v, old, ms = 0;
    int size = c->size ? c->size : 4;
    if (arg_num(c, 0, &a) <= 0 || arg_num(c, 1, &ms) < 0) {
        printf("usage: watch[.b|.h|.w] addr [interval ms, hex]\n");
        return;
    }
    if (!check_align(a, size) || !allowed(a, size, DANGER_R, c->force) || !rd(a, size, &old))
        return;
    printf("watching %08lx (any key stops)\n      0 ms: %0*lx\n", a, size * 2, old);
    uint64_t t0 = ticks();
    while (!key_pressed()) {
        if (!rd(a, size, &v))
            return;
        if (v != old) {
            printf("%7lu ms: %0*lx  changed bits %0*lx\n", (ticks() - t0) / TICKS_PER_MS,
                   size * 2, v, size * 2, v ^ old);
            old = v;
        }
        if (ms)
            delay_ms(ms);
    }
}

static void cmd_fill(struct cmd *c)
{
    uint64_t a, len, v;
    if (arg_num(c, 0, &a) <= 0 || arg_num(c, 1, &len) <= 0 || arg_num(c, 2, &v) <= 0) {
        printf("usage: fill addr len byte\n");
        return;
    }
    if (!allowed(a, len, DANGER_W, c->force))
        return;
    for (uint64_t i = 0; i < len; i++)
        if (!wr(a + i, 1, v & 0xff))
            return;
    printf("filled %lx bytes at %08lx with %02lx\n", len, a, v & 0xff);
}

static void cmd_cp(struct cmd *c)
{
    uint64_t d, s, len, v;
    if (arg_num(c, 0, &d) <= 0 || arg_num(c, 1, &s) <= 0 || arg_num(c, 2, &len) <= 0) {
        printf("usage: cp dst src len\n");
        return;
    }
    if (!allowed(s, len, DANGER_R, c->force) || !allowed(d, len, DANGER_W, c->force))
        return;
    for (uint64_t i = 0; i < len; i++)
        if (!rd(s + i, 1, &v) || !wr(d + i, 1, v))
            return;
    printf("copied %lx bytes %08lx -> %08lx\n", len, s, d);
}

static void cmd_cmp(struct cmd *c)
{
    uint64_t a, b, len, x, y;
    int diffs = 0;
    if (arg_num(c, 0, &a) <= 0 || arg_num(c, 1, &b) <= 0 || arg_num(c, 2, &len) <= 0) {
        printf("usage: cmp addr1 addr2 len\n");
        return;
    }
    if (!allowed(a, len, DANGER_R, c->force) || !allowed(b, len, DANGER_R, c->force))
        return;
    for (uint64_t i = 0; i < len; i++) {
        if (!rd(a + i, 1, &x) || !rd(b + i, 1, &y))
            return;
        if (x != y && diffs++ < 8)
            printf("  +%lx: %02lx != %02lx\n", i, x, y);
    }
    printf(diffs ? "%d bytes differ\n" : "same\n", diffs);
}

static void cmd_crc(struct cmd *c)
{
    uint64_t a, len;
    if (arg_num(c, 0, &a) <= 0 || arg_num(c, 1, &len) <= 0) {
        printf("usage: crc addr len   (same as BOLT's crc and zlib.crc32)\n");
        return;
    }
    if (!allowed(a, len, DANGER_R, c->force))
        return;
    if (a < 0xc0000000 && a + len <= 0x80000000) {  // RAM: fast path
        printf("crc %08lx-%08lx = %08x\n", a, a + len, crc32((const uint8_t *)a, len));
        return;
    }
    printf("crc: RAM only (below 0x80000000)\n");
}

static void cmd_find(struct cmd *c)
{
    uint64_t a, len, want, v;
    int size = c->size ? c->size : 4, found = 0;
    if (arg_num(c, 0, &a) <= 0 || arg_num(c, 1, &len) <= 0 || arg_num(c, 2, &want) <= 0) {
        printf("usage: find[.b|.h|.w|.q] addr len value\n");
        return;
    }
    if (!allowed(a, len, DANGER_R, c->force))
        return;
    for (uint64_t i = 0; i + size <= len; i += size) {
        if (!rd(a + i, size, &v))
            return;
        if (v == want && found++ < 16)
            printf("  %08lx\n", a + i);
    }
    printf("%d found\n", found);
}

static void cmd_calc(struct cmd *c)
{
    uint64_t v;
    if (arg_num(c, 0, &v) <= 0) {
        printf("usage: calc expr   e.g. calc fb+#3840, calc #1000\n");
        return;
    }
    printf("%lx = #%lu\n", v, v);
}

static void cmd_regs(struct cmd *c)
{
    const char *f = c->argc ? c->argv[0] : "";
    for (int i = 0; regs[i].name; i++)
        if (strstr(regs[i].name, f) || strstr(regs[i].desc, f))
            printf("  %-12s %08x  %s\n", regs[i].name, regs[i].addr, regs[i].desc);
}

static void cmd_history(struct cmd *c)
{
    (void)c;
    for (int i = hist_n > HIST ? hist_n - HIST : 0; i < hist_n; i++)
        printf("%4d  %s\n", i + 1, history[i % HIST]);
}

// load [addr]: receive a file over the serial line (tools/kstb-send).
// Protocol: 'KLD1', u32 size, u32 crc32, then size bytes; reply with the CRC.
static void cmd_load(struct cmd *c)
{
    uint64_t a = PROG_ADDR;
    if (arg_num(c, 0, &a) < 0)
        return;
    printf("load: waiting for data at %08lx (tools/kstb-send; Ctrl-C cancels)\n", a);
    const char *magic = "KLD1";
    int m = 0;
    while (m < 4) {
        int ch = uart_getc();
        if (ch == 3) {
            printf("cancelled\n");
            return;
        }
        m = ch == magic[m] ? m + 1 : ch == 'K';
    }
    uint32_t hdr[2];
    uint8_t *h = (uint8_t *)hdr;
    for (int i = 0; i < 8; i++) {
        int ch = uart_getc_timeout(2000);
        if (ch < 0) {
            printf("load: timeout in header\n");
            return;
        }
        h[i] = ch;
    }
    uint32_t size = hdr[0], want = hdr[1];
    // Check the target, then take the bytes even if refused, to keep the line in sync.
    int ok = a + size <= 0x7d600000 && allowed(a, size, DANGER_W, c->force) &&
             !(a < HEAP_END && a + size > HEAP_BASE);
    uint8_t *p = (uint8_t *)a;
    for (uint32_t i = 0; i < size; i++) {
        int ch = uart_getc_timeout(2000);
        if (ch < 0) {
            printf("load: timeout after %u of %u bytes\n", i, size);
            return;
        }
        if (ok)
            p[i] = ch;
    }
    if (!ok) {
        printf("load: refused %08lx + %x (monitor, its heap, or not RAM)\n", a, size);
        return;
    }
    uint32_t got = crc32(p, size);
    printf("load: %u bytes at %08lx, CRC %08x %s\n", size, a, got, got == want ? "OK" : "BAD");
    if (got == want)
        load_addr = a, load_size = size;
}

static void cmd_go(struct cmd *c)
{
    uint64_t a = load_addr;
    if (arg_num(c, 0, &a) < 0)
        return;
    printf("go %08lx (MMU off, like BOLT's go -64; a program returns by jumping to x2)\n", a);
    run_program(a);
}

// call addr [x0] [x1]: run code as a function with the monitor's MMU and
// stack, print what it returns in x0.
static void cmd_call(struct cmd *c)
{
    uint64_t a, x0 = 0, x1 = 0;
    if (arg_num(c, 0, &a) <= 0 || arg_num(c, 1, &x0) < 0 || arg_num(c, 2, &x1) < 0) {
        printf("usage: call addr [x0] [x1]\n");
        return;
    }
    dcache_flush_all();
    __asm__ volatile("ic iallu; dsb sy; isb" ::: "memory");
    uint64_t r = ((uint64_t (*)(uint64_t, uint64_t))a)(x0, x1);
    printf("returned %lx\n", r);
}

struct kwad {
    uint32_t magic, version, size, crc, offset;
    char name[32];
};

#define KWAD_MAGIC 0x4441574b

static void cmd_doom(struct cmd *c)
{
    (void)c;
    const struct kwad *img = (const struct kwad *)DOOM_IMG;
    const struct kwad *wad = (const struct kwad *)WAD_ADDR;
    if (img->magic != KWAD_MAGIC) {
        printf("no doom.bin image at %08x: start the monitor with tools/kstb-mon (mon/README.md)\n", DOOM_IMG);
        return;
    }
    if (wad->magic != KWAD_MAGIC) {             // not loaded by BOLT: read it from the eMMC
        uint32_t lba = SPLASH_FIRST + WAD_OFFSET / 512;
        printf("loading the WAD from flash0.splash +%x ...\n", WAD_OFFSET);
        if (mmc_init() || mmc_read(lba, 1, (void *)WAD_ADDR) || wad->magic != KWAD_MAGIC) {
            printf("no KWAD image at flash0.splash +%x\n", WAD_OFFSET);
            return;
        }
        uint32_t sectors = (wad->offset + wad->size + 511) / 512;
        if (mmc_read(lba, sectors, (void *)WAD_ADDR)) {
            printf("eMMC read failed\n");
            ((struct kwad *)WAD_ADDR)->magic = 0;
            return;
        }
    }
    const uint8_t *bin = (const uint8_t *)DOOM_IMG + img->offset;
    if (crc32(bin, img->size) != img->crc) {
        printf("doom.bin image at %08x: CRC wrong\n", DOOM_IMG);
        return;
    }
    memcpy((void *)PROG_ADDR, bin, img->size);  // a fresh copy every time
    printf("starting Doom (%u bytes); quitting from its menu comes back here\n", img->size);
    run_program(PROG_ADDR);
}

// IR remote: NEC set-up as in doom/dg_kstb.c and docs/hardware/ir.md
#define IR_BASE 0xf0419900

static const uint16_t nec_params[][2] = {
    {0, 0x0ff}, {1, 0x000}, {2, 0x012}, {3, 0x384}, {4, 0x384}, {5, 0x1c2},
    {6, 0x0e1}, {7, 0x000}, {8, 0x000}, {9, 0x000}, {10, 0x000}, {11, 0x80f},
    {12, 0xc32}, {13, 0x000}, {14, 0x071}, {15, 0x071}, {16, 0x10d}, {17, 0x032},
    {18, 0x830}, {19, 0x031}, {20, 0x016}, {21, 0x006}, {22, 0x258}, {23, 0x118},
    {25, 0x000}, {26, 0x000}, {27, 0x000},
};

static void cmd_ir(struct cmd *c)
{
    (void)c;
    write32(IR_BASE + 0x00, 0);
    write32(IR_BASE + 0x14, read32(IR_BASE + 0x14) | 0x20);
    write32(IR_BASE + 0x14, read32(IR_BASE + 0x14) | 0x10);
    write32(IR_BASE + 0x08, 0);
    for (unsigned i = 0; i < sizeof nec_params / sizeof nec_params[0]; i++) {
        write32(IR_BASE + 0x18, nec_params[i][0]);
        write32(IR_BASE + 0x1c, nec_params[i][1]);
    }
    printf("IR (kbd1, NEC) on: press remote keys, any serial key stops\n");
    uint64_t t0 = ticks();
    while (!key_pressed()) {
        uint32_t st = read32(IR_BASE);
        if (!(st & 1))
            continue;
        uint32_t code = read32(IR_BASE + 0x10);
        write32(IR_BASE, st & ~1u);
        const char *name = "?";
        if ((code & 0xffff) == 0x0820)
            for (int i = 0; irkeys[i].name; i++)
                if (irkeys[i].cmd == ((code >> 16) & 0xff))
                    name = irkeys[i].name;
        printf("%7lu ms: %08x  %-12s %s\n", (ticks() - t0) / TICKS_PER_MS, code, name,
               st & 0x40 ? "" : "(repeat)");
    }
}

// LEDs on AON GPIO bank 0 (docs/hardware/gpio.md): all active-low
static void cmd_led(struct cmd *c)
{
    const char *w = c->argc ? c->argv[0] : "";
    uint32_t on;
    if (!strcmp(w, "green")) on = 1 << 18;
    else if (!strcmp(w, "red")) on = 1 << 17;
    else if (!strcmp(w, "orange")) on = 3 << 17;
    else if (!strcmp(w, "blue")) on = 1 << 16;
    else if (!strcmp(w, "off")) on = 0;
    else {
        printf("usage: led green|red|orange|blue|off\n");
        return;
    }
    uint32_t data = read32(0xf0419c84) | (7 << 16);     // all off
    if (on & (1 << 16)) {                               // blue is an input after BOLT
        write32(0xf0419c80, read32(0xf0419c80) | (1 << 16));
        write32(0xf0419c84, data & ~on);
        write32(0xf0419c88, read32(0xf0419c88) & ~(1u << 16));
    } else {
        write32(0xf0419c84, data & ~on);
    }
}

static void cmd_wdt(struct cmd *c)
{
    if (c->argc && !strcmp(c->argv[0], "off")) {
        write32(0xf040a6ac, 0xee00);
        write32(0xf040a6ac, 0x00ee);
        printf("watchdog stopped\n");
        return;
    }
    if (c->argc) {
        uint32_t s = strtol(c->argv[0], NULL, 10);
        if (s < 1 || s > 159) {
            printf("usage: wdt [seconds 1-159 | off]   (seconds in decimal)\n");
            return;
        }
        write32(0xf040a6a8, s * 27000000u);
        write32(0xf040a6ac, 0xff00);
        write32(0xf040a6ac, 0x00ff);
        printf("watchdog armed: the board resets in %u s unless stopped\n", s);
        return;
    }
    uint32_t left = read32(0xf040a6ac);
    printf("watchdog: %u ticks left (%u.%03u s; a stopped watchdog also shows a count)\n",
           left, left / 27000000, left / 27000 % 1000);
}

static void cmd_reset(struct cmd *c)
{
    (void)c;
    printf("software reset -> BOLT\n");
    delay_ms(20);
    write32(RESET_A, 1);
    write32(RESET_B, 1);
}

static void cmd_info(struct cmd *c)
{
    (void)c;
    uint64_t v, el, midr, mpidr, frq, sctlr, hcr;
    __asm__ volatile("mrs %0, CurrentEL" : "=r"(el));
    __asm__ volatile("mrs %0, midr_el1" : "=r"(midr));
    __asm__ volatile("mrs %0, mpidr_el1" : "=r"(mpidr));
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(frq));
    __asm__ volatile("mrs %0, sctlr_el2" : "=r"(sctlr));
    __asm__ volatile("mrs %0, hcr_el2" : "=r"(hcr));

    printf("kmon, built " __DATE__ " " __TIME__ "\n");
    printf("chip      %08x / %08x\n", read32(0xf0404000), read32(0xf0404004));
    printf("CPU       EL%lu, MIDR %08lx, MPIDR %08lx, CNTFRQ %lu Hz\n", el >> 2, midr, mpidr, frq);
    printf("          SCTLR_EL2 %08lx, HCR_EL2 %08lx\n", sctlr, hcr);
    v = read32(0xf04d1500);
    printf("temp      %08lx", v);
    decode(0xf04d1500, v);
    v = read32(0xf041a084);
    printf("uptime    %lu s", v);
    decode(0xf041a084, v);
    if (safe_read(0x07069844, 4, &v) == 0) {
        printf("reset     %lx (BOLT's copy)", v);
        decode(0x07069844, v);
    }
    const struct kwad *w = (const struct kwad *)WAD_ADDR, *d = (const struct kwad *)DOOM_IMG;
    printf("WAD       %s\n", w->magic == KWAD_MAGIC ? w->name : "not loaded");
    if (w->magic == KWAD_MAGIC)
        printf("          %u bytes at %08x\n", w->size, WAD_ADDR + w->offset);
    printf("doom.bin  %s\n", d->magic == KWAD_MAGIC ? "in the bundle" : "not loaded");
    uint32_t r0 = read32(0xf0ca0800);
    delay_ms(5);
    printf("audio     %s\n", read32(0xf0ca0800) != r0 ? "DMA running (ring 0)" : "DMA not running");
    printf("display   surface %08x, frame %08x\n", read32(0xf0641048), read32(0xf0603484));
    printf("memory    monitor %08x, doom image %08x, heap %08x, programs %08x, WAD %08x\n",
           MON_ADDR, DOOM_IMG, HEAP_BASE, PROG_ADDR, WAD_ADDR);
}

// ---------------- eMMC ----------------

static uint8_t sector[512] __attribute__((aligned(16)));

static const char *const card_states[] = {
    "idle", "ready", "ident", "stby", "tran", "data", "rcv", "prg", "dis",
};

static int mmc_ok(int r)
{
    if (r == MMC_EGUARD || r == MMC_ENOCARD)
        printf("mmc: %s\n", mmc_strerror(r));
    else if (r)
        printf("mmc: %s (interrupt status %08x)\n", mmc_strerror(r), mmc_last_int());
    return r == 0;
}

static void mmc_show_status(void)
{
    printf("host   present %08x, control %08x, clock %08x\n",
           read32(0xf0200224), read32(0xf0200228), read32(0xf020022c));
    for (uint32_t rca = 1; rca <= 4; rca++) {
        uint32_t st;
        if (mmc_status(rca, &st) == 0) {
            unsigned s = (st >> 9) & 0xf;
            printf("card   RCA %u: status %08x, state %s%s\n", rca, st,
                   s < 9 ? card_states[s] : "?", st & (1 << 8) ? ", ready for data" : "");
            return;
        }
    }
    printf("card   no answer to CMD13 with RCA 1-4\n");
}

static void mmc_show_ext_csd(void)
{
    static uint8_t e[512] __attribute__((aligned(16)));
    if (!mmc_ok(mmc_read_ext_csd(e)))
        return;
    uint32_t sec = e[212] | e[213] << 8 | e[214] << 16 | (uint32_t)e[215] << 24;
    printf("EXT_CSD_REV          %u\n", e[192]);
    printf("SEC_COUNT            %u sectors = %u MiB\n", sec, sec / 2048);
    printf("DEVICE_TYPE          %02x\n", e[196]);
    printf("HS_TIMING            %u\n", e[185]);
    printf("BUS_WIDTH            %u (0 = 1-bit, 1 = 4-bit, 2 = 8-bit, 5/6 = DDR)\n", e[183]);
    printf("PARTITION_CONFIG     %02x (boot partition enabled %u, access %u)\n",
           e[179], (e[179] >> 3) & 7, e[179] & 7);
    printf("BOOT_BUS_CONDITIONS  %02x\n", e[177]);
    printf("BOOT_SIZE_MULT       %u x 128 KB = %u KB per boot partition\n", e[226], e[226] * 128);
    printf("RPMB_SIZE_MULT       %u x 128 KB\n", e[168]);
    printf("BOOT_WP              %02x   BOOT_WP_STATUS %02x\n", e[173], e[174]);
    printf("PARTITIONING_SUPPORT %02x   PARTITION_SETTING_COMPLETED %02x\n", e[160], e[155]);
    printf("ERASE_GROUP_DEF      %u   HC_ERASE_GRP_SIZE %u x 512 KB\n", e[175], e[224]);
    printf("RST_n_FUNCTION       %02x\n", e[162]);
    printf("life time est. A/B   %02x / %02x (01 = 0-10%% used ... 0b = exceeded)\n", e[268], e[269]);
    printf("PRE_EOL_INFO         %02x (01 normal, 02 warning, 03 urgent)\n", e[267]);
    printf("FIRMWARE_VERSION     ");
    for (int i = 254; i < 262; i++)
        printf("%02x", e[i]);
    printf("\n");
}

static void mmc_show_part(void)
{
    if (!mmc_ok(mmc_read(1, 1, sector)))
        return;
    if (memcmp(sector, "EFI PART", 8) != 0) {
        printf("LBA 1 is not a GPT header\n");
        return;
    }
    uint64_t entries;
    uint32_t count, size;
    memcpy(&entries, sector + 72, 8);
    memcpy(&count, sector + 80, 4);
    memcpy(&size, sector + 84, 4);
    printf("GPT: %u entries of %u bytes at LBA %lu\n", count, size, entries);
    printf("  #  first LBA   last LBA    size        name\n");
    for (uint32_t i = 0; i < count && size >= 128 && size <= 512; i++) {
        uint32_t per = 512 / size;
        if (i % per == 0 && !mmc_ok(mmc_read(entries + i / per, 1, sector)))
            return;
        const uint8_t *en = sector + (i % per) * size;
        uint64_t first, last;
        memcpy(&first, en + 32, 8);
        memcpy(&last, en + 40, 8);
        if (first == 0 && last == 0)
            continue;
        char name[37];
        for (int k = 0; k < 36; k++)
            name[k] = en[56 + 2 * k] < 0x80 ? en[56 + 2 * k] : '?';
        name[36] = 0;
        uint64_t kb = (last - first + 1) / 2;
        printf("  %-2u %-11lu %-11lu %7lu %s  %s\n", i + 1, first, last,
               kb >= 10240 ? kb / 1024 : kb, kb >= 10240 ? "MB" : "KB", name);
    }
}

static void cmd_mmc(struct cmd *c)
{
    const char *sub = c->argc ? c->argv[0] : "";
    uint64_t lba, n, a = 0x02000000;
    int r = mmc_init();
    if (!mmc_ok(r))
        return;

    if (!strcmp(sub, "")) {
        mmc_show_status();
    } else if (!strcmp(sub, "extcsd")) {
        mmc_show_ext_csd();
    } else if (!strcmp(sub, "part")) {
        mmc_show_part();
    } else if (!strcmp(sub, "dump")) {
        if (arg_num(c, 1, &lba) <= 0) {
            printf("usage: mmc dump lba\n");
            return;
        }
        if (!mmc_ok(mmc_read(lba, 1, sector)))
            return;
        for (int i = 0; i < 512; i += 16) {
            printf("%03x:", i);
            for (int k = 0; k < 16; k++)
                printf(" %02x", sector[i + k]);
            printf("  ");
            for (int k = 0; k < 16; k++)
                putchar(isprint(sector[i + k]) ? sector[i + k] : '.');
            printf("\n");
        }
    } else if (!strcmp(sub, "read") || !strcmp(sub, "write")) {
        int write = sub[0] == 'w';
        if (arg_num(c, 1, &lba) <= 0 || arg_num(c, 2, &n) <= 0 || arg_num(c, 3, &a) < 0 || n == 0) {
            printf("usage: mmc %s lba count [addr]   (512-byte sectors; addr default 2000000)\n", sub);
            return;
        }
        uint64_t end = a + n * 512;                 // RAM side: not the monitor, its heap, or non-RAM
        if (a < 0x00001000 || end > 0x7d600000 || (a < DOOM_IMG && end > MON_ADDR) ||
            (a < HEAP_END && end > HEAP_BASE)) {
            printf("mmc: RAM %08lx + %lx refused\n", a, n * 512);
            return;
        }
        if (write && !mmc_ok(mmc_write_allowed(lba, n, c->force)))
            return;
        uint64_t t = ticks();
        r = write ? mmc_write(lba, n, (const void *)a, c->force) : mmc_read(lba, n, (void *)a);
        uint64_t us = (ticks() - t) / 27;
        if (!mmc_ok(r))
            return;
        printf("%s %lu sectors (%lu KB) LBA %lu %s %08lx in %lu ms (%lu KB/s)\n",
               write ? "wrote" : "read", n, n / 2, lba, write ? "from" : "to", a,
               us / 1000, us ? n * 512 * 1000000 / 1024 / us : 0);
    } else {
        printf("usage: mmc [extcsd | part | dump lba | read lba count [addr] | write lba count addr]\n"
               "  lba/count in 512-byte sectors (hex, or #decimal). flash0.splash = LBA #%u-#%u;\n"
               "  writes only there, and not in its first 512 KB without 'mmc!'.\n",
               SPLASH_FIRST, SPLASH_LAST);
    }
}

typedef void (*cmd_fn)(struct cmd *);

static const struct {
    const char *name;
    cmd_fn fn;
    const char *usage;
    const char *help;
} table[] = {
    { "help",    cmd_help,    "[cmd]",                      "this list" },
    { "info",    cmd_info,    "",                           "board, CPU, what is loaded" },
    { "regs",    cmd_regs,    "[filter]",                   "known register and RAM names" },
    { "r",       cmd_r,       "[.bhwq] addr",               "read one value (and decode known registers)" },
    { "w",       cmd_w,       "[.bhwq] addr value",         "write one value, read it back" },
    { "md",      cmd_md,      "[.bhwq] [addr] [count]",     "dump memory; Enter continues" },
    { "mw",      cmd_mw,      "[.bhwq] addr value [count]", "write a value count times" },
    { "setb",    cmd_bits,    "addr bit [bit...]",          "set bits (32-bit read-modify-write)" },
    { "clrb",    cmd_bits,    "addr bit [bit...]",          "clear bits" },
    { "togb",    cmd_bits,    "addr bit [bit...]",          "toggle bits" },
    { "watch",   cmd_watch,   "[.bhw] addr [ms]",           "print a value whenever it changes" },
    { "fill",    cmd_fill,    "addr len byte",              "fill memory" },
    { "cp",      cmd_cp,      "dst src len",                "copy memory" },
    { "cmp",     cmd_cmp,     "addr1 addr2 len",            "compare memory" },
    { "crc",     cmd_crc,     "addr len",                   "CRC32 of RAM (= BOLT crc, zlib.crc32)" },
    { "find",    cmd_find,    "[.bhwq] addr len value",     "search for a value" },
    { "calc",    cmd_calc,    "expr",                       "evaluate a number/name expression" },
    { "load",    cmd_load,    "[addr]",                     "receive a file over serial (tools/kstb-send)" },
    { "go",      cmd_go,      "[addr]",                     "start a program like BOLT's go -64" },
    { "call",    cmd_call,    "addr [x0] [x1]",             "call code as a function, keep the MMU" },
    { "doom",    cmd_doom,    "",                           "start Doom; quitting returns here" },
    { "mmc",     cmd_mmc,     "[extcsd|part|dump|read|write]", "eMMC: status, EXT_CSD, GPT, sectors" },
    { "ir",      cmd_ir,      "",                           "show IR remote codes" },
    { "led",     cmd_led,     "green|red|orange|blue|off",  "front LEDs" },
    { "wdt",     cmd_wdt,     "[seconds|off]",              "watchdog" },
    { "history", cmd_history, "",                           "command history" },
    { "reset",   cmd_reset,   "",                           "software reset back to BOLT" },
};

#define NCMD (int)(sizeof table / sizeof table[0])

static const char *const commands[NCMD + 1] = {
    "help", "info", "regs", "r", "w", "md", "mw", "setb", "clrb", "togb", "watch",
    "fill", "cp", "cmp", "crc", "find", "calc", "load", "go", "call", "doom", "mmc", "ir",
    "led", "wdt", "history", "reset", NULL,
};

static void cmd_help(struct cmd *c)
{
    for (int i = 0; i < NCMD; i++)
        if (c->argc == 0 || !strcmp(c->argv[0], table[i].name))
            printf("  %-7s %-27s %s\n", table[i].name, table[i].usage, table[i].help);
    if (c->argc == 0)
        printf("Numbers are hex (f0404000, 0x10), #decimal (#100), or names (temp, fb+#3840).\n"
               "Sizes: .b .h .w .q (default .w). A '!' after a command (w! ...) overrides the\n"
               "do-not-touch list. Tab completes commands and names; Up/Down recall history.\n");
}

// Split "md.b! fb 10" into name, size, force and arguments.
static int parse(char *line, struct cmd *c)
{
    memset(c, 0, sizeof *c);
    char *s = line;
    while (*s == ' ')
        s++;
    if (*s == 0)
        return 0;
    char *w = s;
    while (*s && *s != ' ')
        s++;
    if (*s)
        *s++ = 0;
    int n = strlen(w);
    if (n && w[n - 1] == '!')
        c->force = 1, w[--n] = 0;
    char *dot = strchr(w, '.');
    if (dot) {
        *dot = 0;
        switch (dot[1]) {
        case 'b': c->size = 1; break;
        case 'h': c->size = 2; break;
        case 'w': c->size = 4; break;
        case 'q': c->size = 8; break;
        default:
            printf("unknown size .%s (use .b .h .w .q)\n", dot + 1);
            return 0;
        }
    }
    strncpy(c->name, w, sizeof c->name - 1);
    while (*s && c->argc < MAXARG) {
        while (*s == ' ')
            s++;
        if (*s == 0)
            break;
        c->argv[c->argc++] = s;
        while (*s && *s != ' ')
            s++;
        if (*s)
            *s++ = 0;
    }
    return 1;
}

static void show_exception(void)
{
    static const char *types[] = { "synchronous", "IRQ", "FIQ", "SError" };
    uint64_t *f = exc_frame;
    printf("\n*** exception: %s, ESR %08lx (%s)\n", types[f[34] & 3], f[33], esr_class(f[33]));
    printf("    ELR %016lx  FAR %016lx  SPSR %08lx  SP %016lx\n", f[32], f[35], f[36], f[31]);
    for (int i = 0; i < 31; i++)
        printf("    x%-2d %016lx%s", i, f[i], i % 3 == 2 ? "\n" : "");
    printf("\n");
}

void mon_main(int how, long status)
{
    static char line[LINE];
    struct cmd c;

    if (how == 0)
        printf("\nkmon: KSTB6077 monitor, EL2. 'help' lists the commands.\n");
    else if (how == 1)
        printf("\nkmon: program returned, status %ld\n", status);
    else
        show_exception();

    for (;;) {
        read_line("kmon> ", line);
        if (line[0] == 0) {                     // Enter on an empty line: md continues
            if (hist_n && !strncmp(history[(hist_n - 1) % HIST], "md", 2)) {
                char again[LINE];
                strcpy(again, history[(hist_n - 1) % HIST]);
                if (parse(again, &c)) {
                    c.argc = 0;                 // from last_addr, default count
                    if (c.size == 0)
                        c.size = 4;
                    cmd_md(&c);
                }
            }
            continue;
        }
        char work[LINE];
        strcpy(work, line);
        if (!parse(work, &c))
            continue;
        int i;
        for (i = 0; i < NCMD; i++)
            if (!strcmp(c.name, table[i].name))
                break;
        if (i == NCMD) {
            printf("unknown command '%s' ('help' lists them)\n", c.name);
            continue;
        }
        table[i].fn(&c);
    }
}
