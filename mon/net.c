// net.c - Ethernet for kmon: the GENET v5 MAC (polled), ARP, IPv4, ICMP
// echo, UDP, a DHCP client and a TFTP client (get and put).
//
// Register layout: Linux bcmgenet (v6.6, GENET v5 parameters: RDMA at
// +0x2000, TDMA at +0x4000, 3 words per descriptor, ring 16 = the default
// ring). The set-up copies what BOLT's own driver leaves after `ifconfig`
// (docs/hardware/ethernet.md): only ring 16, no receive/transmit status
// blocks, UMAC at 100 Mbit/s full duplex, CRC stripped on receive and
// appended on transmit. The frame buffers are in the monitor's
// non-cacheable DMA block, so no cache maintenance is needed.

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>

#include "kstb.h"
#include "mmc.h"
#include "net.h"

#define GENET           0xf0480000
#define SYS_PORT_CTRL   (GENET + 0x004)
#define SYS_RBUF_FLUSH  (GENET + 0x008)
#define INTRL2_0_CLEAR  (GENET + 0x208)
#define UMAC_CMD        (GENET + 0x808)
#define UMAC_MAC0       (GENET + 0x80c)
#define UMAC_MAC1       (GENET + 0x810)
#define UMAC_MAX_FRAME  (GENET + 0x814)
#define UMAC_TX_FLUSH   (GENET + 0xb34)
#define UMAC_MIB_CTRL   (GENET + 0xd80)
#define UMAC_MDIO_CMD   (GENET + 0xe14)

#define CMD_TX_EN       (1u << 0)
#define CMD_RX_EN       (1u << 1)
#define CMD_SPEED_100   (1u << 2)
#define CMD_SW_RESET    (1u << 13)

#define RDESC           (GENET + 0x2000)    // descriptors: length/status, address lo, hi
#define RRING           (GENET + 0x3000)    // ring 16 registers
#define RDMA            (GENET + 0x3040)    // RING_CFG, CTRL, STATUS, SCB_BURST_SIZE
#define TDESC           (GENET + 0x4000)
#define TRING           (GENET + 0x5000)
#define TDMA            (GENET + 0x5040)

// ring registers (RX names / TX names, same offsets)
#define R_WRITE_PTR     0x00                // TX: READ_PTR
#define R_PROD          0x08                // TX: CONS_INDEX
#define R_CONS          0x0c                // TX: PROD_INDEX
#define R_BUF_SIZE      0x10
#define R_START         0x14
#define R_END           0x1c
#define R_MBUF_DONE     0x24
#define R_XON_XOFF      0x28                // TX: FLOW_PERIOD
#define R_READ_PTR      0x2c                // TX: WRITE_PTR
#define T_CONS          0x08
#define T_PROD          0x0c

#define DMA_RING_CFG    0x00
#define DMA_CTRL        0x04
#define DMA_STATUS      0x08
#define DMA_BURST       0x0c
#define DMA_ON          (1u | 1u << 17)     // DMA_EN + ring 16 buffer enable

#define D_OWN           0x8000
#define D_EOP           0x4000
#define D_SOP           0x2000
#define D_TX_CRC        0x0040
#define D_RX_ERR        0x001f              // LG, NO, RXER, CRC, OV

#define N_RX            64
#define N_TX            4
#define BUF_LEN         2048
#define RX_BUF          (NET_BUF)
#define TX_BUF          (NET_BUF + N_RX * BUF_LEN)

#define TICKS_MS        27000

static int up;
static uint8_t mac[6];
static uint32_t my_ip, my_mask, my_gw, dns;
static uint32_t rx_cons, tx_prod;
static uint32_t n_rx, n_rx_err, n_tx, n_drop;

// ---------------- helpers ----------------

static uint64_t now(void)
{
    uint64_t t;
    __asm__ volatile("isb; mrs %0, cntpct_el0" : "=r"(t));
    return t;
}

static void udelay(uint32_t us)
{
    uint64_t end = now() + (uint64_t)us * 27;
    while (now() < end)
        ;
}

static void put16(uint8_t *p, uint32_t v) { p[0] = v >> 8; p[1] = v; }
static void put32(uint8_t *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }
static uint32_t get16(const uint8_t *p) { return p[0] << 8 | p[1]; }
static uint32_t get32(const uint8_t *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

static uint32_t csum(const uint8_t *p, int n)
{
    uint32_t s = 0;
    for (int i = 0; i + 1 < n; i += 2)
        s += get16(p + i);
    if (n & 1)
        s += p[n - 1] << 8;
    while (s >> 16)
        s = (s & 0xffff) + (s >> 16);
    return ~s & 0xffff;
}

static void print_ip(uint32_t ip)
{
    printf("%u.%u.%u.%u", ip >> 24, ip >> 16 & 255, ip >> 8 & 255, ip & 255);
}

int parse_ip(const char *s, uint32_t *ip)
{
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) {
        if (*s < '0' || *s > '9')
            return 0;
        uint32_t b = 0;
        while (*s >= '0' && *s <= '9')
            b = b * 10 + (*s++ - '0');
        if (b > 255 || (i < 3 && *s++ != '.'))
            return 0;
        v = v << 8 | b;
    }
    if (*s)
        return 0;
    *ip = v;
    return 1;
}

// ---------------- MAC ----------------

int net_mdio_read(int reg)
{
    write32(UMAC_MDIO_CMD, 1u << 29 | 2u << 26 | 1u << 21 | (uint32_t)reg << 16);  // start, read, PHY 1
    uint64_t end = now() + 10 * TICKS_MS;
    uint32_t v;
    while ((v = read32(UMAC_MDIO_CMD)) & 1u << 29)
        if (now() > end)
            return -1;
    return v & 1u << 28 ? -1 : (int)(v & 0xffff);
}

static int link_up(void)
{
    net_mdio_read(1);                       // BMSR: the link bit latches low, read twice
    int v = net_mdio_read(1);
    return v >= 0 && (v & 4);
}

void net_stop(void)
{
    if (!up)
        return;
    write32(TDMA + DMA_CTRL, read32(TDMA + DMA_CTRL) & ~DMA_ON);
    write32(RDMA + DMA_CTRL, read32(RDMA + DMA_CTRL) & ~DMA_ON);
    write32(UMAC_CMD, read32(UMAC_CMD) & ~(CMD_TX_EN | CMD_RX_EN));
    udelay(2000);                           // the UniMAC stops on a frame boundary
    write32(UMAC_TX_FLUSH, 1);
    udelay(10);
    write32(UMAC_TX_FLUSH, 0);
    up = 0;
}

static int read_mac(void)
{
    static uint8_t sec[512] __attribute__((aligned(64)));
    // macadr partition (LBA 34): the address at +0x0c, repeated (tested)
    if (mmc_init() || mmc_read(34, 1, sec))
        return 0;
    if (memcmp(sec + 0x0c, sec + 0x14, 6) || (sec[0x0c] & 1) || !memcmp(sec + 0x0c, "\xff\xff\xff\xff\xff\xff", 6))
        return 0;
    memcpy(mac, sec + 0x0c, 6);
    return 1;
}

int net_up(void)
{
    if (up)
        return 0;
    if (!read_mac()) {
        printf("net: no MAC address in the macadr partition (LBA 34)\n");
        return -1;
    }
    up = 1;
    net_stop();                             // DMA and MAC off, whatever BOLT left

    uint64_t end = now() + 10 * TICKS_MS;
    while (!(read32(RDMA + DMA_STATUS) & 1) || !(read32(TDMA + DMA_STATUS) & 1))
        if (now() > end) {
            printf("net: DMA doesn't stop (RDMA %08x, TDMA %08x)\n",
                   read32(RDMA + DMA_STATUS), read32(TDMA + DMA_STATUS));
            return -1;
        }

    // UniMAC reset (Linux reset_umac, bcmgenet_umac_reset), then 100 Mbit/s full duplex
    write32(SYS_RBUF_FLUSH, 0);
    udelay(10);
    write32(UMAC_CMD, CMD_SW_RESET);
    udelay(10);
    write32(SYS_RBUF_FLUSH, 2);
    udelay(10);
    write32(SYS_RBUF_FLUSH, 0);
    udelay(10);
    write32(UMAC_CMD, CMD_SPEED_100);
    udelay(10);
    write32(UMAC_MIB_CTRL, 7);
    write32(UMAC_MIB_CTRL, 0);
    write32(UMAC_MAX_FRAME, 0x5ee);
    write32(UMAC_MAC0, (uint32_t)mac[0] << 24 | mac[1] << 16 | mac[2] << 8 | mac[3]);
    write32(UMAC_MAC1, mac[4] << 8 | mac[5]);
    write32(INTRL2_0_CLEAR, 0xffffffff);    // all masked already; clear the latched bits

    // receive: ring 16, N_RX descriptors from index 0
    for (int i = 0; i < N_RX; i++) {
        write32(RDESC + i * 12 + 0, 0);
        write32(RDESC + i * 12 + 4, RX_BUF + i * BUF_LEN);
        write32(RDESC + i * 12 + 8, 0);
    }
    write32(RRING + R_PROD, 0);
    write32(RRING + R_CONS, 0);
    write32(RRING + R_BUF_SIZE, N_RX << 16 | BUF_LEN);
    write32(RRING + R_XON_XOFF, 5 << 16 | 16);
    write32(RRING + R_START, 0);
    write32(RRING + R_READ_PTR, 0);
    write32(RRING + R_WRITE_PTR, 0);
    write32(RRING + R_END, N_RX * 3 - 1);
    write32(RDMA + DMA_BURST, 0x10);

    // transmit: ring 16, N_TX descriptors from index 0
    for (int i = 0; i < N_TX; i++) {
        write32(TDESC + i * 12 + 0, 0);
        write32(TDESC + i * 12 + 4, TX_BUF + i * BUF_LEN);
        write32(TDESC + i * 12 + 8, 0);
    }
    write32(TRING + T_PROD, 0);
    write32(TRING + T_CONS, 0);
    write32(TRING + R_MBUF_DONE, 1);
    write32(TRING + R_XON_XOFF, 0);
    write32(TRING + R_BUF_SIZE, N_TX << 16 | BUF_LEN);
    write32(TRING + R_START, 0);
    write32(TRING + R_WRITE_PTR, 0);        // TX: READ_PTR
    write32(TRING + R_READ_PTR, 0);         // TX: WRITE_PTR
    write32(TRING + R_END, N_TX * 3 - 1);
    write32(TDMA + DMA_BURST, 0x10);

    write32(RDMA + DMA_RING_CFG, read32(RDMA + DMA_RING_CFG) | 1u << 16);
    write32(TDMA + DMA_RING_CFG, read32(TDMA + DMA_RING_CFG) | 1u << 16);
    write32(RDMA + DMA_CTRL, read32(RDMA + DMA_CTRL) | DMA_ON);
    write32(TDMA + DMA_CTRL, read32(TDMA + DMA_CTRL) | DMA_ON);
    write32(UMAC_CMD, CMD_SPEED_100 | CMD_TX_EN | CMD_RX_EN);

    rx_cons = tx_prod = 0;
    n_rx = n_rx_err = n_tx = n_drop = 0;
    up = 1;
    printf("net: GENET up, MAC %02x:%02x:%02x:%02x:%02x:%02x, link %s\n",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], link_up() ? "up" : "DOWN");
    return 0;
}

static int send_frame(const uint8_t *f, int len)
{
    if (len < 60)
        len = 60;                           // the caller's buffer is zero-padded
    uint64_t end = now() + 100 * TICKS_MS;
    while (((tx_prod - read32(TRING + T_CONS)) & 0xffff) >= N_TX)
        if (now() > end) {
            printf("net: transmit stuck (prod %u, cons %u)\n", tx_prod, read32(TRING + T_CONS) & 0xffff);
            return -1;
        }
    int d = tx_prod % N_TX;
    memcpy((void *)(uintptr_t)(TX_BUF + d * BUF_LEN), f, len);
    __asm__ volatile("dsb st" ::: "memory");
    write32(TDESC + d * 12, (uint32_t)len << 16 | D_SOP | D_EOP | D_TX_CRC);
    tx_prod = (tx_prod + 1) & 0xffff;
    write32(TRING + T_PROD, tx_prod);
    n_tx++;
    return 0;
}

// ---------------- frames out ----------------

static uint8_t txf[1536] __attribute__((aligned(16)));

struct arp_ent { uint32_t ip; uint8_t mac[6]; };
static struct arp_ent arp_cache[4];
static int arp_next;

static void arp_store(uint32_t ip, const uint8_t *m)
{
    for (int i = 0; i < 4; i++)
        if (arp_cache[i].ip == ip) {
            memcpy(arp_cache[i].mac, m, 6);
            return;
        }
    arp_cache[arp_next].ip = ip;
    memcpy(arp_cache[arp_next].mac, m, 6);
    arp_next = (arp_next + 1) % 4;
}

static void eth_head(uint8_t *f, const uint8_t *dst, uint32_t type)
{
    memcpy(f, dst, 6);
    memcpy(f + 6, mac, 6);
    put16(f + 12, type);
}

static void arp_send(int op, const uint8_t *tmac, uint32_t tip)
{
    static const uint8_t bcast[6] = { 255, 255, 255, 255, 255, 255 };
    memset(txf, 0, 60);
    eth_head(txf, op == 1 ? bcast : tmac, 0x0806);
    uint8_t *a = txf + 14;
    put16(a, 1);                            // Ethernet
    put16(a + 2, 0x0800);                   // IPv4
    a[4] = 6;
    a[5] = 4;
    put16(a + 6, op);
    memcpy(a + 8, mac, 6);
    put32(a + 14, my_ip);
    if (op == 2)
        memcpy(a + 18, tmac, 6);
    put32(a + 24, tip);
    send_frame(txf, 42);
}

// The MAC address for ip (through the gateway when it is off the subnet); 0 = none.
static const uint8_t *arp_resolve(uint32_t ip)
{
    static const uint8_t bcast[6] = { 255, 255, 255, 255, 255, 255 };
    if (ip == 0xffffffff)
        return bcast;
    if (my_mask && (ip & my_mask) != (my_ip & my_mask) && my_gw)
        ip = my_gw;
    for (int tries = 0; tries < 4; tries++) {
        for (int i = 0; i < 4; i++)
            if (arp_cache[i].ip == ip)
                return arp_cache[i].mac;
        arp_send(1, NULL, ip);
        uint64_t end = now() + 300 * TICKS_MS;
        while (now() < end) {
            net_poll();
            for (int i = 0; i < 4; i++)
                if (arp_cache[i].ip == ip)
                    return arp_cache[i].mac;
        }
    }
    return NULL;
}

static uint16_t ip_id;

static const uint8_t *resolve(uint32_t dst)
{
    const uint8_t *m = arp_resolve(dst);
    if (!m) {
        printf("net: no ARP reply from ");
        print_ip(dst);
        printf("\n");
    }
    return m;
}

// IPv4 packet with its payload already at txf + 34. dmac comes from
// resolve() before the payload is written: answering ARP or a ping while
// resolving uses txf too.
static int ip_send(uint32_t dst, int proto, int plen, const uint8_t *dmac)
{
    eth_head(txf, dmac, 0x0800);
    uint8_t *ip = txf + 14;
    ip[0] = 0x45;
    ip[1] = 0;
    put16(ip + 2, 20 + plen);
    put16(ip + 4, ip_id++);
    put16(ip + 6, 0x4000);                  // don't fragment
    ip[8] = 64;
    ip[9] = proto;
    put16(ip + 10, 0);
    put32(ip + 12, my_ip);
    put32(ip + 16, dst);
    put16(ip + 10, csum(ip, 20));
    if (34 + plen < 60)
        memset(txf + 34 + plen, 0, 60 - 34 - plen);
    return send_frame(txf, 34 + plen);
}

static int udp_send(uint32_t dst, int sport, int dport, const uint8_t *data, int len, const uint8_t *dmac)
{
    uint8_t *u = txf + 34;
    if (len > 1472 || (!dmac && !(dmac = resolve(dst))))
        return -1;
    put16(u, sport);
    put16(u + 2, dport);
    put16(u + 4, 8 + len);
    put16(u + 6, 0);                        // no checksum (allowed for IPv4)
    memmove(u + 8, data, len);
    return ip_send(dst, 17, 8 + len, dmac);
}

// ---------------- frames in ----------------

// One waiting UDP datagram for the port a client listens on.
static int udp_port;
static int udp_have, udp_len;
static uint32_t udp_src_ip, udp_src_port;
static uint8_t udp_data[1536];

static uint16_t ping_id = 0x4b4d, ping_seq_got;
static int ping_got;

static void rx_ip(const uint8_t *f, int len)
{
    const uint8_t *ip = f + 14;
    if (len < 34 || (ip[0] >> 4) != 4)
        return;
    int hl = (ip[0] & 15) * 4, tot = get16(ip + 2);
    if (tot > len - 14 || hl < 20 || (get16(ip + 6) & 0x3fff))
        return;                             // truncated, or a fragment
    uint32_t src = get32(ip + 12), dst = get32(ip + 16);
    const uint8_t *p = ip + hl;
    int plen = tot - hl;
    if (ip[9] == 1 && dst == my_ip && plen >= 8) {          // ICMP
        if (p[0] == 8) {                    // echo request: reply
            arp_store(src, f + 6);
            if (plen > 1472)
                return;
            memcpy(txf + 34, p, plen);
            txf[34] = 0;
            put16(txf + 36, 0);
            put16(txf + 36, csum(txf + 34, plen));
            ip_send(src, 1, plen, f + 6);
        } else if (p[0] == 0 && get16(p + 4) == ping_id) {
            ping_seq_got = get16(p + 6);
            ping_got = 1;
        }
    } else if (ip[9] == 17 && plen >= 8) {                  // UDP
        int dport = get16(p + 2), ulen = get16(p + 4);
        if (!udp_port || dport != udp_port || ulen < 8 || ulen > plen || udp_have)
            return;
        if (dst != my_ip && dst != 0xffffffff && my_ip)
            return;
        udp_src_ip = src;
        udp_src_port = get16(p);
        udp_len = ulen - 8;
        memcpy(udp_data, p + 8, udp_len);
        udp_have = 1;
        if (my_ip)
            arp_store(src, f + 6);
    }
}

static void rx_arp(const uint8_t *f, int len)
{
    const uint8_t *a = f + 14;
    if (len < 42 || get16(a) != 1 || get16(a + 2) != 0x0800)
        return;
    uint32_t sip = get32(a + 14), tip = get32(a + 24);
    if (!my_ip || tip != my_ip)
        return;
    arp_store(sip, a + 8);
    if (get16(a + 6) == 1)
        arp_send(2, a + 8, sip);
}

void net_poll(void)
{
    if (!up)
        return;
    static uint8_t f[1536];
    for (int n = 0; n < N_RX; n++) {
        uint32_t prod = read32(RRING + R_PROD) & 0xffff;
        if (prod == rx_cons)
            return;
        __asm__ volatile("dmb sy" ::: "memory");
        int d = rx_cons % N_RX;
        uint32_t ls = read32(RDESC + d * 12);
        int len = ls >> 16 & 0xfff;
        if ((ls & D_RX_ERR) || !(ls & D_SOP) || !(ls & D_EOP) || len < 14 || len > (int)sizeof f) {
            n_rx_err++;
        } else {
            memcpy(f, (const void *)(uintptr_t)(RX_BUF + d * BUF_LEN), len);
            n_rx++;
        }
        rx_cons = (rx_cons + 1) & 0xffff;
        write32(RRING + R_CONS, rx_cons);
        if (ls & D_RX_ERR || len < 14)
            continue;
        uint32_t type = get16(f + 12);
        if (type == 0x0806)
            rx_arp(f, len);
        else if (type == 0x0800)
            rx_ip(f, len);
        else
            n_drop++;
    }
}

// Wait up to ms for a datagram to udp_port.
static int udp_wait(uint32_t ms)
{
    uint64_t end = now() + (uint64_t)ms * TICKS_MS;
    while (now() < end) {
        net_poll();
        if (udp_have)
            return 1;
    }
    return 0;
}

// ---------------- commands ----------------

void net_set_ip(uint32_t ip, uint32_t mask, uint32_t gw)
{
    my_ip = ip;
    my_mask = mask;
    my_gw = gw;
    memset(arp_cache, 0, sizeof arp_cache);
}

void net_status(void)
{
    printf("net: %s", up ? "up" : "down");
    if (up)
        printf(", MAC %02x:%02x:%02x:%02x:%02x:%02x, link %s", mac[0], mac[1], mac[2], mac[3],
               mac[4], mac[5], link_up() ? "up" : "DOWN");
    printf("\n  ip ");
    print_ip(my_ip);
    printf(", mask ");
    print_ip(my_mask);
    printf(", gateway ");
    print_ip(my_gw);
    if (dns) {
        printf(", dns ");
        print_ip(dns);
    }
    printf("\n  frames: received %u, receive errors %u, ignored %u, sent %u\n", n_rx, n_rx_err, n_drop, n_tx);
    if (up)
        printf("  UMAC_CMD %08x, RX prod %u cons %u, TX prod %u cons %u\n", read32(UMAC_CMD),
               read32(RRING + R_PROD) & 0xffff, rx_cons, tx_prod, read32(TRING + T_CONS) & 0xffff);
}

int net_ping(uint32_t ip, int count)
{
    int ok = 0;
    if (!my_ip) {
        printf("ping: no address yet (net dhcp, or net ip a.b.c.d)\n");
        return 0;
    }
    for (int seq = 1; seq <= count; seq++) {
        const uint8_t *dmac = resolve(ip);
        if (!dmac)
            return ok;
        uint8_t *p = txf + 34;
        memset(p, 0, 64);
        p[0] = 8;
        put16(p + 4, ping_id);
        put16(p + 6, seq);
        memcpy(p + 8, "kmon ping", 9);
        put16(p + 2, csum(p, 64));
        ping_got = 0;
        uint64_t t = now();
        if (ip_send(ip, 1, 64, dmac))
            return ok;
        uint64_t end = t + 1000 * TICKS_MS;
        while (now() < end && !(ping_got && ping_seq_got == seq))
            net_poll();
        if (ping_got && ping_seq_got == seq) {
            uint64_t us = (now() - t) / 27;
            printf("reply from ");
            print_ip(ip);
            printf(": seq %d, %lu.%03lu ms\n", seq, us / 1000, us % 1000);
            ok++;
        } else
            printf("seq %d: no reply\n", seq);
        if (seq < count) {
            end = now() + 500 * TICKS_MS;
            while (now() < end)
                net_poll();
        }
    }
    return ok;
}

// ---------------- DHCP ----------------

static int dhcp_packet(uint8_t *b, uint32_t xid, int type, uint32_t req_ip, uint32_t server)
{
    memset(b, 0, 300);
    b[0] = 1;
    b[1] = 1;
    b[2] = 6;
    put32(b + 4, xid);
    put16(b + 10, 0x8000);                  // broadcast replies: we have no address yet
    memcpy(b + 28, mac, 6);
    put32(b + 236, 0x63825363);
    uint8_t *o = b + 240;
    *o++ = 53; *o++ = 1; *o++ = type;
    if (req_ip) {
        *o++ = 50; *o++ = 4; put32(o, req_ip); o += 4;
    }
    if (server) {
        *o++ = 54; *o++ = 4; put32(o, server); o += 4;
    }
    *o++ = 55; *o++ = 3; *o++ = 1; *o++ = 3; *o++ = 6;
    *o++ = 12; *o++ = 4; memcpy(o, "kstb", 4); o += 4;
    *o++ = 255;
    return o - b < 300 ? 300 : o - b;
}

// Parse a reply for xid; returns the DHCP message type, 0 if not ours.
static int dhcp_parse(uint32_t xid, uint32_t *yi, uint32_t *server, uint32_t *mask, uint32_t *gw, uint32_t *dn)
{
    const uint8_t *b = udp_data;
    if (udp_len < 244 || b[0] != 2 || get32(b + 4) != xid || get32(b + 236) != 0x63825363 ||
        memcmp(b + 28, mac, 6))
        return 0;
    *yi = get32(b + 16);
    int type = 0;
    for (int i = 240; i < udp_len && b[i] != 255;) {
        int opt = b[i];
        if (opt == 0) {
            i++;
            continue;
        }
        int l = b[i + 1];
        const uint8_t *v = b + i + 2;
        if (i + 2 + l > udp_len)
            break;
        if (opt == 53 && l >= 1) type = v[0];
        if (opt == 54 && l >= 4) *server = get32(v);
        if (opt == 1 && l >= 4) *mask = get32(v);
        if (opt == 3 && l >= 4) *gw = get32(v);
        if (opt == 6 && l >= 4) *dn = get32(v);
        i += 2 + l;
    }
    return type;
}

int net_dhcp(void)
{
    static const uint8_t bcast[6] = { 255, 255, 255, 255, 255, 255 };
    static uint8_t b[400];
    uint32_t xid = (uint32_t)now() ^ 0x4b535442, yi = 0, server = 0, mask = 0, gw = 0, dn = 0;
    if (net_up())
        return -1;
    my_ip = 0;
    udp_port = 68;
    for (int attempt = 0; attempt < 4; attempt++) {
        udp_have = 0;
        int n = dhcp_packet(b, xid, 1, 0, 0);
        udp_send(0xffffffff, 68, 67, b, n, bcast);
        uint64_t end = now() + 2000 * TICKS_MS;
        int type = 0;
        while (now() < end && type != 2) {
            udp_have = 0;
            if (udp_wait(100))
                type = dhcp_parse(xid, &yi, &server, &mask, &gw, &dn);
        }
        if (type != 2)
            continue;
        n = dhcp_packet(b, xid, 3, yi, server);
        udp_have = 0;
        udp_send(0xffffffff, 68, 67, b, n, bcast);
        end = now() + 2000 * TICKS_MS;
        type = 0;
        while (now() < end && type != 5 && type != 6) {
            udp_have = 0;
            if (udp_wait(100))
                type = dhcp_parse(xid, &yi, &server, &mask, &gw, &dn);
        }
        if (type == 5) {
            udp_port = 0;
            net_set_ip(yi, mask, gw);
            dns = dn;
            printf("dhcp: ");
            print_ip(yi);
            printf(", mask ");
            print_ip(mask);
            printf(", gateway ");
            print_ip(gw);
            printf(", from ");
            print_ip(server);
            printf("\n");
            return 0;
        }
        if (type == 6)
            printf("dhcp: server said NAK\n");
    }
    udp_port = 0;
    printf("dhcp: no answer\n");
    return -1;
}

// ---------------- TFTP ----------------

static uint32_t tftp_port(void)
{
    return 0xc000 | ((uint32_t)now() >> 4 & 0x3fff);
}

static int tftp_error(const char *what)
{
    if (udp_len >= 4 && get16(udp_data) == 5)
        printf("tftp: %s: server error %u: %.*s\n", what, get16(udp_data + 2), udp_len - 4, udp_data + 4);
    else
        printf("tftp: %s\n", what);
    udp_port = 0;
    return -1;
}

static int tftp_request(uint8_t *b, int op, const char *file, int blksize)
{
    int n = 0;
    put16(b, op);
    n = 2;
    n += strlen(strcpy((char *)b + n, file)) + 1;
    n += strlen(strcpy((char *)b + n, "octet")) + 1;
    if (blksize) {
        n += strlen(strcpy((char *)b + n, "blksize")) + 1;
        n += sprintf((char *)b + n, "%d", blksize) + 1;
    }
    return n;
}

// The blksize the server accepted in an OACK, else 512.
static int oack_blksize(void)
{
    for (int i = 2; i < udp_len;) {
        const char *k = (const char *)udp_data + i;
        int kl = strnlen(k, udp_len - i);
        const char *v = k + kl + 1;
        if (i + kl + 1 >= udp_len)
            break;
        int vl = strnlen(v, udp_len - i - kl - 1);
        if (!strcasecmp(k, "blksize")) {
            int bs = 0;
            for (const char *p = v; *p >= '0' && *p <= '9'; p++)
                bs = bs * 10 + *p - '0';
            return bs >= 8 && bs <= 1468 ? bs : 512;
        }
        i += kl + 1 + vl + 1;
    }
    return 512;
}

long tftp_get(uint32_t server, const char *file, uint8_t *buf, uint32_t max)
{
    static uint8_t req[600], ack[4];
    if (!my_ip) {
        printf("tftp: no address yet (net dhcp, or net ip a.b.c.d)\n");
        return -1;
    }
    int lport = tftp_port(), bs = 512, rport = 0, resend = 0;
    uint32_t got = 0, block = 1;            // next block expected
    int rlen = tftp_request(req, 1, file, 1468);
    udp_port = lport;
    udp_have = 0;
    if (udp_send(server, lport, 69, req, rlen, NULL))
        return tftp_error("can't send the request");
    const uint8_t *last = req;
    int last_len = rlen, last_port = 69;
    uint64_t t0 = now();
    for (;;) {
        udp_have = 0;
        if (!udp_wait(1000)) {
            if (++resend > 5)
                return tftp_error("timeout");
            udp_send(server, lport, last_port, last, last_len, NULL);
            continue;
        }
        if (udp_src_ip != server || (rport && (int)udp_src_port != rport) || udp_len < 4)
            continue;
        int op = get16(udp_data);
        if (op == 5)
            return tftp_error(file);
        if (!rport)
            rport = udp_src_port;
        if (op == 6 && block == 1 && got == 0) {    // OACK: the options were accepted
            bs = oack_blksize();
            put16(ack, 4);
            put16(ack + 2, 0);
        } else if (op == 3) {
            uint32_t n = get16(udp_data + 2);
            int dlen = udp_len - 4;
            if (n == (block & 0xffff)) {
                if (got + dlen > max) {
                    udp_port = 0;
                    printf("tftp: file is larger than %u bytes\n", max);
                    return -1;
                }
                memcpy(buf + got, udp_data + 4, dlen);
                got += dlen;
                block++;
            }
            put16(ack, 4);
            put16(ack + 2, n);              // ACK it (again, for a duplicate)
            udp_send(server, lport, rport, ack, 4, NULL);
            resend = 0;
            last = ack, last_len = 4, last_port = rport;
            if (n == ((block - 1) & 0xffff) && dlen < bs)
                break;
            continue;
        } else
            continue;
        udp_send(server, lport, rport, ack, 4, NULL);
        resend = 0;
        last = ack, last_len = 4, last_port = rport;
    }
    udp_port = 0;
    uint64_t ms = (now() - t0) / TICKS_MS;
    printf("tftp: %u bytes in %lu ms (%lu KB/s, %d-byte blocks)\n", got, ms, ms ? got / ms * 1000 / 1024 : 0, bs);
    return got;
}

long tftp_put(uint32_t server, const char *file, const uint8_t *data, uint32_t len)
{
    static uint8_t req[600], pkt[4 + 512];
    if (!my_ip) {
        printf("tftp: no address yet (net dhcp, or net ip a.b.c.d)\n");
        return -1;
    }
    int lport = tftp_port(), rport = 0, resend = 0, final = 0;
    int rlen = tftp_request(req, 2, file, 0);   // 512-byte blocks: every server takes them
    udp_port = lport;
    udp_have = 0;
    if (udp_send(server, lport, 69, req, rlen, NULL))
        return tftp_error("can't send the request");
    const uint8_t *last = req;
    int last_len = rlen, last_port = 69;
    uint32_t block = 0;                     // the block sent last (0 = the request)
    uint64_t t0 = now();
    for (;;) {
        udp_have = 0;
        if (!udp_wait(1000)) {
            if (++resend > 5)
                return tftp_error("timeout");
            udp_send(server, lport, last_port, last, last_len, NULL);
            continue;
        }
        if (udp_src_ip != server || (rport && (int)udp_src_port != rport) || udp_len < 4)
            continue;
        int op = get16(udp_data);
        if (op == 5)
            return tftp_error(file);
        if (!rport)
            rport = udp_src_port;
        if (op != 4 || get16(udp_data + 2) != (block & 0xffff))
            continue;                       // not the ACK for the block sent last
        if (final)
            break;
        uint32_t off = block * 512;
        int n = len - off > 512 ? 512 : len - off;  // 0 = the empty last block
        block++;
        put16(pkt, 3);
        put16(pkt + 2, block);
        memcpy(pkt + 4, data + off, n);
        udp_send(server, lport, rport, pkt, 4 + n, NULL);
        final = n < 512;
        resend = 0;
        last = pkt, last_len = 4 + n, last_port = rport;
    }
    udp_port = 0;
    uint64_t ms = (now() - t0) / TICKS_MS;
    printf("tftp: %u bytes sent in %lu ms (%lu KB/s)\n", len, ms, ms ? len / ms * 1000 / 1024 : 0);
    return 0;
}
