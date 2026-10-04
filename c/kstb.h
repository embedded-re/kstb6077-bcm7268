// kstb.h - addresses on the KSTB6077 and two helpers to use them.

#define UART0      0xf040c000   // serial console
#define CHIP_ID    0xf0404000   // reads 72680010
#define TEMP       0xf04d1500   // temperature sensor
#define SECONDS    0xf041a084   // counts seconds since power-on
#define LED        0xf0419c84   // LEDs: bit 18 = green, 17 = red, 16 = blue, 0 = on
#define LED_ODEN   0xf0419c80   // 1 = open-drain
#define LED_IODIR  0xf0419c88   // 1 = input, 0 = output

// read / write one 32-bit hardware register
#define read32(addr)       (*(volatile unsigned int *)(addr))
#define write32(addr, val) (*(volatile unsigned int *)(addr) = (val))
