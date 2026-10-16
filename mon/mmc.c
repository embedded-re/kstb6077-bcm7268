// mmc.c - eMMC driver: SDHCI 1 at 0xf0200200 (SD Host Controller spec 3.00).
//
// BOLT leaves the card initialised and selected, in the "tran" state, on an
// 8-bit 50 MHz bus with 512-byte blocks and sector addressing, and `go -64`
// keeps that state (docs/hardware/storage.md). mmc_init() uses that state
// as it is (kboot depends on it); mmc_reset_card() (kmon `mmc init`) brings
// the host and the card from reset to the same state itself. Data moves by
// programmed I/O through the buffer data port.
//
// Safety:
//   - only these commands are ever sent: CMD0/1/2/3/7 (card identification,
//     mmc_reset_card() only), CMD6 (SWITCH) to BUS_WIDTH [183] and HS_TIMING
//     [185] only, CMD8 (read EXT_CSD), CMD13 (status), CMD17/18 (read),
//     CMD24/25 (write). No other CMD6 (could change the boot or partition
//     configuration), no erase or lock commands.
//   - writes are refused outside flash0.splash and flash0.DATA, and in the
//     first 512 KB of flash0.splash (the splash container) unless forced.

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "kstb.h"
#include "mmc.h"

#define HOST        0xf0200200
#define R_BLKSZ     (HOST + 0x04)   // [15:0] block size (+ SDMA boundary), [31:16] block count
#define R_ARG       (HOST + 0x08)
#define R_CMD       (HOST + 0x0c)   // [15:0] transfer mode, [31:16] command
#define R_RESP0     (HOST + 0x10)
#define R_DATA      (HOST + 0x20)   // buffer data port
#define R_PSTATE    (HOST + 0x24)   // b0 CMD inhibit, b1 DAT inhibit, b20 DAT0 level
#define R_HOSTCTL   (HOST + 0x28)   // [7:0] host control 1, [15:8] power control
#define R_CLK       (HOST + 0x2c)   // [15:0] clock, [23:16] timeout, [26:24] software reset
#define R_INT       (HOST + 0x30)   // [15:0] normal, [31:16] error; write 1 to clear
#define R_INT_EN    (HOST + 0x34)

#define INT_CMD_DONE    (1 << 0)
#define INT_XFER_DONE   (1 << 1)
#define INT_WRITE_READY (1 << 4)
#define INT_READ_READY  (1 << 5)
#define INT_ERROR       (1 << 15)

// command register flags
#define RSP_NONE    0x00
#define RSP_R2      0x09            // 136-bit response, CRC checked
#define RSP_R3      0x02            // 48-bit response, no checks (OCR)
#define RSP_R1      0x1a            // 48-bit response, CRC and index checked
#define RSP_R1B     0x1b            // the same, then busy on DAT0
#define DATA        0x20            // data present

// transfer mode
#define TM_BLKCNT   0x02
#define TM_ACMD12   0x04
#define TM_READ     0x10
#define TM_MULTI    0x20

#define TICKS_PER_MS 27000

static uint64_t ticks(void)
{
    uint64_t t;
    __asm__ volatile("isb; mrs %0, cntpct_el0" : "=r"(t));
    return t;
}

static uint32_t last_int;           // interrupt status of the last failure

// Recover from an error: reset the CMD and DAT state machines.
static void reset_lines(void)
{
    write32(R_CLK, read32(R_CLK) | (6u << 24));
    uint64_t end = ticks() + 100 * TICKS_PER_MS;
    while ((read32(R_CLK) & (6u << 24)) && ticks() < end)
        ;
    write32(R_INT, 0xffffffff);
}

// Wait for any of `bits` in the interrupt status, or an error / timeout.
static int wait_int(uint32_t bits, uint32_t ms)
{
    uint64_t end = ticks() + (uint64_t)ms * TICKS_PER_MS;
    for (;;) {
        uint32_t st = read32(R_INT);
        if (st & INT_ERROR) {
            last_int = st;
            reset_lines();
            return MMC_EIO;
        }
        if (st & bits)
            return 0;
        if (ticks() > end) {
            last_int = st;
            reset_lines();
            return MMC_ETIMEOUT;
        }
    }
}

static int send(int index, uint32_t arg, uint32_t flags, uint32_t mode, uint32_t *resp)
{
    uint64_t end = ticks() + 100 * TICKS_PER_MS;
    uint32_t busy = flags & DATA || (flags & 3) == 3 ? 3 : 1;   // CMD inhibit (+ DAT inhibit for data, busy)
    while (read32(R_PSTATE) & busy)
        if (ticks() > end) {
            last_int = read32(R_INT);
            reset_lines();
            return MMC_EBUSY;
        }
    write32(R_INT, 0xffffffff);
    write32(R_ARG, arg);
    write32(R_CMD, (uint32_t)index << 24 | flags << 16 | mode);
    int r = wait_int(INT_CMD_DONE, 100);
    if (r)
        return r;
    write32(R_INT, INT_CMD_DONE);
    if (resp)
        *resp = read32(R_RESP0);
    return 0;
}

int mmc_init(void)
{
    // Buffer read/write ready must be enabled to show up in the status.
    // BOLT leaves 0x13ff060f; signals to the GIC stay off (R_INT_EN + 4 = 0).
    write32(R_INT_EN, read32(R_INT_EN) | INT_READ_READY | INT_WRITE_READY);
    if (!(read32(R_PSTATE) & (1 << 16)))
        return MMC_ENOCARD;
    return 0;
}

static void delay_ms(uint32_t ms)
{
    uint64_t end = ticks() + (uint64_t)ms * TICKS_PER_MS;
    while (ticks() < end)
        ;
}

// Card clock = 100 MHz base / (2 * n), 10-bit divider (spec 3.00); data
// timeout field 0xe as BOLT leaves it.
static int set_clock(uint32_t n)
{
    write32(R_CLK, 0);                              // SD clock off before the change
    write32(R_CLK, 0xe0000 | (n & 0xff) << 8 | (n >> 8 & 3) << 6 | 1);
    uint64_t end = ticks() + 100 * TICKS_PER_MS;
    while (!(read32(R_CLK) & 2))                    // internal clock stable
        if (ticks() > end)
            return MMC_ETIMEOUT;
    write32(R_CLK, read32(R_CLK) | 4);              // SD clock on
    return 0;
}

// Wait for the end of a busy signal (R1b), then for the card in tran.
static int wait_tran(uint32_t rca)
{
    uint64_t end = ticks() + 1000 * TICKS_PER_MS;
    uint32_t st;
    while (read32(R_PSTATE) & 2)                    // DAT inhibit: busy on DAT0
        if (ticks() > end)
            return MMC_EBUSY;
    write32(R_INT, INT_XFER_DONE);
    do {
        int r = send(13, rca << 16, RSP_R1, 0, &st);
        if (r)
            return r;
        if (st & 0xfdf98080)                        // error bits, SWITCH_ERROR (bit 7)
            return last_int = st, MMC_EIO;
        if (ticks() > end)
            return MMC_ETIMEOUT;
    } while ((st >> 9 & 0xf) != 4);
    return 0;
}

// CMD6 SWITCH, write byte: only BUS_WIDTH [183] and HS_TIMING [185], both
// volatile (back to their defaults at the next power-on or CMD0).
static int mmc_switch(uint32_t index, uint32_t value, uint32_t rca)
{
    if (index != 183 && index != 185)
        return MMC_EGUARD;
    int r = send(6, 3u << 24 | index << 16 | (value & 0xff) << 8, RSP_R1B, 0, NULL);
    return r ? r : wait_tran(rca);
}

int mmc_reset_card(uint32_t cid[4])
{
    const uint32_t rca = 2;                         // the address BOLT gives the card

    // Host: software reset of everything, interrupt status as BOLT + buffer
    // ready, 3.3 V power on, 1-bit bus, 400 kHz.
    write32(R_CLK, 1u << 24);
    uint64_t end = ticks() + 100 * TICKS_PER_MS;
    while (read32(R_CLK) & (1u << 24))
        if (ticks() > end)
            return MMC_ETIMEOUT;
    write32(R_INT_EN, 0x13ff063f);
    write32(R_HOSTCTL, 0x0e00);                     // 3.3 V
    write32(R_HOSTCTL, 0x0f00);                     // power on
    int r = set_clock(125);
    if (r)
        return r;
    delay_ms(10);                                   // power ramp, >= 74 clocks

    // Card: idle, then CMD1 until powered up (sector addressing, 2.7-3.6 V).
    if ((r = send(0, 0, RSP_NONE, 0, NULL)))
        return r;
    delay_ms(1);
    uint32_t ocr = 0;
    for (int i = 0; !(ocr & (1u << 31)); i++) {
        if (i == 1000)
            return MMC_ETIMEOUT;
        if ((r = send(1, 0x40ff8000, RSP_R3, 0, &ocr)))
            return r;
        delay_ms(1);
    }
    if ((r = send(2, 0, RSP_R2, 0, NULL)))
        return r;
    for (int i = 0; i < 4; i++)
        cid[i] = read32(R_RESP0 + 4 * i);
    if ((r = send(3, rca << 16, RSP_R1, 0, NULL)) ||
        (r = send(7, rca << 16, RSP_R1B, 0, NULL)) || (r = wait_tran(rca)))
        return r;

    // High speed at 50 MHz, then the 8-bit bus.
    if ((r = mmc_switch(185, 1, rca)))
        return r;
    write32(R_HOSTCTL, 0x0f04);
    if ((r = set_clock(1)) || (r = mmc_switch(183, 2, rca)))
        return r;
    write32(R_HOSTCTL, 0x0f24);
    return 0;
}

uint32_t mmc_last_int(void)
{
    return last_int;
}

int mmc_status(uint32_t rca, uint32_t *status)
{
    return send(13, rca << 16, RSP_R1, 0, status);
}

static int read_blocks(int index, uint32_t arg, uint32_t n, uint32_t mode, void *buf)
{
    write32(R_BLKSZ, n << 16 | 0x7200);             // 512-byte blocks (BOLT's 0x7200)
    int r = send(index, arg, RSP_R1 | DATA, mode, NULL);
    if (r)
        return r;
    uint32_t *p = buf;
    for (uint32_t b = 0; b < n; b++) {
        if ((r = wait_int(INT_READ_READY, 1000)))
            return r;
        write32(R_INT, INT_READ_READY);
        for (int i = 0; i < 128; i++)
            *p++ = read32(R_DATA);
    }
    if ((r = wait_int(INT_XFER_DONE, 1000)))
        return r;
    write32(R_INT, INT_XFER_DONE);
    return 0;
}

int mmc_read(uint32_t lba, uint32_t n, void *buf)
{
    while (n) {
        uint32_t k = n > 0xffff ? 0xffff : n;
        int r = k == 1 ? read_blocks(17, lba, 1, TM_READ, buf)
                       : read_blocks(18, lba, k, TM_READ | TM_MULTI | TM_BLKCNT | TM_ACMD12, buf);
        if (r)
            return r;
        lba += k, n -= k;
        buf = (uint8_t *)buf + k * 512;
    }
    return 0;
}

int mmc_read_ext_csd(uint8_t *buf)
{
    return read_blocks(8, 0, 1, TM_READ, buf);
}

int mmc_write_allowed(uint32_t lba, uint32_t n, int force)
{
    if (n && lba >= DATA_FIRST && lba + n - 1 <= DATA_LAST)
        return 0;
    if (n == 0 || lba < SPLASH_FIRST || lba + n - 1 > SPLASH_LAST)
        return MMC_EGUARD;
    if (!force && lba < SPLASH_FIRST + SPLASH_PROTECT)
        return MMC_EGUARD;
    return 0;
}

int mmc_write(uint32_t lba, uint32_t n, const void *buf, int force)
{
    int r = mmc_write_allowed(lba, n, force);
    if (r)
        return r;
    while (n) {
        uint32_t k = n > 0xffff ? 0xffff : n;
        write32(R_BLKSZ, k << 16 | 0x7200);
        r = k == 1 ? send(24, lba, RSP_R1 | DATA, 0, NULL)
                   : send(25, lba, RSP_R1 | DATA, TM_MULTI | TM_BLKCNT | TM_ACMD12, NULL);
        if (r)
            return r;
        const uint32_t *p = buf;
        for (uint32_t b = 0; b < k; b++) {
            if ((r = wait_int(INT_WRITE_READY, 1000)))
                return r;
            write32(R_INT, INT_WRITE_READY);
            for (int i = 0; i < 128; i++)
                write32(R_DATA, *p++);
        }
        if ((r = wait_int(INT_XFER_DONE, 5000)))    // includes the card's programming time
            return r;
        write32(R_INT, INT_XFER_DONE);
        uint64_t end = ticks() + 5000ull * TICKS_PER_MS;
        while (read32(R_PSTATE) & 2)                // DAT inhibit: card still busy
            if (ticks() > end)
                return MMC_EBUSY;
        lba += k, n -= k;
        buf = (const uint8_t *)buf + k * 512;
    }
    return 0;
}

const char *mmc_strerror(int r)
{
    switch (r) {
    case MMC_EIO:      return "error status";
    case MMC_ETIMEOUT: return "timeout";
    case MMC_EBUSY:    return "controller/card busy";
    case MMC_ENOCARD:  return "no card";
    case MMC_EGUARD:   return "refused: outside flash0.splash and flash0.DATA, or in splash's first 512 KB (use '!')";
    }
    return "ok";
}
