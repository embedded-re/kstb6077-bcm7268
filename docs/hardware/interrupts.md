# Interrupts

## Controllers (from the original DTB)

| Device | Address | Notes |
|---|---|---|
| GIC: **ARM GIC-400** (GICv2; DTB `arm,cortex-a15-gic`) | distributor `0xffd01000`, CPU interface `0xffd02000` | root interrupt parent, 3-cell specifiers. Not mapped under BOLT's MMU; reachable from AArch64 EL2/EL3 |
| `brcm,l2-intc` | `0xf0403000` (sys), `0xf0201000`, `0xf0410640`, `0xf04d1200` | SoC-level interrupt muxes |
| `brcm,hif-spi-l2-intc` | `0xf0201a00` | HIF SPI interrupts |
| `brcm,bcm7271-l2-intc` | `0xf040a600`, `0xf0419c00` (upg_main_aon), `0xf040a640`, `0xf0419c40`, `0xf0419000` | level-type L2 controllers |
| GPIO | `0xf040a500` / `0xf0419c80` | GPIO banks as interrupt controllers (`gpio.md`) |

GIC numbering: DTB `<0x0 N ...>` = SPI *N* = GIC interrupt ID *N* + 32.

| Source | DTB | GIC ID |
|---|---|---|
| AON L2 (`upg_main_aon`, includes AON GPIO) | SPI `0x42` = 66 | 98 |
| UART0 | SPI `0x46` = 70 | 102 |
| UART1 | SPI `0x47` = 71 | 103 |
| UART2 | SPI `0x48` = 72 | 104 |

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

The GIC hop needs the GIC reachable: `ISENABLER3`/`ISPENDR3` bit 2 for ID 98
are at `0xffd0110c`/`0xffd0120c`. From 32-bit code under BOLT that means
mapping the `0xffd` megabyte in BOLT's page table (`../bolt/bolt.md` §3) or
turning the MMU off. From 64-bit code (`go -64` / `boot -64 -el3`) the GIC is
reachable directly (next section).

## GIC state at handoff (read from AArch64)

Read with an AArch64 probe (`../bolt/raw/gic64b_probe.s`; output in
`../bolt/raw/gic_probe_el2_el3.txt`), started by `go -64` (EL2, non-secure
view) and `boot -64 -el3` (EL3, secure view). No read aborted.

### Identity

| Register | Value | Meaning |
|---|---|---|
| GICD_IIDR | `0200143b` | ARM (`0x43b`), product `0x02` = **GIC-400**, revision 1 |
| GICC_IIDR | `0202143b` | GIC-400 CPU interface |
| GICD_TYPER | `0000fc67` | ITLinesNumber 7 → **256 interrupt IDs** (0–255, so SPIs 32–255); CPUNumber 3 → **4 CPU interfaces**; SecurityExtn = 1; LSPI 31 |

### Control and groups

| Register | EL2 (non-secure view) | EL3 (secure view) |
|---|---|---|
| GICD_CTLR | `1`: group 1 enabled | `3`: group 0 and group 1 enabled |
| GICC_CTLR | `1` | `3` |
| IGROUPR0 (IDs 0–31) | reads 0 (RAZ from non-secure) | `fe00ffff` |
| IGROUPR1–7 (IDs 32–255) | reads 0 (RAZ from non-secure) | **`ffffffff` each** |

- **Every SPI (IDs 32–255) is in group 1 (non-secure)**, including ID 98
  (AON L2 / SW1) and ID 102 (UART0). A non-secure OS can see and configure
  all of them.
- IDs 0–15 (SGIs) and 25–31 (PPIs, including the ARM generic timers
  26/27/29/30) are group 1. **IDs 16–24 are group 0 (secure).** On GIC-400
  these PPI slots are not wired to Cortex-A53 timers (⚠️ general GIC-400
  knowledge).

### Enables and routing

| Register | Value |
|---|---|
| ISENABLER0 | `0000ffff`: SGIs 0–15 enabled (always enabled on GIC-400) |
| ISENABLER1–7 | `0`: **no SPI enabled** |
| ISPENDR3 | `0`: nothing pending in IDs 96–127 |
| ITARGETSR (IDs 96–99) | `0`: no CPU targets set |

The GIC is handed over enabled but with every SPI disabled and untargeted.
The OS (or bare-metal code) enables and routes each interrupt it uses.

## CPU state under BOLT

BOLT runs in AArch32 SVC mode with IRQ and FIQ masked (`cpsr 800001d3`). On
entry to our EL3 code, `SCR_EL3` = `0x131`: NS = 1 (the lower exception
levels are non-secure) and RW = 0 (the level below EL3 runs AArch32). That
matches BOLT's world being **non-secure AArch32** (⚠️ inferred: this is the
SCR state left by the EL3 monitor).
