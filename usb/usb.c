// usb.c - OHCI1 (the USB-A port's low/full-speed controller), hubs, and a
// HID boot-protocol keyboard, polled (docs/hardware/usb.md).
//
// OHCI 1.0a: the controller walks lists of endpoint descriptors (ED) and
// transfer descriptors (TD) in RAM by DMA. They live in a non-cacheable 2 MB
// block (DMA_BASE, start.S), so no cache maintenance is needed.
//
//   usb_init()  USB clocks on, every USB host controller reset by the
//               control block (not BOLT's state), port power and reset, then enumeration:
//               GET_DESCRIPTOR, SET_ADDRESS, configuration; a hub's ports are
//               powered, reset and enumerated in turn (addresses 1, 2, ...);
//               the first boot keyboard and the first boot mouse found get
//               boot protocol and are polled
//   usb_kbd_poll()  one 8-byte report from the keyboard's interrupt endpoint, if any
//   usb_mouse_poll()  the same for a boot-protocol mouse (buttons, x, y)
//   usb_msc_*() a bulk-only mass storage device (a USB stick): SCSI
//               INQUIRY, TEST UNIT READY, READ CAPACITY(10), READ(10), WRITE(10)
//   usb_port_changed()  1 when the root port changed (enumerate again); a hub
//               port (one per call) is handled here: a new device is
//               enumerated, a removed one (and what hung below it) forgotten
//
// Shared by mon/ and doom/ (like libc/); each builds it with its own kstb.h.

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "kstb.h"                       // the program's: DMA_BASE
#include "usb.h"

int usb_verbose = 1;                    // 0: print nothing (kmon starts it quietly)
#define say(...) do { if (usb_verbose) printf(__VA_ARGS__); } while (0)

// USB control block (docs/hardware/usb.md, "Our own USB start"). BOLT never
// writes it; USB_PM bits 20/21/22 hold EHCI0+OHCI0 / EHCI1+OHCI1 / xHCI in
// reset while 0 (bit 23, the device controller, stays in reset).
#define USB_PM          0xf0b00234
#define USB_PM_HC_RESETB 0x00700000
// USB 2.0 clock gates (DTB sw_usb20): usb0_freerun (bit 0, 1 = off),
// usb0_gisb (bit 0), sys_108/54/scb_usb20 (bits 0-2)
#define CLK_USB_FREERUN 0xf04e049c
#define CLK_USB_GISB    0xf04e04a4
#define CLK_USB20       0xf04e04ac

#define OHCI            0xf0b00600
#define HC_CONTROL      (OHCI + 0x04)
#define HC_CMDSTATUS    (OHCI + 0x08)
#define HC_INTDISABLE   (OHCI + 0x14)
#define HC_HCCA         (OHCI + 0x18)
#define HC_CTRLHEAD     (OHCI + 0x20)
#define HC_CTRLCUR      (OHCI + 0x24)
#define HC_BULKHEAD     (OHCI + 0x28)
#define HC_BULKCUR      (OHCI + 0x2c)
#define HC_FMINTERVAL   (OHCI + 0x34)
#define HC_FMNUMBER     (OHCI + 0x3c)
#define HC_PERIODSTART  (OHCI + 0x40)
#define HC_RHSTATUS     (OHCI + 0x50)
#define HC_PORT1        (OHCI + 0x54)

#define CTL_PLE         (1u << 2)       // periodic (interrupt) list on
#define CTL_CLE         (1u << 4)       // control list on
#define CTL_BLE         (1u << 5)       // bulk list on
#define CTL_OPER        (2u << 6)       // HCFS = Operational

// DMA area layout (all uncached)
#define HCCA            (DMA_BASE + 0x000)      // 256 bytes, 256-aligned
#define ED_CTRL         (DMA_BASE + 0x100)
#define ED_INTR         (DMA_BASE + 0x110)      // keyboard; its NextED is the mouse's
#define ED_MOUSE        (DMA_BASE + 0x140)
#define TD_POOL         (DMA_BASE + 0x200)      // 16 TDs of 16 bytes
#define SETUP_BUF       (DMA_BASE + 0x400)
#define DATA_BUF        (DMA_BASE + 0x800)      // 256 bytes
#define REPORT_BUF      (DMA_BASE + 0xc00)      // keyboard 2 x 16 bytes, mouse 2 x 16 at +0x20
#define ED_BULK_OUT     (DMA_BASE + 0x120)      // mass storage: bulk OUT and IN endpoints
#define ED_BULK_IN      (DMA_BASE + 0x130)
#define BULK_TD         (DMA_BASE + 0x1000)     // 2 x 32 TDs
#define CBW_BUF         (DMA_BASE + 0x1800)     // 31-byte command block wrapper
#define CSW_BUF         (DMA_BASE + 0x1840)     // 13-byte command status wrapper
#define SENSE_BUF       (DMA_BASE + 0x1880)
#define BULK_BUF        (DMA_BASE + 0x10000)    // 64 KB data, page-aligned
#define BULK_MAX        0x10000

#define W(a)            (*(volatile uint32_t *)(uintptr_t)(a))

// TD dword 0
#define TD_R            (1u << 18)              // a short packet is not an error
#define TD_SETUP        (0u << 19)
#define TD_OUT          (1u << 19)
#define TD_IN           (2u << 19)
#define TD_NOINT        (7u << 21)              // DelayInterrupt: none
#define TD_DATA0        (2u << 24)              // toggle from the TD: DATA0
#define TD_DATA1        (3u << 24)
#define TD_CC(d0)       ((d0) >> 28)

#define TICKS_PER_MS    27000

static uint64_t now(void)
{
    uint64_t t;
    __asm__ volatile("isb; mrs %0, cntpct_el0" : "=r"(t));
    return t;
}

static void wait_ms(uint32_t ms)
{
    uint64_t end = now() + (uint64_t)ms * TICKS_PER_MS;
    while (now() < end)
        ;
}

static const char *const cc_names[16] = {
    "ok", "CRC", "bit stuffing", "toggle mismatch", "STALL", "no response",
    "PID check", "unexpected PID", "data overrun", "data underrun", "cc10", "cc11",
    "buffer overrun", "buffer underrun", "not accessed", "not accessed",
};

// ---------------- state ----------------

#define MAX_DEV         8
#define MAX_HUB_DEPTH   2

struct dev {
    int addr, lowspeed, mps0;           // addr 0: free slot
    int parent, port;                   // hub's device index and port (parent -1: the root port)
    uint16_t vid, pid;
    uint8_t cls;                        // 09 hub, 03 keyboard, 02 mouse, 08 mass storage, 0 other
    int nports;                         // hubs: number of ports
    int bulk_in, bulk_out, bulk_mps;    // mass storage (bulk-only) endpoints, interface
    int msc_iface;
};
static struct dev devs[MAX_DEV];
static int ndev;                        // slots used so far (some may be free again)
static int next_addr;
static struct dev *cur;                 // the device control() talks to

// A HID boot device's interrupt IN endpoint: one ED on the periodic list
// (all 32 HCCA slots -> keyboard ED -> mouse ED), sKipped while unused; two
// TDs swapped as the active one and the tail.
struct hid {
    const char *what;
    uint32_t ed, td_a, td_b, buf;
    int dev, ep, mps, iface, interval;  // dev -1: none found
    int up, err, n;                     // polled; failed last time (print once); reports
    uint32_t td, dummy;
};
static struct hid kbd = { "keyboard", ED_INTR, TD_POOL + 64, TD_POOL + 80, REPORT_BUF, -1 };
static struct hid mouse = { "mouse", ED_MOUSE, TD_POOL + 96, TD_POOL + 112, REPORT_BUF + 0x20, -1 };
static int hub_poll_dev, hub_poll_port; // usb_port_changed: the next hub port to check
static int msc_dev = -1;                // the first mass storage device found
static int msc_ready;                   // usb_msc_init() done for it
static uint32_t msc_blocks, msc_bsize, msc_tag;

static void td_fill(uint32_t td, uint32_t d0, uint32_t buf, uint32_t len, uint32_t next)
{
    W(td + 0) = d0 | (0xfu << 28);      // CC = not accessed
    W(td + 4) = len ? buf : 0;
    W(td + 8) = next;
    W(td + 12) = len ? buf + len - 1 : 0;
}

// ED dword 0: function address, endpoint, speed, max packet size. A
// low-speed device behind a full-speed hub: the controller sends the PRE
// preamble itself.
static uint32_t ed_d0(const struct dev *d, int ep, int mps)
{
    return d->addr | (ep << 7) | (d->lowspeed ? 1u << 13 : 0) | ((uint32_t)mps << 16);
}

// ---------------- control transfers ----------------

// One control transfer to the device cur on the control list. Returns the
// number of bytes received/sent in the data stage, or -1 (and prints why).
static int control(uint8_t rtype, uint8_t req, uint16_t value, uint16_t index,
                   uint16_t len, void *data)
{
    uint8_t *setup = (uint8_t *)SETUP_BUF;
    setup[0] = rtype; setup[1] = req;
    setup[2] = value; setup[3] = value >> 8;
    setup[4] = index; setup[5] = index >> 8;
    setup[6] = len; setup[7] = len >> 8;
    int in = rtype & 0x80;
    if (!in && len)
        memcpy((void *)DATA_BUF, data, len);

    uint32_t td0 = TD_POOL, td1 = TD_POOL + 16, td2 = TD_POOL + 32, dummy = TD_POOL + 48;
    td_fill(td0, TD_SETUP | TD_NOINT | TD_DATA0, SETUP_BUF, 8, len ? td1 : td2);
    td_fill(td1, (in ? TD_IN : TD_OUT) | TD_R | TD_NOINT | TD_DATA1, DATA_BUF, len, td2);
    td_fill(td2, (in && len ? TD_OUT : TD_IN) | TD_NOINT | TD_DATA1, 0, 0, dummy);
    td_fill(dummy, 0, 0, 0, 0);

    W(ED_CTRL + 0) = ed_d0(cur, 0, cur->mps0) | (1u << 14); // sKip while editing
    W(ED_CTRL + 4) = dummy;                                 // TailP
    W(ED_CTRL + 8) = td0;                                   // HeadP, toggle carry 0
    W(ED_CTRL + 12) = 0;
    W(ED_CTRL + 0) = ed_d0(cur, 0, cur->mps0);
    __asm__ volatile("dsb sy" ::: "memory");
    W(HC_CONTROL) = W(HC_CONTROL) & ~CTL_CLE;      // the list pointers may only change with CLE off
    wait_ms(1);
    W(HC_CTRLHEAD) = ED_CTRL;
    W(HC_CTRLCUR) = 0;
    W(HC_CONTROL) = W(HC_CONTROL) | CTL_CLE;
    W(HC_CMDSTATUS) = 1u << 1;                              // CLF: control list filled

    uint64_t end = now() + 500ull * TICKS_PER_MS;
    while ((W(ED_CTRL + 8) & ~0xfu) != dummy && !(W(ED_CTRL + 8) & 1)) {
        if (now() > end) {
            say("usb: control %02x %02x: timeout (ED head %08x)\n", rtype, req, W(ED_CTRL + 8));
            W(ED_CTRL + 0) |= 1u << 14;
            return -1;
        }
    }
    W(ED_CTRL + 0) |= 1u << 14;
    uint32_t td[3] = { td0, td1, td2 };
    for (int i = 0; i < 3; i++) {
        uint32_t cc = TD_CC(W(td[i]));
        if (i == 1 && !len)
            continue;
        if (cc != 0 && !(i == 1 && cc == 9)) {               // underrun = short packet: fine with R
            say("usb: control %02x %02x stage %d: %s\n", rtype, req, i, cc_names[cc]);
            W(ED_CTRL + 8) = dummy;                         // clear the halt
            return -1;
        }
    }
    int got = len;
    if (len && W(td1 + 4))                                  // CBP left non-zero: short
        got = W(td1 + 4) - DATA_BUF;
    if (in && got)
        memcpy(data, (void *)DATA_BUF, got);
    return got;
}

static int get_descriptor(int type, int len, void *buf)
{
    return control(0x80, 6, type << 8, 0, len, buf);
}

// ---------------- bring-up ----------------

static void hc_start(void)
{
    W(CLK_USB_FREERUN) &= ~1u;
    W(CLK_USB_GISB) |= 1;
    W(CLK_USB20) |= 7;
    // All host controllers to their hardware reset state; nothing may read
    // them while they are held (a read then hangs instead of aborting).
    uint32_t pm = W(USB_PM);
    W(USB_PM) = pm & ~USB_PM_HC_RESETB;
    wait_ms(1);
    W(USB_PM) = pm | USB_PM_HC_RESETB;
    wait_ms(1);
    W(HC_CMDSTATUS) = 1;                        // HCR: controller reset
    wait_ms(1);
    memset((void *)DMA_BASE, 0, 0x1000);
    W(HC_INTDISABLE) = 0xc000007f;              // polled: no interrupts
    W(HC_HCCA) = HCCA;
    W(HC_FMINTERVAL) = 0x27782edf;              // values from the tested probe (usb.md)
    W(HC_PERIODSTART) = 0x2a2f;
    W(HC_CONTROL) = CTL_OPER;
    W(HC_RHSTATUS) = 0x10000;                   // LPSC: power on
    W(HC_PORT1) = 0x100;                        // SetPortPower
    wait_ms(20);
}

// Root port reset; 0 or 1 (low speed) when a device is there and the port
// is enabled, -1 when not.
static int port_reset(void)
{
    uint32_t ps = W(HC_PORT1);
    if (!(ps & 1)) {
        say("usb: nothing connected to the USB-A port (port status %08x)\n", ps);
        return -1;
    }
    wait_ms(100);                               // debounce
    W(HC_PORT1) = 0x10;                         // SetPortReset
    uint64_t end = now() + 100ull * TICKS_PER_MS;
    while (!(W(HC_PORT1) & (1u << 20)))
        if (now() > end) {
            say("usb: port reset didn't finish (port status %08x)\n", W(HC_PORT1));
            return -1;
        }
    ps = W(HC_PORT1);
    W(HC_PORT1) = 0x1f0000;                     // clear the change bits
    wait_ms(10);                                // reset recovery
    int ls = (ps >> 9) & 1;
    say("usb: port %08x: device connected, %s speed, enabled %s\n", ps,
           ls ? "low" : "full", ps & 2 ? "yes" : "no");
    return ps & 2 ? ls : -1;
}

// Walk a configuration: note a boot keyboard interface and its interrupt IN
// endpoint (if no keyboard yet), a hub, a bulk-only mass storage interface.
static void parse_config(struct dev *dv, const uint8_t *c, int n)
{
    int kind = 0, idx = dv - devs;              // kind of the current interface
    for (int i = 0; i + 1 < n && c[i]; i += c[i]) {
        if (c[i + 1] == 4) {                    // interface
            say("usb:   interface %d: class %02x subclass %02x protocol %02x, %d endpoints\n",
                   c[i + 2], c[i + 5], c[i + 6], c[i + 7], c[i + 4]);
            kind = 0;
            if (c[i + 5] == 9)
                kind = 9, dv->cls = 9;
            else if (c[i + 5] == 3 && c[i + 6] == 1 && c[i + 7] == 1 && kbd.dev < 0)
                kind = 3, dv->cls = dv->cls ? dv->cls : 3, kbd.iface = c[i + 2], kbd.ep = 0;
            else if (c[i + 5] == 3 && c[i + 6] == 1 && c[i + 7] == 2 && mouse.dev < 0)
                kind = 2, dv->cls = dv->cls ? dv->cls : 2, mouse.iface = c[i + 2], mouse.ep = 0;
            else if (c[i + 5] == 8 && c[i + 6] == 6 && c[i + 7] == 0x50 && !dv->cls)
                kind = 8, dv->cls = 8, dv->msc_iface = c[i + 2];
        } else if (c[i + 1] == 5) {             // endpoint
            int ea = c[i + 2], type = c[i + 3] & 3, mps = c[i + 4] | c[i + 5] << 8;
            say("usb:     endpoint %02x: type %d, max packet %d, interval %d ms\n",
                   ea, type, mps, c[i + 6]);
            struct hid *h = kind == 3 ? &kbd : kind == 2 ? &mouse : NULL;
            if (h && !h->ep && (ea & 0x80) && type == 3) {
                h->ep = ea & 0xf, h->mps = mps, h->interval = c[i + 6];
                h->dev = idx;
            } else if (kind == 8 && type == 2) {
                if (ea & 0x80)
                    dv->bulk_in = ea & 0xf;
                else
                    dv->bulk_out = ea & 0xf;
                dv->bulk_mps = mps;
            }
        }
    }
}

static void intr_queue(struct hid *h)
{
    // The TD the controller just finished becomes the new tail; the old tail
    // gets the next IN request (the standard OHCI tail swap).
    uint32_t td = h->dummy, dummy = h->td;
    uint32_t buf = h->buf + (td == h->td_a ? 0 : 16);
    td_fill(dummy, 0, 0, 0, 0);
    td_fill(td, TD_IN | TD_R | TD_NOINT, buf, h->mps > 16 ? 16 : h->mps, dummy);
    // toggle: from the ED's carry (T = 0x)
    __asm__ volatile("dsb sy" ::: "memory");
    h->td = td, h->dummy = dummy;
    W(h->ed + 4) = dummy;
}

// ---------------- hubs ----------------

#define HUB_PORT_STATUS(p, st)  control(0xa3, 0, 0, (p), 4, (st))
#define HUB_SET(p, f)           control(0x23, 3, (f), (p), 0, NULL)
#define HUB_CLEAR(p, f)         control(0x23, 1, (f), (p), 0, NULL)
#define F_PORT_RESET     4
#define F_PORT_POWER     8
#define F_C_CONNECTION  16
#define F_C_RESET       20

static int enumerate(int lowspeed, int parent, int port, int depth);

// A hub port: reset it and enumerate what is on it. The device on it gets
// the next address.
static int hub_port_reset(int hub, int p, int depth)
{
    uint8_t st[4];
    cur = &devs[hub];
    HUB_CLEAR(p, F_C_CONNECTION);
    wait_ms(100);                               // debounce
    if (HUB_SET(p, F_PORT_RESET) < 0)
        return -1;
    uint64_t end = now() + 500ull * TICKS_PER_MS;
    do {
        wait_ms(10);
        if (HUB_PORT_STATUS(p, st) < 4)
            return -1;
        if (now() > end) {
            say("usb: hub %d port %d: reset didn't finish (status %02x%02x)\n",
                cur->addr, p, st[1], st[0]);
            return -1;
        }
    } while (!(st[2] & (1u << 4)));             // C_PORT_RESET
    HUB_CLEAR(p, F_C_RESET);
    wait_ms(10);                                // reset recovery
    int status = st[0] | st[1] << 8;
    if (!(status & 2)) {
        say("usb: hub %d port %d: not enabled after reset (status %04x)\n", cur->addr, p, status);
        return -1;
    }
    int ls = (status >> 9) & 1;
    say("usb: hub %d port %d: device connected, %s speed\n", cur->addr, p,
        ls ? "low" : status & 0x400 ? "high" : "full");
    return enumerate(ls, hub, p, depth + 1);
}

static int depth_of(int i)                      // hubs above device i
{
    int n = 0;
    while (devs[i].parent >= 0)
        i = devs[i].parent, n++;
    return n;
}

// Forget device i and everything below it (it was unplugged).
static void forget(int i)
{
    for (int j = 0; j < ndev; j++)
        if (devs[j].addr && devs[j].parent == i)
            forget(j);
    struct hid *hs[2] = { &kbd, &mouse };
    for (int k = 0; k < 2; k++)
        if (i == hs[k]->dev) {
            W(hs[k]->ed + 0) |= 1u << 14;       // sKip: off the periodic list in effect
            hs[k]->up = 0, hs[k]->dev = -1;
        }
    if (i == msc_dev) {
        W(HC_CONTROL) = W(HC_CONTROL) & ~CTL_BLE;
        msc_dev = -1, msc_ready = 0;
    }
    say("usb: device %d (%04x:%04x) gone\n", devs[i].addr, devs[i].vid, devs[i].pid);
    devs[i].addr = 0;
}

static int hid_start(struct hid *h);

static int hub_init(int hub, int depth)
{
    uint8_t d[16];
    cur = &devs[hub];
    if (control(0xa0, 6, 0x2900, 0, sizeof d, d) < 7) {    // hub descriptor
        say("usb: hub %d: no hub descriptor\n", cur->addr);
        return -1;
    }
    int n = d[2], good = d[5] * 2;
    cur->nports = n;
    say("usb: hub %d: %d ports, characteristics %02x%02x, power good after %d ms\n",
        cur->addr, n, d[4], d[3], good);
    for (int p = 1; p <= n; p++)
        HUB_SET(p, F_PORT_POWER);
    wait_ms(good > 100 ? good : 100);
    // A device that connects later (some sticks take longer than this) shows
    // up as a connect change and is enumerated by usb_port_changed.
    for (int p = 1; p <= n; p++) {
        uint8_t st[4];
        cur = &devs[hub];
        if (HUB_PORT_STATUS(p, st) < 4)
            continue;
        if (!(st[0] & 1))
            continue;                           // nothing on this port (yet)
        if (depth >= MAX_HUB_DEPTH)
            say("usb: hub %d port %d: hubs nested too deep, skipped\n", devs[hub].addr, p);
        else
            hub_port_reset(hub, p, depth);      // also clears its connect change
    }
    return 0;
}

// ---------------- enumeration ----------------

// The device just reset on (parent, port) answers at address 0: give it the
// next address, read its descriptors, set its configuration; a hub's ports
// in turn. Returns its index, or -1.
static int enumerate(int lowspeed, int parent, int port, int depth)
{
    int i = 0;
    while (i < ndev && devs[i].addr)
        i++;
    if (i >= MAX_DEV || next_addr > 127) {
        say("usb: too many devices\n");
        return -1;
    }
    struct dev *dv = &devs[i];
    memset(dv, 0, sizeof *dv);
    dv->lowspeed = lowspeed, dv->parent = parent, dv->port = port, dv->mps0 = 8;
    cur = dv;

    uint8_t d[256];
    if (get_descriptor(1, 8, d) < 8)
        return -1;
    dv->mps0 = d[7];
    if (control(0x00, 5, next_addr, 0, 0, NULL) < 0)       // SET_ADDRESS
        return -1;
    dv->addr = next_addr++;
    if (i == ndev)
        ndev++;
    wait_ms(5);
    if (get_descriptor(1, 18, d) < 18)
        return -1;
    dv->vid = d[8] | d[9] << 8, dv->pid = d[10] | d[11] << 8;
    say("usb: device %d: %04x:%04x, USB %x.%02x, class %02x, ep0 max packet %d, %d configuration(s)\n",
           dv->addr, dv->vid, dv->pid, d[3], d[2], d[4], dv->mps0, d[17]);
    if (get_descriptor(2, 9, d) < 9)
        return -1;
    int total = d[2] | d[3] << 8;
    if (total > (int)sizeof d)
        total = sizeof d;
    int n = get_descriptor(2, total, d);
    if (n < 9)
        return -1;
    int config = d[5];
    say("usb: configuration %d, %d bytes, %d interface(s)\n", config, n, d[4]);
    parse_config(dv, d, n);
    if (control(0x00, 9, config, 0, 0, NULL) < 0)          // SET_CONFIGURATION
        return -1;
    if (dv->cls == 8 && msc_dev < 0 && dv->bulk_in && dv->bulk_out)
        msc_dev = i, msc_ready = 0;
    if (dv->cls == 9)
        hub_init(i, depth);
    else if (dv->cls == 8)
        say("usb: device %d: mass storage (bulk-only), endpoints %d IN, %d OUT, %d bytes\n",
            dv->addr, dv->bulk_in, dv->bulk_out, dv->bulk_mps);
    return i;
}

// The periodic list: every HCCA slot -> keyboard ED -> mouse ED, both
// sKipped until a device is polled on them.
static void periodic_start(void)
{
    W(ED_INTR + 0) = 1u << 14, W(ED_INTR + 4) = 0, W(ED_INTR + 8) = 0, W(ED_INTR + 12) = ED_MOUSE;
    W(ED_MOUSE + 0) = 1u << 14, W(ED_MOUSE + 4) = 0, W(ED_MOUSE + 8) = 0, W(ED_MOUSE + 12) = 0;
    for (int i = 0; i < 32; i++)
        W(HCCA + i * 4) = ED_INTR;
    __asm__ volatile("dsb sy" ::: "memory");
    W(HC_CONTROL) = W(HC_CONTROL) | CTL_PLE;
    kbd.up = mouse.up = 0;
    kbd.dev = mouse.dev = -1;
}

// Boot protocol for a found keyboard or mouse, then poll its endpoint.
static int hid_start(struct hid *h)
{
    cur = &devs[h->dev];
    if (control(0x21, 0x0b, 0, h->iface, 0, NULL) < 0)     // SET_PROTOCOL: boot
        return -1;
    control(0x21, 0x0a, 0, h->iface, 0, NULL);              // SET_IDLE 0: reports on change (may STALL)

    W(h->ed + 0) |= 1u << 14;                               // sKip while editing
    wait_ms(1);
    h->td = h->td_a, h->dummy = h->td_b;
    td_fill(h->dummy, 0, 0, 0, 0);
    td_fill(h->td, TD_IN | TD_R | TD_NOINT, h->buf, h->mps > 16 ? 16 : h->mps, h->dummy);
    W(h->ed + 4) = h->dummy;
    W(h->ed + 8) = h->td;
    __asm__ volatile("dsb sy" ::: "memory");
    W(h->ed + 0) = ed_d0(cur, h->ep, h->mps) | (2u << 11);  // direction IN, sKip off
    h->n = 0, h->err = 0, h->up = 1;
    say("usb: %s ready (device %d, interface %d, endpoint %d IN, %d bytes, every %d ms)\n",
           h->what, cur->addr, h->iface, h->ep, h->mps, h->interval);
    return 0;
}

int usb_init(void)
{
    kbd.up = mouse.up = 0;
    ndev = 0;
    next_addr = 1;
    msc_dev = -1, msc_ready = 0;
    hub_poll_dev = hub_poll_port = 0;
    hc_start();
    periodic_start();
    int ls = port_reset();
    if (ls < 0)
        return -1;
    if (enumerate(ls, -1, 1, 0) < 0)
        return -1;
    if (mouse.dev >= 0)
        hid_start(&mouse);
    if (kbd.dev < 0) {
        say("usb: no boot-protocol keyboard\n");
        return mouse.up ? 0 : -1;
    }
    return hid_start(&kbd) && !mouse.up ? -1 : 0;
}

// 1 when something was plugged into or out of the USB-A port (its
// connect-change bit, cleared on read; *connected = whether a device is there
// now): the caller enumerates again. Each call also checks one hub port for
// C_PORT_CONNECTION and handles it here (returns 0): what was on the port is
// forgotten, a new device enumerated (quietly, then one line about it).
int usb_port_changed(int *connected)
{
    uint32_t ps = W(HC_PORT1);
    *connected = ps & 1;
    if (ps & (1u << 16)) {
        W(HC_PORT1) = 1u << 16;
        if (!*connected)
            kbd.up = mouse.up = 0, kbd.dev = mouse.dev = -1, ndev = 0, msc_dev = -1, msc_ready = 0;
        return 1;
    }
    if (!*connected)
        return 0;
    for (int tries = 0; tries < MAX_DEV; tries++) {     // the next hub port
        if (hub_poll_dev >= ndev)
            hub_poll_dev = 0, hub_poll_port = 0;
        int hub = hub_poll_dev;
        struct dev *h = &devs[hub];
        if (!h->addr || h->cls != 9 || ++hub_poll_port > h->nports) {
            hub_poll_dev++, hub_poll_port = 0;
            continue;
        }
        int p = hub_poll_port;
        uint8_t st[4];
        int v = usb_verbose;
        usb_verbose = 0;                        // a hub that was just unplugged: no noise
        cur = h;
        int r = HUB_PORT_STATUS(p, st);
        usb_verbose = v;
        if (r < 4 || !(st[2] & 1))
            return 0;
        cur = h;
        HUB_CLEAR(p, F_C_CONNECTION);
        for (int j = 0; j < ndev; j++)          // what was on that port is gone either way
            if (devs[j].addr && devs[j].parent == hub && devs[j].port == p)
                forget(j);
        if (st[0] & 1) {
            if (depth_of(hub) + 1 > MAX_HUB_DEPTH) {
                say("usb: hub %d port %d: hubs nested too deep, skipped\n", h->addr, p);
                return 0;
            }
            usb_verbose = 0;
            int j = hub_port_reset(hub, p, depth_of(hub));
            if (j >= 0 && !kbd.up && kbd.dev >= 0)
                hid_start(&kbd);
            if (j >= 0 && !mouse.up && mouse.dev >= 0)
                hid_start(&mouse);
            usb_verbose = v;
            if (j < 0)
                say("usb: hub %d port %d: device plugged in, enumeration failed\n", h->addr, p);
            else
                say("usb: hub %d port %d: device %d (%04x:%04x)%s\n", h->addr, p, devs[j].addr,
                    devs[j].vid, devs[j].pid, devs[j].cls == 9 ? ", hub" : devs[j].cls == 3 ?
                    ", keyboard" : devs[j].cls == 2 ? ", mouse" : devs[j].cls == 8 ? ", mass storage" : "");
        }
        return 0;
    }
    return 0;
}

int usb_kbd_up(void)
{
    return kbd.up;
}

// One finished report of h copied to r (8 bytes): 1; nothing yet: 0; error: -1.
static int hid_poll(struct hid *h, uint8_t *r)
{
    if (!h->up)
        return -1;
    uint32_t head = W(h->ed + 8);
    if ((head & ~0xfu) != h->dummy && !(head & 1))
        return 0;                               // still waiting
    uint32_t cc = TD_CC(W(h->td));
    uint32_t buf = h->buf + (h->td == h->td_a ? 0 : 16);
    int ok = cc == 0 || cc == 9;
    if (ok)
        memcpy(r, (void *)(uintptr_t)buf, 8);
    if (head & 1) {                             // halted: report (once), clear, retry
        if (!h->err)
            say("usb: %s interrupt endpoint: %s\n", h->what, cc_names[cc]);
        W(h->ed + 8) = h->dummy & ~1u;
    }
    h->err = !ok;
    intr_queue(h);
    if (ok)
        h->n++;
    return ok ? 1 : -1;
}

int usb_kbd_poll(uint8_t *r)
{
    return hid_poll(&kbd, r);
}

int usb_mouse_up(void)
{
    return mouse.up;
}

// A boot mouse report: buttons (bit 0 left, 1 right, 2 middle) and the
// movement since the last one (x right, y down). 1 = new report.
int usb_mouse_poll(int *buttons, int *dx, int *dy)
{
    uint8_t r[8];
    int n = hid_poll(&mouse, r);
    if (n != 1)
        return n;
    *buttons = r[0] & 7, *dx = (int8_t)r[1], *dy = (int8_t)r[2];
    return 1;
}

void usb_stop(void)
{
    kbd.up = mouse.up = 0;
    W(HC_CONTROL) = 0;                          // UsbReset: no more DMA
}

void usb_status(void)
{
    printf("OHCI1 control %08x, frame %04x, HCCA %08x, port %08x%s\n", W(HC_CONTROL),
           W(HC_FMNUMBER) & 0xffff, W(HC_HCCA), W(HC_PORT1),
           W(HC_PORT1) & 1 ? (W(HC_PORT1) & 0x200 ? " (connected, low speed)" : " (connected, full speed)")
                           : " (nothing connected)");
    for (int i = 0; i < ndev; i++) {
        const struct dev *d = &devs[i];
        if (!d->addr)
            continue;
        printf("  device %d: %04x:%04x, %s speed, ", d->addr, d->vid, d->pid, d->lowspeed ? "low" : "full");
        if (d->parent < 0)
            printf("on the USB-A port");
        else
            printf("on hub %d port %d", devs[d->parent].addr, d->port);
        printf("%s\n", d->cls == 9 ? ", hub" : d->cls == 3 ? ", keyboard" : d->cls == 2 ? ", mouse" :
                        d->cls == 8 ? ", mass storage" : "");
    }
    if (kbd.up)
        printf("keyboard (device %d) up, %d reports\n", devs[kbd.dev].addr, kbd.n);
    if (mouse.up)
        printf("mouse (device %d) up, %d reports\n", devs[mouse.dev].addr, mouse.n);
    if (msc_ready && msc_dev >= 0)
        printf("mass storage (device %d) ready, %u blocks of %u bytes\n", devs[msc_dev].addr,
               msc_blocks, msc_bsize);
}

// ---------------- mass storage (bulk-only transport, SCSI) ----------------

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

// ED for a bulk endpoint, empty (HeadP = TailP = its dummy), toggle DATA0.
static void bulk_ed(uint32_t ed, uint32_t dummy, int ep, int in)
{
    const struct dev *d = &devs[msc_dev];
    td_fill(dummy, 0, 0, 0, 0);
    W(ed + 0) = ed_d0(d, ep, d->bulk_mps) | (in ? 2u : 1u) << 11;
    W(ed + 4) = dummy;
    W(ed + 8) = dummy;
}

// One bulk transfer of len bytes (up to BULK_MAX) from/to the DMA buffer
// buf. TDs of at most 4 KB, each within one page; a short IN packet ends
// the transfer. Returns the bytes moved, or -1 - CC (-5 = STALL).
#define BULK_POOL       32                      // TDs per direction
static int bulk(int in, uint32_t buf, uint32_t len, uint32_t timeout_ms)
{
    uint32_t ed = in ? ED_BULK_IN : ED_BULK_OUT;
    uint32_t pool = BULK_TD + (in ? 0 : BULK_POOL * 16);
    uint32_t first = W(ed + 4);                         // the idle ED's tail: our first TD
    uint32_t td[18], start[17], size[17], n = 0, k = 0;
    uint32_t a = buf, left = len;
    if (len > BULK_MAX)
        return -1;
    td[0] = first;
    do {                                                // TD n covers one page's part
        uint32_t chunk = 0x1000 - (a & 0xfff);
        if (chunk > left)
            chunk = left;
        uint32_t next;
        do
            next = pool + 16 * (k++ % BULK_POOL);
        while (next == first);
        td[n + 1] = next;
        td_fill(next, 0, 0, 0, 0);                      // the next TD, or the new tail
        int last = chunk == left;
        td_fill(td[n], (in ? TD_IN : TD_OUT) | TD_NOINT | (last && in ? TD_R : 0), a, chunk, next);
        start[n] = a, size[n] = chunk;
        n++;
        a += chunk, left -= chunk;
    } while (left);
    uint32_t tail = td[n];
    __asm__ volatile("dsb sy" ::: "memory");
    W(ed + 4) = tail;                                   // the controller may start now
    W(HC_CMDSTATUS) = 1u << 2;                          // BLF: bulk list filled

    uint64_t end = now() + (uint64_t)timeout_ms * TICKS_PER_MS;
    while ((W(ed + 8) & ~0xfu) != tail && !(W(ed + 8) & 1))
        if (now() > end) {
            say("usb: bulk %s %u bytes: timeout\n", in ? "IN" : "OUT", len);
            W(ed + 0) |= 1u << 14;                      // sKip while we drop the TDs
            wait_ms(1);
            W(ed + 8) = tail | (W(ed + 8) & 2);
            W(ed + 0) &= ~(1u << 14);
            return -1;
        }
    int moved = 0, cc = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t c = TD_CC(W(td[i]));
        if (c == 0xf || c == 0xe)
            break;                                      // not reached
        uint32_t cbp = W(td[i] + 4);
        moved += cbp ? cbp - start[i] : size[i];
        if (c != 0 && !(c == 9 && in)) {                // underrun (short IN) is fine
            cc = c;
            break;
        }
        if (cbp)
            break;                                      // short packet: done
    }
    if (W(ed + 8) & 1)                                  // halted: drop the rest, keep the toggle
        W(ed + 8) = tail | (W(ed + 8) & 2);
    if (cc) {
        say("usb: bulk %s: %s\n", in ? "IN" : "OUT", cc_names[cc]);
        return -1 - (int)cc;
    }
    return moved;
}

static int clear_halt(int ep_addr)
{
    cur = &devs[msc_dev];
    int r = control(0x02, 1, 0, ep_addr, 0, NULL);      // CLEAR_FEATURE ENDPOINT_HALT
    uint32_t ed = ep_addr & 0x80 ? ED_BULK_IN : ED_BULK_OUT;
    W(ed + 8) = W(ed + 8) & ~3u;                        // toggle back to DATA0
    return r;
}

// Bulk-only mass storage reset and both endpoints' halts cleared.
static void msc_reset(void)
{
    const struct dev *d = &devs[msc_dev];
    cur = &devs[msc_dev];
    control(0x21, 0xff, 0, d->msc_iface, 0, NULL);
    clear_halt(0x80 | d->bulk_in);
    clear_halt(d->bulk_out);
}

// One SCSI command: CBW, data (in or out, up to BULK_MAX at BULK_BUF), CSW.
// Returns 0 = good, 1 = command failed (CSW status 1: REQUEST SENSE), -1 = transport error.
static int scsi(const uint8_t *cdb, int cdb_len, int in, uint32_t len)
{
    const struct dev *d = &devs[msc_dev];
    uint8_t *cbw = (uint8_t *)CBW_BUF;
    memset(cbw, 0, 31);
    uint32_t tag = ++msc_tag;
    cbw[0] = 'U', cbw[1] = 'S', cbw[2] = 'B', cbw[3] = 'C';
    memcpy(cbw + 4, &tag, 4);
    memcpy(cbw + 8, &len, 4);
    cbw[12] = in ? 0x80 : 0;
    cbw[13] = 0;                                        // LUN 0
    cbw[14] = cdb_len;
    memcpy(cbw + 15, cdb, cdb_len);
    if (bulk(0, CBW_BUF, 31, 1000) != 31) {
        msc_reset();
        return -1;
    }
    if (len) {
        int r = bulk(in, BULK_BUF, len, 5000);
        if (r == -5)                                    // STALL: clear it, the CSW follows
            clear_halt(in ? 0x80 | d->bulk_in : d->bulk_out);
        else if (r < 0) {
            msc_reset();
            return -1;
        }
    }
    int r = bulk(1, CSW_BUF, 13, 2000);
    if (r == -5) {
        clear_halt(0x80 | d->bulk_in);
        r = bulk(1, CSW_BUF, 13, 2000);
    }
    const uint8_t *csw = (const uint8_t *)CSW_BUF;
    uint32_t t;
    memcpy(&t, csw + 4, 4);
    if (r != 13 || csw[0] != 'U' || csw[1] != 'S' || csw[2] != 'B' || csw[3] != 'S' || t != tag) {
        say("usb: mass storage: bad status block (%d bytes)\n", r);
        msc_reset();
        return -1;
    }
    if (csw[12] == 2) {                                 // phase error
        msc_reset();
        return -1;
    }
    return csw[12] ? 1 : 0;
}

static void request_sense(uint8_t *key, uint8_t *asc)
{
    static const uint8_t cdb[6] = { 0x03, 0, 0, 0, 18, 0 };
    *key = *asc = 0xff;
    if (scsi(cdb, 6, 1, 18) == 0) {
        const uint8_t *b = (const uint8_t *)BULK_BUF;
        *key = b[2] & 0xf, *asc = b[12];
    }
}

// Set up the first mass storage device: bulk endpoints, INQUIRY, wait until
// it is ready, READ CAPACITY. 0 = ready.
int usb_msc_init(void)
{
    msc_ready = 0;
    if (msc_dev < 0) {
        say("usb: no mass storage device\n");
        return -1;
    }
    const struct dev *d = &devs[msc_dev];
    W(HC_CONTROL) = W(HC_CONTROL) & ~CTL_BLE;           // list pointers only with BLE off
    wait_ms(1);
    bulk_ed(ED_BULK_OUT, BULK_TD + BULK_POOL * 16, d->bulk_out, 0);
    bulk_ed(ED_BULK_IN, BULK_TD, d->bulk_in, 1);
    W(ED_BULK_OUT + 12) = ED_BULK_IN;
    W(ED_BULK_IN + 12) = 0;
    __asm__ volatile("dsb sy" ::: "memory");
    W(HC_BULKHEAD) = ED_BULK_OUT;
    W(HC_BULKCUR) = 0;
    W(HC_CONTROL) = W(HC_CONTROL) | CTL_BLE;

    static const uint8_t inquiry[6] = { 0x12, 0, 0, 0, 36, 0 };
    if (scsi(inquiry, 6, 1, 36) != 0) {
        say("usb: mass storage: INQUIRY failed\n");
        return -1;
    }
    const char *b = (const char *)BULK_BUF;
    say("usb: mass storage device %d: %.8s %.16s %.4s, type %02x%s\n", d->addr, b + 8, b + 16,
        b + 32, b[0] & 0x1f, b[1] & 0x80 ? ", removable" : "");
    static const uint8_t tur[6] = { 0 };
    int ok = 0;
    for (int i = 0; i < 30 && !ok; i++) {               // up to ~3 s: a stick may spin up
        int r = scsi(tur, 6, 0, 0);
        if (r == 0)
            ok = 1;
        else {
            uint8_t key, asc;
            if (r > 0)
                request_sense(&key, &asc);
            wait_ms(100);
        }
    }
    if (!ok) {
        say("usb: mass storage: not ready\n");
        return -1;
    }
    static const uint8_t cap[10] = { 0x25 };
    if (scsi(cap, 10, 1, 8) != 0) {
        say("usb: mass storage: READ CAPACITY failed\n");
        return -1;
    }
    const uint8_t *c = (const uint8_t *)BULK_BUF;
    msc_blocks = be32(c) + 1, msc_bsize = be32(c + 4);
    say("usb: mass storage: %u blocks of %u bytes (%u MB)\n", msc_blocks, msc_bsize,
        (uint32_t)((uint64_t)msc_blocks * msc_bsize >> 20));
    if (msc_bsize != 512) {
        say("usb: mass storage: only 512-byte blocks are supported\n");
        return -1;
    }
    msc_ready = 1;
    return 0;
}

int usb_msc_info(uint32_t *blocks, uint32_t *bsize)
{
    if (!msc_ready || msc_dev < 0)
        return -1;
    *blocks = msc_blocks, *bsize = msc_bsize;
    return 0;
}

static int rw10(int write, uint32_t lba, uint32_t n)
{
    uint8_t cdb[10] = { write ? 0x2a : 0x28, 0, lba >> 24, lba >> 16, lba >> 8, lba, 0, n >> 8, n, 0 };
    int r = scsi(cdb, 10, !write, n * 512);
    if (r > 0) {
        uint8_t key, asc;
        request_sense(&key, &asc);
        say("usb: mass storage: %s at %u failed, sense key %x, ASC %02x\n",
            write ? "WRITE(10)" : "READ(10)", lba, key, asc);
    }
    return r;
}

// n blocks of 512 bytes from lba into buf (any RAM). 0 = ok.
int usb_msc_read(uint32_t lba, uint32_t n, void *buf)
{
    if (!msc_ready || msc_dev < 0 || lba + n > msc_blocks || lba + n < lba)
        return -1;
    uint8_t *p = buf;
    while (n) {
        uint32_t k = n > BULK_MAX / 512 ? BULK_MAX / 512 : n;
        if (rw10(0, lba, k))
            return -1;
        memcpy(p, (const void *)BULK_BUF, k * 512);
        p += k * 512, lba += k, n -= k;
    }
    return 0;
}

// n blocks of 512 bytes from buf to lba. 0 = ok.
int usb_msc_write(uint32_t lba, uint32_t n, const void *buf)
{
    if (!msc_ready || msc_dev < 0 || lba + n > msc_blocks || lba + n < lba)
        return -1;
    const uint8_t *p = buf;
    while (n) {
        uint32_t k = n > BULK_MAX / 512 ? BULK_MAX / 512 : n;
        memcpy((void *)BULK_BUF, p, k * 512);
        if (rw10(1, lba, k))
            return -1;
        p += k * 512, lba += k, n -= k;
    }
    return 0;
}

// ---------------- HID boot keyboard -> characters ----------------

static const char hid_plain[] =
    "\0\0\0\0abcdefghijklmnopqrstuvwxyz1234567890\r\x1b\x7f\t -=[]\\\0;'`,./";
static const char hid_shift[] =
    "\0\0\0\0ABCDEFGHIJKLMNOPQRSTUVWXYZ!@#$%^&*()\r\x1b\x7f\t _+{}|\0:\"~<>?";

// Usage code -> character (US layout), 0 if none.
int usb_hid_char(uint8_t usage, uint8_t mods)
{
    if (usage >= sizeof hid_plain - 1)
        return 0;
    int c = (mods & 0x22) ? hid_shift[usage] : hid_plain[usage];
    if ((mods & 0x11) && c >= 'a' && c <= 'z')  // Ctrl
        c &= 0x1f;
    return c;
}
