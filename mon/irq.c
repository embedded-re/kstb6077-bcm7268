// irq.c - interrupts at EL2: GIC-400, the EL2 timer, UART0 receive, SW1 and
// the IR remote (docs/hardware/interrupts.md).
//
// The chain for every source: device -> (L2 controller) -> GIC distributor
// (enable, priority, target CPU) -> GIC CPU interface (PMR) -> CPU (HCR_EL2.IMO,
// DAIF.I) -> the IRQ vector in start.S -> irq_handler() here, which reads
// GICC_IAR, handles the source, clears it and writes GICC_EOIR.
//
// Each step is a separate subcommand, so a source can be enabled in the GIC
// and seen pending there before the CPU takes it.
//
// Built with -mgeneral-regs-only: the IRQ entry in start.S saves only the
// general registers, so the handler must not touch FP/SIMD.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "kstb.h"
#include "mon.h"

// GIC-400 (docs/hardware/interrupts.md)
#define GICD            0xffd01000
#define GICD_CTLR       (GICD + 0x000)
#define GICD_ISENABLER  (GICD + 0x100)
#define GICD_ICENABLER  (GICD + 0x180)
#define GICD_ISPENDR    (GICD + 0x200)
#define GICD_ICPENDR    (GICD + 0x280)
#define GICD_ISACTIVER  (GICD + 0x300)
#define GICD_IPRIORITYR (GICD + 0x400)
#define GICD_ITARGETSR  (GICD + 0x800)
#define GICD_ICFGR      (GICD + 0xc00)
#define GICC            0xffd02000
#define GICC_CTLR       (GICC + 0x00)
#define GICC_PMR        (GICC + 0x04)
#define GICC_IAR        (GICC + 0x0c)
#define GICC_EOIR       (GICC + 0x10)
#define GICC_RPR        (GICC + 0x14)
#define GICC_HPPIR      (GICC + 0x18)

#define ID_TIMER        26              // PPI: EL2 physical timer (CNTHP)
#define ID_AON          98              // SPI 66: UPG main AON L2 0xf0419c00
#define ID_UART0        102             // SPI 70: UART0

#define AON_L2          0xf0419c00      // STATUS, MASK_STATUS, MASK_SET, MASK_CLEAR
#define L2_KBD1         (1u << 0)
#define L2_GIO          (1u << 3)
#define GPIO_DATA       0xf0419c84
#define GPIO_EI         0xf0419c90      // 1 = interrupt on both edges (0 after BOLT: one edge)
#define GPIO_MASK       0xf0419c94
#define GPIO_STAT       0xf0419c9c
#define SW1             (1u << 14)
#define IR_BASE         0xf0419900

#define read8(a)        (*(volatile uint8_t *)(uintptr_t)(a))
#define write8(a, v)    (*(volatile uint8_t *)(uintptr_t)(a) = (v))

#define TICKS_PER_MS    27000

static inline uint64_t cntpct(void)
{
    uint64_t t;
    __asm__ volatile("isb; mrs %0, cntpct_el0" : "=r"(t));
    return t;
}

// ---------------- state ----------------

static int timer_on, uart_on, sw1_on, ir_on, cpu_on;
static uint64_t timer_period;           // CNTHP ticks between timer interrupts
static uint64_t hcr_saved;              // HCR_EL2 before `irq on`, restored by irq_off
static int hcr_changed;

static volatile uint32_t count[256];    // interrupts taken per GIC ID
static volatile uint32_t spurious, unknown_id, aon_other, rx_dropped, wfi_count;
static volatile uint64_t ticks;         // timer interrupts

// UART0 receive ring, filled by the handler
#define RX_SIZE 256
static volatile uint8_t rx_buf[RX_SIZE];
static volatile uint32_t rx_head, rx_tail;

// Events for `irq watch` (not the timer or UART: too many)
#define EV_SW1      1
#define EV_IR       2
#define EV_UNKNOWN  3
#define EV_IR_REPEAT 4
#define EV_SIZE     32
static volatile struct { uint64_t t; uint32_t what, val; } ev[EV_SIZE];
static volatile uint32_t ev_head, ev_tail;

static void ev_add(uint32_t what, uint32_t val)
{
    uint32_t h = ev_head;
    if (h - ev_tail >= EV_SIZE)
        return;
    ev[h % EV_SIZE].t = cntpct();
    ev[h % EV_SIZE].what = what;
    ev[h % EV_SIZE].val = val;
    ev_head = h + 1;
}

// ---------------- GIC ----------------

static int gic_bit(uint32_t base, int id)
{
    return (read32(base + id / 32 * 4) >> (id % 32)) & 1;
}

// This CPU's target mask: on GIC-400, ITARGETSR0 bytes read back the mask of
// the CPU interface that reads them.
static uint8_t gic_this_cpu(void)
{
    return read8(GICD_ITARGETSR);
}

static void gic_enable(int id)
{
    write8(GICD_IPRIORITYR + id, 0xa0);
    if (id >= 32)
        write8(GICD_ITARGETSR + id, gic_this_cpu());
    write32(GICD_ISENABLER + id / 32 * 4, 1u << (id % 32));
}

static void gic_disable(int id)
{
    write32(GICD_ICENABLER + id / 32 * 4, 1u << (id % 32));
}

// ---------------- the handler ----------------

static void timer_irq(void)
{
    uint64_t cval, now = cntpct();
    __asm__ volatile("mrs %0, cnthp_cval_el2" : "=r"(cval));
    cval += timer_period;
    if (cval <= now)                    // fell behind (e.g. IRQs were masked): skip ahead
        cval = now + timer_period;
    __asm__ volatile("msr cnthp_cval_el2, %0; isb" :: "r"(cval));
    ticks++;
}

static void uart_irq(void)
{
    while (read32(UART0 + 0x14) & 1) {         // LSR bit 0: byte received
        uint8_t c = read32(UART0) & 0xff;
        if (rx_head - rx_tail < RX_SIZE) {
            rx_buf[rx_head % RX_SIZE] = c;
            rx_head++;
        } else {
            rx_dropped++;
        }
    }
}

static void aon_irq(void)
{
    uint32_t st = read32(AON_L2) & ~read32(AON_L2 + 4);
    if (st & L2_GIO) {
        uint32_t g = read32(GPIO_STAT) & read32(GPIO_MASK);
        write32(GPIO_STAT, g);                  // write 1 to clear: drops L2 bit 3
        if (g & SW1)
            ev_add(EV_SW1, (read32(GPIO_DATA) & SW1) ? 1 : 0);
    }
    if (st & L2_KBD1) {
        uint32_t s = read32(IR_BASE);
        if (s & 1) {
            uint32_t code = read32(IR_BASE + 0x10);
            write32(IR_BASE, s & ~1u);          // clears the code: drops L2 bit 0
            ev_add((s & 0x40) ? EV_IR : EV_IR_REPEAT, code);       // b6: new key (as in cmd_ir)
        }
    }
    if (st & ~(L2_GIO | L2_KBD1))
        aon_other++;
}

// Called from the IRQ vector (start.S) with IRQs masked.
void irq_handler(void)
{
    for (int n = 0; n < 16; n++) {
        uint32_t iar = read32(GICC_IAR);
        uint32_t id = iar & 0x3ff;
        if (id >= 1020) {
            if (n == 0)
                spurious++;
            return;
        }
        count[id]++;
        switch (id) {
        case ID_TIMER:  timer_irq(); break;
        case ID_UART0:  uart_irq(); break;
        case ID_AON:    aon_irq(); break;
        default:                                // not ours: switch it off, report it
            gic_disable(id);
            unknown_id = id;
            ev_add(EV_UNKNOWN, id);
            break;
        }
        write32(GICC_EOIR, iar);
    }
}

// ---------------- for the console (mon.c) ----------------

int irq_uart_on(void)
{
    return uart_on;
}

int irq_rx_ready(void)
{
    return rx_head != rx_tail;
}

int irq_rx_get(void)
{
    if (rx_head == rx_tail)
        return -1;
    int c = rx_buf[rx_tail % RX_SIZE];
    rx_tail++;
    return c;
}

// Sleep until an interrupt while the receive ring is empty. IRQs are masked
// around the check so a byte arriving in between still wakes the WFI.
//
// Safety net: if a byte waits in the UART for 100 ms without the handler
// taking it, the interrupt isn't arriving. Fall back to polling so the
// console stays alive.
static void uart_set(int on);
static uint64_t rx_stuck_since;

void irq_wait_rx(void)
{
    if (rx_head == rx_tail && (read32(UART0 + 0x14) & 1)) {
        uint64_t now = cntpct();
        if (!rx_stuck_since) {
            rx_stuck_since = now;
        } else if (now - rx_stuck_since > 100 * TICKS_PER_MS) {
            uart_set(0);
            rx_stuck_since = 0;
            printf("\n[irq: UART0 receive interrupt not arriving, back to polling]\n");
            return;
        }
    } else {
        rx_stuck_since = 0;
    }
    __asm__ volatile("msr daifset, #2" ::: "memory");
    if (rx_head == rx_tail) {
        wfi_count++;
        __asm__ volatile("dsb sy; wfi" ::: "memory");
    }
    __asm__ volatile("msr daifclr, #2; isb" ::: "memory");
}

int irq_ir_on(void)
{
    return ir_on;
}

// ---------------- switching sources ----------------

static void timer_set(uint32_t ms)
{
    __asm__ volatile("msr cnthp_ctl_el2, %0; isb" :: "r"(0ul));
    timer_on = 0;
    if (!ms) {
        gic_disable(ID_TIMER);
        return;
    }
    timer_period = (uint64_t)ms * TICKS_PER_MS;
    uint64_t cval = cntpct() + timer_period;
    __asm__ volatile("msr cnthp_cval_el2, %0" :: "r"(cval));
    __asm__ volatile("msr cnthp_ctl_el2, %0; isb" :: "r"(1ul));    // ENABLE, IMASK = 0
    gic_enable(ID_TIMER);
    timer_on = 1;
}

static void uart_set(int on)
{
    if (on) {
        gic_enable(ID_UART0);
        write32(UART0 + 0x04, read32(UART0 + 0x04) | 1);           // IER: ERBFI
    } else {
        write32(UART0 + 0x04, read32(UART0 + 0x04) & ~1u);
        gic_disable(ID_UART0);
    }
    uart_on = on;
}

static void aon_update(void)
{
    if (sw1_on || ir_on)
        gic_enable(ID_AON);
    else
        gic_disable(ID_AON);
}

static void sw1_set(int on)
{
    if (on) {
        write32(GPIO_EI, read32(GPIO_EI) | SW1);    // press and release, as Linux sets it
        write32(GPIO_STAT, SW1);                // forget old edges
        write32(GPIO_MASK, read32(GPIO_MASK) | SW1);
        write32(AON_L2 + 0xc, L2_GIO);          // MASK_CLEAR
    } else {
        write32(AON_L2 + 0x8, L2_GIO);          // MASK_SET
        write32(GPIO_MASK, read32(GPIO_MASK) & ~SW1);
        write32(GPIO_EI, read32(GPIO_EI) & ~SW1);
        write32(GPIO_STAT, SW1);
    }
    sw1_on = on;
    aon_update();
}

static void ir_set(int on)
{
    if (on) {
        ir_setup();                             // NEC on kbd1 (mon.c)
        write32(IR_BASE, 0);
        write32(AON_L2 + 0xc, L2_KBD1);
    } else {
        write32(AON_L2 + 0x8, L2_KBD1);
    }
    ir_on = on;
    aon_update();
}

static void cpu_set(int on, int imo)
{
    if (on) {
        write32(GICC_PMR, 0xff);                // let every priority through
        write32(GICC_CTLR, (read32(GICC_CTLR) | 1) & ~(1u << 9));  // group 1 on, EOI = drop + deactivate
        if (imo) {
            uint64_t hcr;
            __asm__ volatile("mrs %0, hcr_el2" : "=r"(hcr));
            if (!hcr_changed)
                hcr_saved = hcr, hcr_changed = 1;
            __asm__ volatile("msr hcr_el2, %0; isb" :: "r"(hcr | (1ul << 4)));     // IMO
        }
        __asm__ volatile("msr daifclr, #2; isb" ::: "memory");
    } else {
        __asm__ volatile("msr daifset, #2; isb" ::: "memory");
        if (hcr_changed) {
            __asm__ volatile("msr hcr_el2, %0; isb" :: "r"(hcr_saved));
            hcr_changed = 0;
        }
    }
    cpu_on = on;
}

// Everything off: before `go`/`doom`, and when the monitor (re)starts.
void irq_off(void)
{
    cpu_set(0, 0);
    timer_set(0);
    if (uart_on)
        uart_set(0);
    if (sw1_on)
        sw1_set(0);
    if (ir_on)
        ir_set(0);
    gic_disable(ID_AON);
}

// ---------------- the command ----------------

static void show_line(const char *name, int id, int on)
{
    uint32_t cfg = (read32(GICD_ICFGR + id / 16 * 4) >> (id % 16 * 2)) & 3;
    printf("  %-22s %3d   %-3s  %d   %d   %d    %02x   %02x   %-5s %u\n", name, id,
           on ? "on" : "off", gic_bit(GICD_ISENABLER, id), gic_bit(GICD_ISPENDR, id),
           gic_bit(GICD_ISACTIVER, id), read8(GICD_IPRIORITYR + id),
           read8(GICD_ITARGETSR + id), cfg & 2 ? "edge" : "level", count[id]);
}

static void show(void)
{
    uint64_t hcr, daif, vbar, ctl;
    __asm__ volatile("mrs %0, hcr_el2" : "=r"(hcr));
    __asm__ volatile("mrs %0, daif" : "=r"(daif));
    __asm__ volatile("mrs %0, vbar_el2" : "=r"(vbar));
    __asm__ volatile("mrs %0, cnthp_ctl_el2" : "=r"(ctl));
    printf("CPU   HCR_EL2 %08lx (IMO %lu), DAIF %lx (IRQ %s), VBAR_EL2 %08lx\n",
           hcr, (hcr >> 4) & 1, daif >> 6, daif & 0x80 ? "masked" : "unmasked", vbar);
    printf("GIC   GICD_CTLR %x, GICC_CTLR %x, PMR %02x, RPR %02x, HPPIR %x, this CPU = target %02x\n",
           read32(GICD_CTLR), read32(GICC_CTLR), read32(GICC_PMR), read32(GICC_RPR),
           read32(GICC_HPPIR), gic_this_cpu());
    printf("  source                  ID   kmon en pend act  prio tgt  type  taken\n");
    show_line("EL2 timer (CNTHP)", ID_TIMER, timer_on);
    show_line("AON L2: SW1, IR", ID_AON, sw1_on || ir_on);
    show_line("UART0 receive", ID_UART0, uart_on);
    printf("timer CNTHP_CTL %lx (b2 = condition met), period %lu ms, ticks %lu\n",
           ctl, timer_period / TICKS_PER_MS, ticks);
    printf("AON   L2 STATUS %08x MASK %08x, GPIO MASK %08x STAT %08x\n", read32(AON_L2),
           read32(AON_L2 + 4), read32(GPIO_MASK), read32(GPIO_STAT));
    printf("UART0 IER %02x, rx dropped %u, WFI sleeps %u\n", read32(UART0 + 4), rx_dropped, wfi_count);
    printf("other spurious %u, unknown ID %u, AON other %u\n", spurious, unknown_id, aon_other);
}

static void watch(int (*key)(void))
{
    printf("interrupt events (any key stops; timer ticks shown once a second)\n");
    uint64_t t0 = cntpct(), last = t0, ticks0 = ticks;
    ev_tail = ev_head;
    while (!key()) {
        while (ev_tail != ev_head) {
            uint32_t i = ev_tail % EV_SIZE;
            uint64_t ms = (ev[i].t - t0) / TICKS_PER_MS;
            uint32_t v = ev[i].val;
            if (ev[i].what == EV_SW1)
                printf("%7lu ms: SW1 %s\n", ms, v ? "released" : "pressed");
            else if (ev[i].what == EV_IR || ev[i].what == EV_IR_REPEAT)
                printf("%7lu ms: IR %08x%s\n", ms, v, ev[i].what == EV_IR_REPEAT ? " (repeat)" : "");
            else
                printf("%7lu ms: unexpected interrupt ID %u (now disabled)\n", ms, v);
            ev_tail++;
        }
        uint64_t now = cntpct();
        if (timer_on && now - last >= 1000ul * TICKS_PER_MS) {
            printf("%7lu ms: timer ticks %lu (+%lu)\n", (now - t0) / TICKS_PER_MS, ticks, ticks - ticks0);
            ticks0 = ticks;
            last += 1000ul * TICKS_PER_MS;
        }
    }
}

static int onoff(int argc, char **argv, int i)
{
    if (i < argc && !strcmp(argv[i], "on"))
        return 1;
    if (i < argc && !strcmp(argv[i], "off"))
        return 0;
    return -1;
}

void irq_cmd(int argc, char **argv, int (*key)(void))
{
    const char *w = argc ? argv[0] : "";
    int on = onoff(argc, argv, 1);
    if (argc == 0) {
        show();
    } else if (!strcmp(w, "on")) {
        int imo = !(argc > 1 && !strcmp(argv[1], "noimo"));
        cpu_set(1, imo);
        printf("IRQs unmasked (DAIF.I = 0)%s\n", imo ? ", HCR_EL2.IMO = 1" : ", HCR_EL2 unchanged");
    } else if (!strcmp(w, "off")) {
        irq_off();
        printf("all interrupt sources off, IRQs masked\n");
    } else if (!strcmp(w, "timer") && argc > 1) {
        uint32_t ms = strcmp(argv[1], "off") ? (uint32_t)strtol(argv[1], NULL, 10) : 0;
        timer_set(ms);
        printf(ms ? "EL2 timer every %u ms (GIC ID 26)\n" : "EL2 timer off\n", ms);
    } else if (!strcmp(w, "uart") && on >= 0) {
        if (on && !(cpu_on && ticks)) {
            // WFI only wakes for interrupts the CPU can take: prove that first
            printf("first show that the CPU takes interrupts: irq on, irq timer 10, irq\n");
            return;
        }
        uart_set(on);
        printf("UART0 receive interrupt %s\n", on ? "on: the console reads a ring buffer, waits with WFI" : "off");
    } else if (!strcmp(w, "sw1") && on >= 0) {
        sw1_set(on);
        printf("SW1 interrupt %s (AON GPIO 14 -> AON L2 bit 3 -> ID 98)\n", on ? "on" : "off");
    } else if (!strcmp(w, "ir") && on >= 0) {
        ir_set(on);
        printf("IR interrupt %s (kbd1 -> AON L2 bit 0 -> ID 98)\n", on ? "on" : "off");
    } else if (!strcmp(w, "watch")) {
        watch(key);
    } else if (!strcmp(w, "clear")) {
        memset((void *)count, 0, sizeof count);
        spurious = unknown_id = aon_other = rx_dropped = wfi_count = 0;
        ticks = 0;
    } else {
        printf("usage: irq                     state of the GIC, CPU and sources\n"
               "       irq on [noimo]          unmask IRQs on the CPU (and set HCR_EL2.IMO)\n"
               "       irq off                 everything off\n"
               "       irq timer ms|off        EL2 timer interrupt, period in decimal ms\n"
               "       irq uart|sw1|ir on|off  UART0 receive, SW1 button, IR remote\n"
               "       irq watch               print SW1/IR events and timer rate\n"
               "       irq clear               zero the counters\n");
    }
}
