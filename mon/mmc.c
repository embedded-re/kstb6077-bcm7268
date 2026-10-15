// mmc.c - eMMC driver: SDHCI 1 at 0xf0200200 (SD Host Controller spec 3.00).
//
// BOLT leaves the card initialised and selected, in the "tran" state, on an
// 8-bit 50 MHz bus with 512-byte blocks and sector addressing, and `go -64`
// keeps that state (docs/hardware/storage.md). So there is no card
// initialisation here: only data commands, by programmed I/O through the
// buffer data port.
//
// Safety:
//   - only these commands are ever sent: CMD8 (read EXT_CSD), CMD13 (status),
//     CMD17/18 (read), CMD24/25 (write). Never CMD6 (SWITCH: could change the
//     boot or partition configuration), erase, or lock commands.
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
#define R_CLK       (HOST + 0x2c)   // [15:0] clock, [23:16] timeout, [26:24] software reset
#define R_INT       (HOST + 0x30)   // [15:0] normal, [31:16] error; write 1 to clear
#define R_INT_EN    (HOST + 0x34)

#define INT_CMD_DONE    (1 << 0)
#define INT_XFER_DONE   (1 << 1)
#define INT_WRITE_READY (1 << 4)
#define INT_READ_READY  (1 << 5)
#define INT_ERROR       (1 << 15)

// command register flags
#define RSP_R1      0x1a            // 48-bit response, CRC and index checked
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
    uint32_t busy = flags & DATA ? 3 : 1;           // CMD inhibit (+ DAT inhibit for data)
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
