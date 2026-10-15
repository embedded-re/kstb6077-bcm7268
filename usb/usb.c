// usb.c - OHCI1 (the USB-A port's low/full-speed controller) and a HID
// boot-protocol keyboard, polled (docs/hardware/usb.md).
//
// OHCI 1.0a: the controller walks lists of endpoint descriptors (ED) and
// transfer descriptors (TD) in RAM by DMA. They live in a non-cacheable 2 MB
// block (DMA_BASE, start.S), so no cache maintenance is needed.
//
//   usb_init()  controller reset, port power and reset, then enumeration:
//               GET_DESCRIPTOR, SET_ADDRESS, configuration, boot protocol
//   usb_kbd_poll()  one 8-byte report from the interrupt endpoint, if any
//
// Shared by mon/ and doom/ (like libc/); each builds it with its own kstb.h.

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "kstb.h"                       // the program's: DMA_BASE
#include "usb.h"

int usb_verbose = 1;                    // 0: print nothing (kmon starts it quietly)
#define say(...) do { if (usb_verbose) printf(__VA_ARGS__); } while (0)

#define OHCI            0xf0b00600
#define HC_CONTROL      (OHCI + 0x04)
#define HC_CMDSTATUS    (OHCI + 0x08)
#define HC_INTDISABLE   (OHCI + 0x14)
#define HC_HCCA         (OHCI + 0x18)
#define HC_CTRLHEAD     (OHCI + 0x20)
#define HC_CTRLCUR      (OHCI + 0x24)
#define HC_FMINTERVAL   (OHCI + 0x34)
#define HC_FMNUMBER     (OHCI + 0x3c)
#define HC_PERIODSTART  (OHCI + 0x40)
#define HC_RHSTATUS     (OHCI + 0x50)
#define HC_PORT1        (OHCI + 0x54)

#define CTL_PLE         (1u << 2)       // periodic (interrupt) list on
#define CTL_CLE         (1u << 4)       // control list on
#define CTL_OPER        (2u << 6)       // HCFS = Operational

// DMA area layout (all uncached)
#define HCCA            (DMA_BASE + 0x000)      // 256 bytes, 256-aligned
#define ED_CTRL         (DMA_BASE + 0x100)
#define ED_INTR         (DMA_BASE + 0x110)
#define TD_POOL         (DMA_BASE + 0x200)      // 16 TDs of 16 bytes
#define SETUP_BUF       (DMA_BASE + 0x400)
#define DATA_BUF        (DMA_BASE + 0x800)      // 256 bytes
#define REPORT_BUF      (DMA_BASE + 0xc00)      // 2 x 16 bytes

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

static int up;                          // enumerated and polling
static int lowspeed, mps0;
static int kbd_ep, kbd_mps, kbd_iface, kbd_interval;
static uint32_t intr_td, intr_dummy;    // the interrupt endpoint's TD and tail
static int report_n;
static uint16_t vid, pid;

static void td_fill(uint32_t td, uint32_t d0, uint32_t buf, uint32_t len, uint32_t next)
{
    W(td + 0) = d0 | (0xfu << 28);      // CC = not accessed
    W(td + 4) = len ? buf : 0;
    W(td + 8) = next;
    W(td + 12) = len ? buf + len - 1 : 0;
}

// ED dword 0: function address, endpoint, speed, max packet size
static uint32_t ed_d0(int addr, int ep, int mps)
{
    return addr | (ep << 7) | (lowspeed ? 1u << 13 : 0) | ((uint32_t)mps << 16);
}

// ---------------- control transfers ----------------

// One control transfer on the control list. Returns the number of bytes
// received/sent in the data stage, or -1 (and prints why).
static int control(int addr, uint8_t rtype, uint8_t req, uint16_t value, uint16_t index,
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

    W(ED_CTRL + 0) = ed_d0(addr, 0, mps0) | (1u << 14);    // sKip while editing
    W(ED_CTRL + 4) = dummy;                                 // TailP
    W(ED_CTRL + 8) = td0;                                   // HeadP, toggle carry 0
    W(ED_CTRL + 12) = 0;
    W(ED_CTRL + 0) = ed_d0(addr, 0, mps0);
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

static int get_descriptor(int addr, int type, int len, void *buf)
{
    return control(addr, 0x80, 6, type << 8, 0, len, buf);
}

// ---------------- bring-up ----------------

static void hc_start(void)
{
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

// Port reset; 0 when a device is there and the port is enabled.
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
    lowspeed = (ps >> 9) & 1;
    say("usb: port %08x: device connected, %s speed, enabled %s\n", ps,
           lowspeed ? "low" : "full", ps & 2 ? "yes" : "no");
    return ps & 2 ? 0 : -1;
}

// Find the boot keyboard interface and its interrupt IN endpoint.
static int parse_config(const uint8_t *c, int n)
{
    int iface = -1;
    kbd_ep = 0;
    for (int i = 0; i + 1 < n && c[i]; i += c[i]) {
        if (c[i + 1] == 4) {                    // interface
            say("usb:   interface %d: class %02x subclass %02x protocol %02x, %d endpoints\n",
                   c[i + 2], c[i + 5], c[i + 6], c[i + 7], c[i + 4]);
            iface = (c[i + 5] == 3 && c[i + 6] == 1 && c[i + 7] == 1) ? c[i + 2] : -1;
            if (iface >= 0 && !kbd_ep)
                kbd_iface = iface;
        } else if (c[i + 1] == 5) {             // endpoint
            say("usb:     endpoint %02x: type %d, max packet %d, interval %d ms\n",
                   c[i + 2], c[i + 3] & 3, c[i + 4] | c[i + 5] << 8, c[i + 6]);
            if (iface >= 0 && !kbd_ep && (c[i + 2] & 0x80) && (c[i + 3] & 3) == 3) {
                kbd_ep = c[i + 2] & 0xf;
                kbd_mps = c[i + 4];
                kbd_interval = c[i + 6];
            }
        }
    }
    return kbd_ep ? 0 : -1;
}

static void intr_queue(void)
{
    // The TD the controller just finished becomes the new tail; the old tail
    // gets the next IN request (the standard OHCI tail swap).
    uint32_t td = intr_dummy, dummy = intr_td;
    uint32_t buf = REPORT_BUF + (td == TD_POOL + 64 ? 0 : 16);
    td_fill(dummy, 0, 0, 0, 0);
    td_fill(td, TD_IN | TD_R | TD_NOINT, buf, kbd_mps > 16 ? 16 : kbd_mps, dummy);
    // toggle: from the ED's carry (T = 0x)
    __asm__ volatile("dsb sy" ::: "memory");
    intr_td = td, intr_dummy = dummy;
    W(ED_INTR + 4) = dummy;
}

int usb_init(void)
{
    up = 0;
    hc_start();
    if (port_reset())
        return -1;

    uint8_t d[256];
    mps0 = 8;
    if (get_descriptor(0, 1, 8, d) < 8)
        return -1;
    mps0 = d[7];
    if (control(0, 0x00, 5, 1, 0, 0, NULL) < 0) {           // SET_ADDRESS 1
        return -1;
    }
    wait_ms(5);
    if (get_descriptor(1, 1, 18, d) < 18)
        return -1;
    vid = d[8] | d[9] << 8, pid = d[10] | d[11] << 8;
    say("usb: device %04x:%04x, USB %x.%02x, class %02x, ep0 max packet %d, %d configuration(s)\n",
           vid, pid, d[3], d[2], d[4], mps0, d[17]);
    if (get_descriptor(1, 2, 9, d) < 9)
        return -1;
    int total = d[2] | d[3] << 8;
    if (total > (int)sizeof d)
        total = sizeof d;
    int n = get_descriptor(1, 2, total, d);
    if (n < 9)
        return -1;
    int config = d[5];
    say("usb: configuration %d, %d bytes, %d interface(s)\n", config, n, d[4]);
    if (parse_config(d, n)) {
        say("usb: no boot-protocol keyboard interface\n");
        return -1;
    }
    if (control(1, 0x00, 9, config, 0, 0, NULL) < 0)        // SET_CONFIGURATION
        return -1;
    if (control(1, 0x21, 0x0b, 0, kbd_iface, 0, NULL) < 0)  // SET_PROTOCOL: boot
        return -1;
    control(1, 0x21, 0x0a, 0, kbd_iface, 0, NULL);          // SET_IDLE 0: reports on change (may STALL)

    // Interrupt endpoint: one ED hung from all 32 HCCA slots (polled every frame)
    intr_td = TD_POOL + 64, intr_dummy = TD_POOL + 80;
    td_fill(intr_dummy, 0, 0, 0, 0);
    td_fill(intr_td, TD_IN | TD_R | TD_NOINT, REPORT_BUF, kbd_mps > 16 ? 16 : kbd_mps, intr_dummy);
    W(ED_INTR + 0) = ed_d0(1, kbd_ep, kbd_mps) | (2u << 11);  // direction IN
    W(ED_INTR + 4) = intr_dummy;
    W(ED_INTR + 8) = intr_td;
    W(ED_INTR + 12) = 0;
    for (int i = 0; i < 32; i++)
        W(HCCA + i * 4) = ED_INTR;
    __asm__ volatile("dsb sy" ::: "memory");
    W(HC_CONTROL) = W(HC_CONTROL) | CTL_PLE;
    report_n = 0;
    up = 1;
    say("usb: keyboard ready (interface %d, endpoint %d IN, %d bytes, every %d ms)\n",
           kbd_iface, kbd_ep, kbd_mps, kbd_interval);
    return 0;
}

// One finished report copied to r (8 bytes): 1; nothing yet: 0; error: -1.
// The port's connect-change bit (set by plugging or unplugging), cleared on
// read; *connected = whether a device is there now.
int usb_port_changed(int *connected)
{
    uint32_t ps = W(HC_PORT1);
    *connected = ps & 1;
    if (!(ps & (1u << 16)))
        return 0;
    W(HC_PORT1) = 1u << 16;
    if (!*connected)
        up = 0;
    return 1;
}

int usb_kbd_up(void)
{
    return up;
}

int usb_kbd_poll(uint8_t *r)
{
    if (!up)
        return -1;
    uint32_t head = W(ED_INTR + 8);
    if ((head & ~0xfu) != intr_dummy && !(head & 1))
        return 0;                               // still waiting
    uint32_t d0 = W(intr_td);
    uint32_t cc = TD_CC(d0);
    uint32_t buf = REPORT_BUF + (intr_td == TD_POOL + 64 ? 0 : 16);
    int ok = cc == 0 || cc == 9;
    if (ok)
        memcpy(r, (void *)(uintptr_t)buf, 8);
    if (head & 1) {                             // halted: report, clear, retry
        say("usb: interrupt endpoint: %s\n", cc_names[cc]);
        W(ED_INTR + 8) = intr_dummy & ~1u;
    }
    intr_queue();
    if (ok)
        report_n++;
    return ok ? 1 : -1;
}

void usb_stop(void)
{
    up = 0;
    W(HC_CONTROL) = 0;                          // UsbReset: no more DMA
}

void usb_status(void)
{
    printf("OHCI1 control %08x, frame %04x, HCCA %08x, port %08x%s\n", W(HC_CONTROL),
           W(HC_FMNUMBER) & 0xffff, W(HC_HCCA), W(HC_PORT1),
           W(HC_PORT1) & 1 ? (W(HC_PORT1) & 0x200 ? " (connected, low speed)" : " (connected, full speed)")
                           : " (nothing connected)");
    if (up)
        printf("keyboard %04x:%04x up, %d reports\n", vid, pid, report_n);
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
