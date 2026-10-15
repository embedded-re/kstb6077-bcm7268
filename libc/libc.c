// libc.c - the small C library for the bare-metal programs (doom/, mon/).
//
// What doomgeneric calls (found with `llvm-nm -u` on its objects), plus a few
// extras for the monitor. Each program provides kstb.h (UART0, HEAP_BASE/END).
//   output    printf & co. go to UART0 (\n -> \r\n), and to libc_output_hook
//             if the program sets one (the monitor's HDMI console)
//   memory    bump allocator from HEAP_BASE; free() only gives back the last block
//   files     a RAM file system: the WAD is registered by the platform code,
//             files opened for writing (config, savegames) live in the heap
//             until the next reboot

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <stdint.h>
#include <errno.h>
#include <sys/stat.h>

#include "kstb.h"

int errno;

// ---------------- UART ----------------

void uart_putc(char c)
{
    while ((read32(UART0 + 0x14) & 0x20) == 0)  // LSR bit 5: room in the FIFO
        ;
    write32(UART0, c);
}

void (*libc_output_hook)(const char *s, size_t n);

// The hook runs first, so a slow screen update is done before the text reaches
// the serial line (where a host tool may be waiting for it to start sending).
static void uart_write(const char *s, size_t n)
{
    if (libc_output_hook)
        libc_output_hook(s, n);
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '\n')
            uart_putc('\r');
        uart_putc(s[i]);
    }
}

// ---------------- memory ----------------

// Each block has a 16-byte header with its size, so realloc knows how much to copy.
static uint8_t *heap_top = (uint8_t *)HEAP_BASE;
static uint8_t *last_block;

void *malloc(size_t n)
{
    n = (n + 15) & ~(size_t)15;
    if (heap_top + 16 + n > (uint8_t *)HEAP_END) {
        errno = ENOMEM;
        return NULL;
    }
    *(size_t *)heap_top = n;
    last_block = heap_top + 16;
    heap_top += 16 + n;
    return last_block;
}

void free(void *p)
{
    if (p != NULL && p == last_block) {     // only the newest block can be returned
        heap_top = last_block - 16;
        last_block = NULL;
    }
}

void *calloc(size_t n, size_t size)
{
    void *p = malloc(n * size);
    if (p)
        memset(p, 0, n * size);
    return p;
}

void *realloc(void *p, size_t n)
{
    if (p == NULL)
        return malloc(n);
    size_t old = *(size_t *)((uint8_t *)p - 16);
    if (n <= old)
        return p;
    if (p == last_block && (uint8_t *)p + n <= (uint8_t *)HEAP_END) {   // grow in place
        n = (n + 15) & ~(size_t)15;
        *(size_t *)((uint8_t *)p - 16) = n;
        heap_top = (uint8_t *)p + n;
        return p;
    }
    void *q = malloc(n);
    if (q)
        memcpy(q, p, old);
    return q;
}

// ---------------- strings ----------------

void *memcpy(void *d, const void *s, size_t n)
{
    uint8_t *dd = d;
    const uint8_t *ss = s;
    if ((((uintptr_t)dd | (uintptr_t)ss) & 7) == 0) {
        for (; n >= 8; n -= 8, dd += 8, ss += 8)
            *(uint64_t *)dd = *(const uint64_t *)ss;
    }
    while (n--)
        *dd++ = *ss++;
    return d;
}

void *memmove(void *d, const void *s, size_t n)
{
    uint8_t *dd = d;
    const uint8_t *ss = s;
    if (dd <= ss || dd >= ss + n)
        return memcpy(d, s, n);
    while (n--)
        dd[n] = ss[n];
    return d;
}

void *memset(void *d, int c, size_t n)
{
    uint8_t *dd = d;
    if (((uintptr_t)dd & 7) == 0) {
        uint64_t v = (uint8_t)c * 0x0101010101010101ull;
        for (; n >= 8; n -= 8, dd += 8)
            *(uint64_t *)dd = v;
    }
    while (n--)
        *dd++ = c;
    return d;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const uint8_t *x = a, *y = b;
    for (; n; n--, x++, y++)
        if (*x != *y)
            return *x - *y;
    return 0;
}

void *memchr(const void *s, int c, size_t n)
{
    const uint8_t *p = s;
    for (; n; n--, p++)
        if (*p == (uint8_t)c)
            return (void *)p;
    return NULL;
}

size_t strlen(const char *s)
{
    const char *p = s;
    while (*p)
        p++;
    return p - s;
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b)
        a++, b++;
    return (uint8_t)*a - (uint8_t)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
    for (; n; n--, a++, b++) {
        if (*a != *b || *a == 0)
            return (uint8_t)*a - (uint8_t)*b;
    }
    return 0;
}

int strcasecmp(const char *a, const char *b)
{
    while (*a && tolower(*a) == tolower(*b))
        a++, b++;
    return tolower((uint8_t)*a) - tolower((uint8_t)*b);
}

int strncasecmp(const char *a, const char *b, size_t n)
{
    for (; n; n--, a++, b++) {
        if (tolower(*a) != tolower(*b) || *a == 0)
            return tolower((uint8_t)*a) - tolower((uint8_t)*b);
    }
    return 0;
}

char *strcpy(char *d, const char *s)
{
    char *r = d;
    while ((*d++ = *s++))
        ;
    return r;
}

char *strncpy(char *d, const char *s, size_t n)
{
    size_t i = 0;
    for (; i < n && s[i]; i++)
        d[i] = s[i];
    for (; i < n; i++)
        d[i] = 0;
    return d;
}

char *strchr(const char *s, int c)
{
    for (;; s++) {
        if (*s == (char)c)
            return (char *)s;
        if (*s == 0)
            return NULL;
    }
}

char *strrchr(const char *s, int c)
{
    const char *r = NULL;
    for (;; s++) {
        if (*s == (char)c)
            r = s;
        if (*s == 0)
            return (char *)r;
    }
}

char *strstr(const char *h, const char *n)
{
    size_t len = strlen(n);
    for (; *h; h++)
        if (strncmp(h, n, len) == 0)
            return (char *)h;
    return len == 0 ? (char *)h : NULL;
}

size_t strcspn(const char *s, const char *reject)
{
    size_t n = 0;
    while (s[n] && !strchr(reject, s[n]))
        n++;
    return n;
}

char *strdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *d = malloc(n);
    if (d)
        memcpy(d, s, n);
    return d;
}

// ---------------- numbers ----------------

int abs(int x) { return x < 0 ? -x : x; }
double fabs(double x) { return x < 0 ? -x : x; }

long strtol(const char *s, char **end, int base)
{
    long v = 0;
    int neg = 0;
    while (isspace(*s))
        s++;
    if (*s == '-' || *s == '+')
        neg = *s++ == '-';
    if ((base == 0 || base == 16) && s[0] == '0' && (s[1] | 32) == 'x') {
        s += 2;
        base = 16;
    } else if (base == 0) {
        base = s[0] == '0' ? 8 : 10;
    }
    for (;; s++) {
        int d;
        if (isdigit(*s))
            d = *s - '0';
        else if (isalpha(*s))
            d = (*s | 32) - 'a' + 10;
        else
            break;
        if (d >= base)
            break;
        v = v * base + d;
    }
    if (end)
        *end = (char *)s;
    return neg ? -v : v;
}

int atoi(const char *s) { return strtol(s, NULL, 10); }

double atof(const char *s)
{
    double v = 0, scale = 1;
    int neg = 0;
    while (isspace(*s))
        s++;
    if (*s == '-' || *s == '+')
        neg = *s++ == '-';
    for (; isdigit(*s); s++)
        v = v * 10 + (*s - '0');
    if (*s == '.')
        for (s++; isdigit(*s); s++)
            v += (*s - '0') * (scale /= 10);
    return neg ? -v : v;
}

// Only what m_misc.c / m_config.c use: whitespace, literal characters,
// %d %i %u %x %o (into int).
int sscanf(const char *s, const char *fmt, ...)
{
    va_list ap;
    int n = 0;
    va_start(ap, fmt);
    while (*fmt) {
        if (isspace(*fmt)) {
            while (isspace(*s))
                s++;
            fmt++;
        } else if (*fmt != '%') {
            if (*s++ != *fmt++)
                break;
        } else {
            int base;
            char *end;
            fmt++;
            switch (*fmt++) {
            case 'd': case 'u': base = 10; break;
            case 'i': base = 0; break;
            case 'x': case 'X': base = 16; break;
            case 'o': base = 8; break;
            default: goto out;
            }
            long v = strtol(s, &end, base);
            if (end == s)
                break;
            s = end;
            *va_arg(ap, int *) = v;
            n++;
        }
    }
out:
    va_end(ap);
    return n;
}

// ---------------- printf ----------------

struct out {
    char *buf;      // NULL: count only
    size_t size;
    size_t len;
};

static void out_c(struct out *o, char c)
{
    if (o->buf && o->len + 1 < o->size)
        o->buf[o->len] = c;
    o->len++;
}

static void out_pad(struct out *o, char c, int n)
{
    while (n-- > 0)
        out_c(o, c);
}

// %[-0][width][.prec][hh|h|l|ll|z]d i u x X o c s p f %
int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
    struct out o = { buf, size, 0 };

    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            out_c(&o, *fmt);
            continue;
        }
        int left = 0, zero = 0, width = 0, prec = -1, lng = 0;
        for (;; fmt++) {
            if (fmt[1] == '-') left = 1;
            else if (fmt[1] == '0') zero = 1;
            else if (fmt[1] == ' ' || fmt[1] == '+' || fmt[1] == '#') ;
            else break;
        }
        fmt++;
        if (*fmt == '*') {
            width = va_arg(ap, int);
            fmt++;
        }
        while (isdigit(*fmt))
            width = width * 10 + (*fmt++ - '0');
        if (*fmt == '.') {
            prec = 0;
            fmt++;
            if (*fmt == '*') {
                prec = va_arg(ap, int);
                fmt++;
            }
            while (isdigit(*fmt))
                prec = prec * 10 + (*fmt++ - '0');
        }
        while (*fmt == 'l' || *fmt == 'h' || *fmt == 'z') {
            if (*fmt == 'l' || *fmt == 'z')
                lng = 1;
            fmt++;
        }

        char tmp[32];
        const char *str = tmp;
        int len = 0, neg = 0;
        unsigned long long u;

        switch (*fmt) {
        case 'd': case 'i': {
            long long v = lng ? va_arg(ap, long) : va_arg(ap, int);
            neg = v < 0;
            u = neg ? -(unsigned long long)v : v;
            goto number10;
        }
        case 'u':
            u = lng ? va_arg(ap, unsigned long) : va_arg(ap, unsigned);
        number10:
            do tmp[sizeof tmp - ++len] = '0' + u % 10; while (u /= 10);
            str = tmp + sizeof tmp - len;
            break;
        case 'p':
            lng = 1;
            /* fall through */
        case 'x': case 'X': case 'o': {
            int base = *fmt == 'o' ? 8 : 16;
            const char *digits = *fmt == 'X' ? "0123456789ABCDEF" : "0123456789abcdef";
            u = lng ? va_arg(ap, unsigned long) : va_arg(ap, unsigned);
            do tmp[sizeof tmp - ++len] = digits[u % base]; while (u /= base);
            str = tmp + sizeof tmp - len;
            break;
        }
        case 'c':
            tmp[0] = va_arg(ap, int);
            len = 1;
            break;
        case 's':
            str = va_arg(ap, const char *);
            if (str == NULL)
                str = "(null)";
            len = prec >= 0 ? (int)strnlen(str, prec) : (int)strlen(str);
            prec = -1;
            break;
        case 'f': case 'g': {
            double v = va_arg(ap, double);
            if (prec < 0)
                prec = 6;
            neg = v < 0;
            if (neg)
                v = -v;
            for (int i = 0; i < prec; i++)
                v *= 10;
            u = (unsigned long long)(v + 0.5);
            for (int i = 0; i < prec; i++, u /= 10)
                tmp[sizeof tmp - ++len] = '0' + u % 10;
            if (prec)
                tmp[sizeof tmp - ++len] = '.';
            do tmp[sizeof tmp - ++len] = '0' + u % 10; while (u /= 10);
            str = tmp + sizeof tmp - len;
            prec = -1;
            break;
        }
        case '%':
            tmp[0] = '%';
            len = 1;
            break;
        default:
            continue;
        }

        int digits0 = (prec > len) ? prec - len : 0;  // %.3d
        int total = len + digits0 + neg;
        if (!left && !zero)
            out_pad(&o, ' ', width - total);
        if (neg)
            out_c(&o, '-');
        if (!left && zero)
            out_pad(&o, '0', width - total);
        out_pad(&o, '0', digits0);
        for (int i = 0; i < len; i++)
            out_c(&o, str[i]);
        if (left)
            out_pad(&o, ' ', width - total);
    }
    if (buf && size)
        buf[o.len < size ? o.len : size - 1] = 0;
    return o.len;
}

size_t strnlen(const char *s, size_t n)
{
    size_t i = 0;
    while (i < n && s[i])
        i++;
    return i;
}

int snprintf(char *s, size_t n, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(s, n, fmt, ap);
    va_end(ap);
    return r;
}

int sprintf(char *s, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(s, (size_t)-1 >> 1, fmt, ap);
    va_end(ap);
    return r;
}

// ---------------- files ----------------

#define MAX_FILES 16

struct file {               // one entry of the RAM file system
    char name[64];
    uint8_t *data;
    size_t size;
    size_t cap;             // 0: read-only (data not from our heap)
};

struct FILE {
    struct file *file;      // NULL: the console
    size_t pos;
    int write;
    int eof;
};

static struct file files[MAX_FILES];
static FILE console = { NULL, 0, 1, 0 };
FILE *stdin = &console, *stdout = &console, *stderr = &console;

// "./doom1.wad", "/x/doom1.wad", "DOOM1.WAD" all mean the same file.
static const char *basename_of(const char *path)
{
    const char *s = strrchr(path, '/');
    return s ? s + 1 : path;
}

static struct file *file_find(const char *path)
{
    const char *name = basename_of(path);
    for (int i = 0; i < MAX_FILES; i++)
        if (files[i].name[0] && strcasecmp(files[i].name, name) == 0)
            return &files[i];
    return NULL;
}

static struct file *file_new(const char *path)
{
    for (int i = 0; i < MAX_FILES; i++)
        if (files[i].name[0] == 0) {
            strncpy(files[i].name, basename_of(path), sizeof files[i].name - 1);
            return &files[i];
        }
    return NULL;
}

void file_register(const char *name, void *data, size_t size)
{
    struct file *f = file_new(name);
    if (f) {
        f->data = data;
        f->size = size;
        f->cap = 0;
    }
}

FILE *fopen(const char *path, const char *mode)
{
    struct file *f = file_find(path);
    int write = mode[0] == 'w' || mode[0] == 'a';

    if (write) {
        if (f == NULL)
            f = file_new(path);
        if (f == NULL) {
            errno = ENOMEM;
            return NULL;
        }
        if (mode[0] == 'w' || f->cap == 0) {    // start a fresh heap copy
            f->data = NULL;
            f->size = 0;
            f->cap = 0;
        }
    } else if (f == NULL) {
        errno = ENOENT;
        return NULL;
    }

    FILE *s = malloc(sizeof *s);
    if (s == NULL)
        return NULL;
    s->file = f;
    s->pos = mode[0] == 'a' ? f->size : 0;
    s->write = write;
    s->eof = 0;
    return s;
}

int fclose(FILE *s)
{
    if (s != &console)
        free(s);
    return 0;
}

size_t fread(void *buf, size_t size, size_t n, FILE *s)
{
    if (s->file == NULL || size == 0)
        return 0;
    size_t avail = s->file->size > s->pos ? s->file->size - s->pos : 0;
    size_t count = avail / size < n ? avail / size : n;
    memcpy(buf, s->file->data + s->pos, count * size);
    s->pos += count * size;
    if (count < n)
        s->eof = 1;
    return count;
}

size_t fwrite(const void *buf, size_t size, size_t n, FILE *s)
{
    size_t bytes = size * n;
    if (s->file == NULL) {
        uart_write(buf, bytes);
        return n;
    }
    struct file *f = s->file;
    if (!s->write)
        return 0;
    if (s->pos + bytes > f->cap) {
        size_t cap = (s->pos + bytes) * 2 + 4096;
        uint8_t *d = malloc(cap);
        if (d == NULL)
            return 0;
        if (f->data)
            memcpy(d, f->data, f->size);
        f->data = d;
        f->cap = cap;
    }
    memcpy(f->data + s->pos, buf, bytes);
    s->pos += bytes;
    if (s->pos > f->size)
        f->size = s->pos;
    return n;
}

int fseek(FILE *s, long off, int whence)
{
    if (s->file == NULL)
        return -1;
    long base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? (long)s->pos : (long)s->file->size;
    if (base + off < 0)
        return -1;
    s->pos = base + off;
    s->eof = 0;
    return 0;
}

long ftell(FILE *s) { return s->pos; }
int fflush(FILE *s) { (void)s; return 0; }
int feof(FILE *s) { return s->eof; }

int fputs(const char *str, FILE *s)
{
    fwrite(str, 1, strlen(str), s);
    return 0;
}

int puts(const char *s)
{
    fputs(s, stdout);
    uart_write("\n", 1);
    return 0;
}

int putchar(int c)
{
    char ch = c;
    uart_write(&ch, 1);
    return c;
}

int vfprintf(FILE *s, const char *fmt, va_list ap)
{
    char buf[512];
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    if ((size_t)n < sizeof buf) {
        fwrite(buf, 1, n, s);
    } else {                    // longer than the stack buffer: format into the heap
        char *big = malloc(n + 1);
        if (big) {
            vsnprintf(big, n + 1, fmt, ap2);
            fwrite(big, 1, n, s);
            free(big);
        }
    }
    va_end(ap2);
    return n;
}

int fprintf(FILE *s, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = vfprintf(s, fmt, ap);
    va_end(ap);
    return r;
}

int printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = vfprintf(stdout, fmt, ap);
    va_end(ap);
    return r;
}

int remove(const char *path)
{
    struct file *f = file_find(path);
    if (f == NULL)
        return -1;
    f->name[0] = 0;
    return 0;
}

int rename(const char *from, const char *to)
{
    struct file *f = file_find(from);
    if (f == NULL)
        return -1;
    remove(to);
    strncpy(f->name, basename_of(to), sizeof f->name - 1);
    return 0;
}

int mkdir(const char *path, mode_t mode) { (void)path; (void)mode; return 0; }
int system(const char *cmd) { (void)cmd; return -1; }

// ---------------- exit ----------------

// A program can define platform_exit(), e.g. to return to the monitor;
// if it returns (or isn't defined), exit() does the default below.
void platform_exit(int status) __attribute__((weak));

void exit(int status)
{
    printf("\nexit(%d)\n", status);
    if (platform_exit)
        platform_exit(status);
    if (status == 0) {
        // Quit from the menu: software reset back to BOLT (docs/hardware/system-blocks.md)
        write32(RESET_A, 1);
        write32(RESET_B, 1);
    }
    for (;;)                    // error: stay here so the message stays on the console
        __asm__ volatile("wfe");
}

void abort(void) { exit(-1); }

void __assert_fail(const char *expr, const char *file, int line)
{
    printf("assert failed: %s (%s:%d)\n", expr, file, line);
    exit(-1);
}
