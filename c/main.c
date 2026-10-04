// main.c - bare-metal C on the KSTB6077
//
// build: tools/build-c c/main.c
// run:   tools/kstb-run --a64 --watchdog 60 --listen 30 --wait-bolt c/main.bin

#include "kstb.h"

// BOLT jumps here (first byte of the file). Give C a stack, call main.
__asm__(
"   .global _start      \n"
"_start:                \n"
"   mov  x0, #0x02000000\n"   // stack at 0x02000000 (free RAM), grows down
"   mov  sp, x0         \n"
"   bl   main           \n"
"1: b    1b             \n"   // main returned: stay here
);

void putc(char c)
{
    while ((read32(UART0 + 0x14) & 0x20) == 0)  // wait until UART has room
        ;
    write32(UART0, c);
}

void puts(const char *s)
{
    while (*s)
        putc(*s++);
}

void puthex(unsigned int v)
{
    for (int i = 28; i >= 0; i -= 4)
        putc("0123456789abcdef"[(v >> i) & 0xf]);
}

void putdec(unsigned int v)
{
    char buf[10];
    int n = 0;
    do {
        buf[n++] = '0' + v % 10;
        v /= 10;
    } while (v);
    while (n)
        putc(buf[--n]);
}

void wait_seconds(unsigned int s)
{
    unsigned int end = read32(SECONDS) + s;
    while (read32(SECONDS) < end)
        ;
}

int main(void)
{
    puts("\r\nHello from C!\r\n");

    puts("chip id: ");
    puthex(read32(CHIP_ID));
    puts("\r\n");

    unsigned int code = (read32(TEMP) >> 1) & 0x3ff;
    puts("temperature: ");
    putdec((410040 - code * 487) / 1000);
    puts(" C\r\n");

    puts("red\r\n");
    write32(LED, (read32(LED) | (1 << 18)) & ~(1 << 17));
    wait_seconds(2);

    puts("green\r\n");
    write32(LED, (read32(LED) | (1 << 17)) & ~(1 << 18));
    wait_seconds(2);

    puts("orange\r\n");
    write32(LED, read32(LED) & ~((1 << 17) | (1 << 18)));
    wait_seconds(2);

    puts("off\r\n");
    write32(LED, read32(LED) | (1 << 17) | (1 << 18));
    wait_seconds(2);

    // blue: BOLT leaves its pin as an input, make it an output first
    write32(LED_ODEN, read32(LED_ODEN) | (1 << 16));
    write32(LED, read32(LED) | (1 << 16));             // off before it drives
    write32(LED_IODIR, read32(LED_IODIR) & ~(1 << 16));
    puts("blue\r\n");
    write32(LED, read32(LED) & ~(1 << 16));
    wait_seconds(2);

    puts("blue + green\r\n");
    write32(LED, read32(LED) & ~(1 << 18));
    wait_seconds(2);

    puts("done\r\n");
    return 0;
}
