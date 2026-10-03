# Ethernet: GENET 0 and the internal PHY

One Ethernet port: the SoC's **GENET v5** MAC at `0xf0480000` with the
**BCM7268 internal PHY** at MDIO address 1. The PHY is 10/100 only; with a
cable to a normal switch the link comes up at **100 Mbit/s full duplex**.
GENET 1 (`0xf04a0000`) is clocked but unused (`peripherals.md`).

Tested on the stock box on 2026-10-04 at `BOLT>` after a power-on, cable
connected, BOLT's network not started (`ifconfig` not run). Probe and
outputs: `../bolt/raw/sdhci_genet/`. Register names are Linux `bcmgenet`
(GENET v5 layout) and IEEE 802.3 clause 22; the values are tested.

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
DTB BOLT passes to Linux (`local-mac-address`, `device-tree.md`).

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
| 2, 3 | PHY ID | `0xae02`, `0x5091` | BCM7268 internal PHY rev 1 (`../bolt/bolt.md`) |
| 4 | advertised | `0x01e1` | 10/100 half/full |
| 5 | link partner | `0xc5e1` | 10/100 half/full, pause |

Both sides advertise 100 full duplex, so that is the link speed (matches
BOLT's `100 Mbps Full-Duplex` in `../bolt/bolt.md`).
