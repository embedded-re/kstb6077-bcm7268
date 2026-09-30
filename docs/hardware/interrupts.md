# Interrupts

## Controllers (from the original DTB)

| Device | Address | Notes |
|---|---|---|
| GIC (`arm,cortex-a15-gic`, GICv2) | distributor `0xffd01000`, CPU interface `0xffd02000` | root interrupt parent, 3-cell specifiers. **Not mapped under BOLT's MMU** |
| `brcm,l2-intc` | `0xf0403000` (sys), `0xf0201000`, `0xf0410640`, `0xf04d1200` | SoC-level interrupt muxes |
| `brcm,hif-spi-l2-intc` | `0xf0201a00` | HIF SPI interrupts |
| `brcm,bcm7271-l2-intc` | `0xf040a600`, `0xf0419c00` (upg_main_aon), `0xf040a640`, `0xf0419c40`, `0xf0419000` | level-type L2 controllers |
| GPIO | `0xf040a500` / `0xf0419c80` | GPIO banks as interrupt controllers (`gpio.md`) |

GIC numbering: DTB `<0x0 N ...>` = SPI *N* = GIC interrupt ID *N* + 32.

| Source | DTB | GIC ID |
|---|---|---|
| AON L2 (`upg_main_aon`, includes AON GPIO) | SPI `0x42` = 66 | 98 |
| UART0 | SPI `0x46` = 70 | 102 |

## AON L2 controller (`0xf0419c00`)

Level variant; the layout matches Linux `drivers/irqchip/irq-brcmstb-l2.c`:

| Offset | Register |
|---|---|
| `+0x0` | STATUS: raw state, visible even while masked |
| `+0x4` | MASK_STATUS |
| `+0x8` | MASK_SET |
| `+0xc` | MASK_CLEAR |

There is no clear register: a bit stays set until its source is cleared.

| Bit | Source |
|---|---|
| 0–2 | kbd1–3 (IR receivers) |
| 3 | gio (AON GPIO) |
| 4 | ldk (LED/keypad controller) |
| 5 | icap |
| 6 | spare |

BOLT leaves all sources masked (MASK_STATUS = `7f`). MASK_SET/MASK_CLEAR are
taken from the Linux driver and have not been written on this board.

## Interrupt chain: SW1 → CPU

```
SW1 (AON GPIO bank 0 bit 14)
  → GPIO STAT & MASK      0xf0419c9c / 0xf0419c94            verified
  → AON L2 bit 3 "gio"    0xf0419c00                         verified
  → GIC SPI 66 = ID 98    distributor 0xffd01000             not reachable under BOLT's MMU
  → CPU IRQ
```

Test from BOLT: `e -w 0xf0419c9c ffffffff` (clear GPIO STAT),
`e -w 0xf0419c94 00004000` (GPIO MASK bit 14), press SW1. Result: GPIO STAT =
`00004000`, and L2 STATUS = `00000008` with the L2 mask still `7f`. The GPIO
interrupt reaches the L2 with no trigger-type (EC/EI/LEVEL) setup.

The GIC hop needs the GIC mapped: `ISENABLER3`/`ISPENDR3` bit 2 for ID 98 are
at `0xffd0110c`/`0xffd0120c`. Options: map the `0xffd` megabyte in BOLT's
page table (`../bolt/bolt.md` §3), turn the MMU off, or run at a fresh
exception level with `go -64` / `boot -64 -el3` (`../booting.md`).

## CPU state under BOLT

BOLT runs in AArch32 SVC mode with IRQ and FIQ masked (`cpsr 800001d3`).
