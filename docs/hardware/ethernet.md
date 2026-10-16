# Ethernet: GENET 0 and the internal PHY

One Ethernet port: the SoC's **GENET v5** MAC at `0xf0480000` with the
**BCM7268 internal PHY** at MDIO address 1. The PHY is 10/100 only; with a
cable to a normal switch the link comes up at **100 Mbit/s full duplex**.
GENET 1 (`0xf04a0000`) is clocked but unused ([`peripherals.md`](peripherals.md)).

Tested on the stock box on 2026-10-04 at `BOLT>` after a power-on, cable
connected, BOLT's network not started (`ifconfig` not run). Probe and
outputs: [`../evidence/sdhci_genet/`](../evidence/sdhci_genet/). Register names are Linux `bcmgenet`
(GENET v5 layout) and IEEE 802.3 clause 22; the values are tested.

## Summary

| | |
|---|---|
| MAC | GENET v5 at `0xf0480000` (GENET 1 at `0xf04a0000`: clocked, unused) |
| PHY | BCM7268 internal PHY, ID `0xae025091`, MDIO address 1, 10/100 only |
| Link | 100 Mbit/s full duplex to a normal switch |
| State after BOLT | MAC idle: TX and RX off, **no MAC address**, interrupts masked |
| Register names | Linux `bcmgenet` (GENET v5 layout), IEEE 802.3 clause 22 |
| Tested | register reads at `BOLT>`, PHY reads with `mii read`, BOLT's own network ([`../bolt/bolt.md`](../bolt/bolt.md) §9) |
| Not tested | sending or receiving frames from bare-metal code |

## Blocks

| Offset | Block | Read after BOLT |
|---|---|---|
| `+0x000` | SYS: `+0x0` revision | `06000000` (GENET v5) |
| `+0x080` | EXT | `+0x0` = `05000240` |
| `+0x200` / `+0x240` | INTRL2_0 / INTRL2_1 (STATUS `+0x0`, MASK_STATUS `+0xc`) | `0x200`: STATUS `00800034`, all masked (`07ffffff`) |
| `+0x300` | RBUF | `+0x0` = `0000c040` |
| `+0x600` | TBUF | `+0x0` = `0` |
| `+0x800` | UMAC | see below |
| `+0xe14` | MDIO command | see below |

Inside these blocks several words abort (e.g. `+0x030–0x03f`,
`+0x050–0x07f`, `+0x304`, `+0x604`); the full map is in the probe output.

## UMAC (`0xf0480800`)

| Offset | Name | Value | Meaning |
|---|---|---|---|
| `+0x008` | UMAC_CMD | `010000d8` | TX and RX **off** (bits 0, 1 clear) |
| `+0x00c` / `+0x010` | UMAC_MAC0 / MAC1 | `0` / `0` | **no MAC address programmed** |
| `+0x014` | max frame length | `000005ee` | 1518 bytes |

So after BOLT the MAC is idle and has no address: a bare-metal driver must
write the MAC address itself. The address the stock firmware uses is in the
DTB BOLT passes to Linux (`local-mac-address`, [`device-tree.md`](device-tree.md)).

INTRL2_0 STATUS `00800034` = bits 2, 4, 5, 23: in Linux's names PHY detect
(rising), **link up**, link down, MDIO done. They latched during BOLT's boot
and stay set while masked.

## MDIO (`0xf0480e14`) and the PHY

`+0xe14` still held BOLT's last MDIO access: `08217809` = a read (bits 27:26
= `2`) of PHY 1 (bits 25:21), register 1 (bits 20:16), result `0x7809`
(bits 15:0). `+0xe18` (MDIO config) = `00000091`.

PHY registers, read with BOLT's `mii read mdio0 1 <reg>` (cable in):

| Reg | Name | Value | Meaning |
|---|---|---|---|
| 0 | BMCR | `0x3000` | auto-negotiation on |
| 1 | BMSR | `0x7829`, then `0x782d` | link bit (2) is latched-low: first read 0, second read **1 = link up**; auto-negotiation complete; 10/100 half/full abilities, no gigabit |
| 2, 3 | PHY ID | `0xae02`, `0x5091` | BCM7268 internal PHY rev 1 ([`../bolt/bolt.md`](../bolt/bolt.md)) |
| 4 | advertised | `0x01e1` | 10/100 half/full |
| 5 | link partner | `0xc5e1` | 10/100 half/full, pause |

Both sides advertise 100 full duplex, so that is the link speed (matches
BOLT's `100 Mbps Full-Duplex` in [`../bolt/bolt.md`](../bolt/bolt.md)).

## What BOLT's driver sets up (tested 2026-10-16)

The same registers read with kmon's abort-safe `md`: once after a plain
boot (BOLT's network never started), once after `ifconfig eth0 -auto` and
a `ping`, with kmon started by `go -64 -noclose` so BOLT left the network
open. The block layout is Linux `bcmgenet`'s for GENET v5 (RDMA at
`+0x2000`, TDMA at `+0x4000`, 3 words per descriptor, 256 descriptors, ring
registers after them, ring 16 = the default ring); every value below fits
it.

| Register | After a plain boot | After BOLT `ifconfig` | Meaning |
|---|---|---|---|
| `+0x004` SYS_PORT_CTRL | `0` | `0` | port mode 0 = internal EPHY |
| `+0x300` RBUF_CTRL | `0000c040` | `0000c040` | no 64-byte receive status block, no 2-byte alignment |
| `+0x808` UMAC_CMD | `010000d8` | `00000007` | TX + RX on, speed field 1 = 100 Mbit/s, full duplex, not promiscuous, CRC not forwarded |
| `+0x80c` / `+0x810` MAC0 / MAC1 | `0` / `0` | `90f891e7` / `0000000a` | 90:f8:91:e7:00:0a |
| `+0x3000` RX ring 16 | zeros | buffer size `00400800` (64 descriptors × 2048), end `0xbf` (= 64 × 3 − 1), producer `0x39`, consumer `0x1c` | 64 receive buffers at `0x0765xxxx` (BOLT's RAM) |
| `+0x2000` RX descriptors | zeros | e.g. `003c7fc0`, `07651000`, `0`: 60 bytes, start + end of frame, broadcast | received frames arrive **without CRC** (a 60-byte ARP) |
| `+0x5000` TX ring 16 | zeros | buffer size `00010800`: **1** descriptor, end `2`; producer = consumer = 9 | |
| `+0x4000` TX descriptor | zeros | `002a6040`, `07672000`, `0`: 42 bytes, `SOP + EOP + append CRC` | BOLT sends short frames unpadded |
| `+0x3040` / `+0x5040` RING_CFG, CTRL, STATUS, BURST | `00010000`, `0`, `0003ffff`, `10` | `00010000`, **`00020001`**, `0001fffe`, `10` | CTRL = DMA on + ring 16 |

The MAC address is stored in the `macadr` partition (LBA 34), in binary at
offset `0x0c` and repeated after it: `90 f8 91 e7 00 0a ff ff 90 f8 …`.

## The kmon driver (tested 2026-10-16)

[`../../mon/net.c`](../../mon/net.c): polled, ring 16 only, set up like BOLT's
(above), with 64 receive and 4 transmit buffers of 2 KB in kmon's
non-cacheable DMA block (`0x00700000`). Order: DMA off and wait for
DMA_STATUS bit 0, UniMAC soft reset (UMAC_CMD bit 13, then
SYS_RBUF_FLUSH_CTRL bit 1 toggled, as Linux does), UMAC_CMD = 100 Mbit/s,
MAC address from `macadr`, descriptors and ring registers, DMA_CTRL =
`00020001` on both, UMAC_CMD = `00000007`. Receive: frames between the
software consumer index and RX ring `+0x08` (producer); the consumer index
is written back to `+0x0c`. Transmit: the descriptor's length/status is
written, then the producer index `+0x0c`; ring `+0x08` is the hardware's
consumer index.

| Test | Result |
|---|---|
| `net up` after a plain boot, and after BOLT's network was left open | MAC programmed, link up (PHY register 1), frames received |
| DHCP | `192.168.1.33`, mask `255.255.255.0`, gateway `192.168.1.1` from the router; also after a reset with BOLT's network never started |
| ping kmon → PC | 4 of 4, 0.17–0.24 ms |
| ping PC → kmon (answered from the idle prompt) | 4 of 4, 0 % loss, 0.20 ms average |
| TFTP get, 844,800 bytes from [`tools/tftpd`](../../tools/tftpd) | 420 ms (~1.9 MB/s, 512-byte blocks), CRC `cbc5f298` = the file |
| TFTP put, the same 844,800 bytes | 421 ms, the PC's file has CRC `cbc5f298` |
| `net stop` | both DMA_CTRL `0`, UMAC_CMD `4` (TX and RX off); `net up` again works |

## Open questions

- Throughput with larger TFTP blocks: kmon asks for `blksize 1468`, but
  [`tools/tftpd`](../../tools/tftpd) ignores options and answers with 512-byte blocks.
  dnsmasq accepts the option (not tested).
- Interrupts (GENET's INTRL2) instead of polling; more than one RX/TX ring.
