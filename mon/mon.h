// mon.h - shared declarations of the monitor.
#include <stdint.h>

struct reg {
    const char *name;
    uint32_t addr;
    const char *desc;
};

#define DANGER_R 1
#define DANGER_W 2

struct danger {
    uint32_t start, end;    // inclusive
    int what;
    const char *why;
};

struct irkey {
    uint8_t cmd;
    const char *name;
};

extern const struct reg regs[];
extern const struct danger dangers[];
extern const struct irkey irkeys[];

// start.S
int safe_read(uint64_t addr, int size, uint64_t *val);
int safe_write(uint64_t addr, int size, uint64_t val);
void run_program(uint64_t entry) __attribute__((noreturn));
void dcache_flush_all(void);
extern uint64_t exc_frame[37];

// irq.c
void irq_cmd(int argc, char **argv, int (*key)(void));
void irq_off(void);
int irq_uart_on(void);
int irq_rx_ready(void);
int irq_rx_get(void);
void irq_wait_rx(void);
int irq_ir_on(void);

// mon.c
void ir_setup(void);
uint32_t crc32(const uint8_t *p, unsigned long n);

// fbcon.c
void con_init(void);
void con_off(void);
void con_clear(void);
void con_write(const char *s, unsigned long n);
void con_status(void);
void con_scroll(int lines);
int con_rows(void);

#include "usb.h"                        // ../usb
#include "fat.h"                        // ../fat
