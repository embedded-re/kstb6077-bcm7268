# Interrupts

## Summary

| | |
|---|---|
| Root controller | ARM **GIC-400** (GICv2): distributor `0xffd01000`, CPU interface `0xffd02000`; 256 IDs, 4 CPU interfaces |
| Numbering | DTB SPI *N* = GIC ID *N* + 32 |
| Second level | Broadcom L2 controllers (`brcm,l2-intc`, `brcm,bcm7271-l2-intc`) in front of the GIC |
| State at handoff | GIC enabled, every SPI in group 1 (non-secure), **every SPI disabled and untargeted**, **GICC_PMR = `00`** (blocks everything); all L2 sources masked; HCR_EL2.IMO = 0 |
| Reachable | from AArch64 (`go -64`, `boot -64 -el3`); **not** under BOLT's 32-bit MMU |
| Tested | GIC state from EL2 and EL3; the stock kernel's interrupt counts per action; **taking interrupts at EL2** from the EL2 timer (ID 26), UART0 receive (ID 102), SW1 and the IR remote (both ID 98 through the AON L2) |
| Needed at EL2 | HCR_EL2.IMO = 1, GICC_PMR opened, DAIF.I clear, then per ID: enable, priority, target ("Taking interrupts at EL2") |

## Controllers (from the original DTB)

| Device | Address | Notes |
|---|---|---|
| GIC: **ARM GIC-400** (GICv2; DTB `arm,cortex-a15-gic`) | distributor `0xffd01000`, CPU interface `0xffd02000` | root interrupt parent, 3-cell specifiers. Not mapped under BOLT's MMU; reachable from AArch64 EL2/EL3 |
| `brcm,l2-intc` | `0xf0403000` (sys), `0xf0201000`, `0xf0410640`, `0xf04d1200` | SoC-level interrupt muxes |
| `brcm,hif-spi-l2-intc` | `0xf0201a00` | HIF SPI interrupts |
| `brcm,bcm7271-l2-intc` | `0xf040a600`, `0xf0419c00` (upg_main_aon), `0xf040a640`, `0xf0419c40`, `0xf0419000` | level-type L2 controllers |
| GPIO | `0xf040a500` / `0xf0419c80` | GPIO banks as interrupt controllers ([`gpio.md`](gpio.md)) |

GIC numbering: DTB `<0x0 N ...>` = SPI *N* = GIC interrupt ID *N* + 32.

## Every interrupt the DTB names

From the original and stock DTBs (identical here, except that the stock DTB
disables GENET 1 and has no SATA node), the stock kernel's boot log
("kernel log", 2026-10-03), and the stock kernel's `/proc/interrupts`. The
DTBs name no interrupt for the display, video decoder, audio, M2MC or V3D:
Nexus (`nexus.ko`) registers those itself, and `/proc/interrupts` shows
them.

### Straight to the GIC

Two sources. **Tested** = listed in `/proc/interrupts` of the stock Android
kernel (4.9.322) on the stock box, read over `adb shell` on 2026-10-04
([`../docs/evidence/stock/adb/proc_interrupts_1.txt`](../docs/evidence/stock/adb/proc_interrupts_1.txt)), with a count when the
interrupt had fired. **DTB** = only in the DTB (not tested). The kernel's
numbers in `/proc/interrupts` are the GIC IDs themselves.

| GIC ID | Name (stock kernel) | What | Fired | Source |
|---|---|---|---|---|
| 26, 27, 29, 30 | `arch_timer` | ARM generic timers (PPI) | 30: yes | tested |
| 32 | `BSP` | security processor (the DTB calls SPI 0 PCIe "bogus") | yes | tested |
| 33 | `SCPU` | security CPU | yes | tested |
| 36–39 | | CPU 0–3 performance monitors (PMU) | | DTB |
| 45 | `AIO` | audio input/output (FMM) | yes | tested |
| 46 | `GFX` | M2MC 2D graphics | yes | tested |
| 47 | `VEC` | video encoder (display timing); L2 `0xf06e7800` (`nexus.ko`'s map), all 15 sources masked under kmon | yes | tested |
| 48, 55 | `BVNB_0`, `BVNB_1` | video network back-end | | tested |
| 49–53 | `BVNF_0`, `_1`, `_5`, `_9`, `_16` | video network front-end; L2s `0xf0606000`, `+0x100`, `+0x500`, `+0x900`, `0xf0607000` (`nexus.ko`'s map). `BVNF_0` bit 0 = one per display frame, Linux's vblank ([`display.md`](display.md), "Frame interrupt") | `BVNF_0` | tested |
| 54 | `BVNM_0` | video network middle | | tested |
| 56 | `CLKGEN` | clock generator | | tested |
| 57 | | AVS L2 `0xf04d1200` | | kernel log |
| 58 | `DVP_HR` | HDMI receive path | | tested |
| 59 | `HDMI_TX` | **HDMI transmitter** (hot-plug, HDCP) | yes | tested |
| 60 | `HDMI_RX_0` | HDMI receiver (not fitted) | | tested |
| 63 | | HIF L2 `0xf0201000` | | kernel log |
| 64 | | HIF SPI L2 `0xf0201a00` | | kernel log |
| 69 | `mmc0` | SDHCI 0 (SD) | | tested |
| 70 | `mmc1` | SDHCI 1 (**eMMC**) | yes | tested |
| 78 | `PCIe PME, aerdrv, dhdpcie` | PCIe INTA: **Wi-Fi** (DTB `interrupt-map` INTA–INTD = 78–81, malformed, fixed by the kernel) | yes | tested |
| 83 | `PCIe0_msi` | PCIe MSI | | tested |
| 85 | `HVD0_0` | video decoder | yes | tested |
| 86 / 87 | `RAAGA` / `RAAGA_FW` | audio DSP / its firmware | 87: yes | tested |
| 88 | `MEMC0` | memory controller | | tested |
| 89 | | SATA AHCI (original DTB only) | | DTB |
| 91 | | sys L2 `0xf0403000` | | kernel log |
| 92 | `SYS_AON` | | | tested |
| 93 | | AON L2 `0xf0410640` (`sys_pm`, wake-up sources) | | kernel log |
| 94 | `UPG_AUX_AON` | | | tested |
| 95 | | UPG BSC L2 `0xf040a640` (I2C) | | kernel log |
| 96 | | UPG BSC AON L2 `0xf0419c40` (I2C) | | kernel log |
| 97 | | UPG main L2 `0xf040a600` | | kernel log |
| 98 | | UPG main AON L2 `0xf0419c00` (buttons, IR) | | kernel log |
| 99 | `UPG_SC` | smartcard | | tested |
| 100 | | UPG SPI AON L2 `0xf0419000` | | kernel log |
| 101 | `UPG_TMR` | UPG timers | yes | tested |
| 102, 103, 104 | `serial` | UART0, 1, 2 (only 102 in use) | | tested / kernel log |
| 109 / 110 | `V3D_INT` / `V3D_HUB_INT` | **V3D GPU** / its hub | yes | tested |
| 111–118, 120, 121 | `XPT_FE_STATUS0`, `XPT_OVFL`, `XPT_MSG_STAT`, `XPT_MSG`, `XPT_PCRXPT_DPCR0`, `XPT_RAV`, `XPT_STATUS_BUS`, `XPT_MCPB`, `XPT_WMDMA`, `XPT_EXTCARD` | transport (TV stream) | | tested |
| 122 / 123 | `ehci_hcd:usb3` / `ohci_hcd:usb5` | EHCI0 / OHCI0 (internal Bluetooth) | yes | tested |
| 124 | `xhci-hcd:usb1` | xHCI | yes | tested |
| 126 / 127 | `ehci_hcd:usb4` / `ohci_hcd:usb6` | EHCI1 / OHCI1 (USB-A port) | yes | tested |
| 128 | | USB device controller BDC (disabled) | | DTB |
| 129, 130 | `eth0` | GENET 0 Ethernet | 129: yes | tested |
| 131, 132 | | GENET 1 (unused) | | DTB |
| 138 | `MPM_TOP` | | | tested |
| 140 | `SID0_0` | still-image decoder | | tested |

### Through an L2 controller

| L2 controller | Bit | Source (DTB name) |
|---|---|---|
| sys `0xf0403000` | 0 | GISB timeout |
| | 2 | **GISB target error** (`gisb_tea`): tested, set by the probe's aborted reads |
| HIF `0xf0201000` | 4, 24 | NAND `flash_dma_done`, `nand_ctlrdy` (no NAND fitted; bits 24–25 read set) |
| HIF SPI `0xf0201a00` | 0–6 | QSPI (disabled): `spi_lr_*`, `mspi_done` (5), `mspi_halted` (6) |
| AON `0xf0410640` (`sys_pm`) | 1, 2, 3 | wake-ups: `cec`, `irr`, `kpd` |
| | 4 | wake timer |
| | 6 | GPIO wake-up (`upg_gio_aon_wakeup`, main and AON GPIO) |
| | 11 | `xpt_pmu` |
| | 13 | USB |
| | 18, 19 | Wake-on-LAN, GENET 0 / 1 |
| AVS `0xf04d1200` | 6 | temperature monitor (`tmon`) |
| | 26 | AVS CPU `sw_intr` (reads set) |
| UPG main `0xf040a600` | 0, 1, 2 | main GPIO (`gio`), `irb`, spare |
| UPG main AON `0xf0419c00` | 0–6 | see "AON L2 controller" below |
| UPG BSC `0xf040a640` | 0, 1, 2 | I2C `iica` (ch0, HDMI DDC), `iice` (ch4), spare ([`i2c.md`](i2c.md)) |
| UPG BSC AON `0xf0419c40` | 0, 1, 2, 3 | I2C `iicb` (ch1), `iicc` (ch2), `iicd` (ch3, device at `0x67`), spare ([`i2c.md`](i2c.md)) |
| UPG SPI AON `0xf0419000` | 0, 1 | MSPI `spi` (`mspi_done`), spare |

The stock kernel's `/proc/interrupts` attaches these L2 bits (tested, same
read as above; "fired" = non-zero count after ~10 minutes of use):

| L2 bit | Kernel handler | Fired |
|---|---|---|
| sys `0xf0403000` 0, 2 | `f0400000.gisb-arb` | |
| AON `0xf0410640` 1, 2, 3, 11 | `droid_pm` (CEC, IR, keypad, XPT wake-ups) | |
| AON `0xf0410640` 4 | `brcm-waketimer` | |
| AON `0xf0410640` 6 | `brcmstb-gpio-wake` (main and AON GPIO) | |
| AON `0xf0410640` 18 | `eth0` (Wake-on-LAN) | |
| AVS `0xf04d1200` 6 / 26 | `brcmstb_thermal` / `sw_intr` | 26: yes |
| UPG SPI AON `0xf0419000` 0 | `spi` | |
| UPG main `0xf040a600` 1 | `irb` | |
| UPG main AON `0xf0419c00` 0 | `kbd1`: **the remote** ([`ir.md`](ir.md)) | yes (410) |
| UPG main AON `0xf0419c00` 1, 2, 4, 5 | `kbd2`, `kbd3`, `ldk`, `icap` | |
| UPG BSC `0xf040a640` 0, 1 | I2C `iica`, `iice` | |
| UPG BSC AON `0xf0419c40` 0, 1 | I2C `iicb`, `iicc` | |
| UPG BSC AON `0xf0419c40` 2 | I2C **`iicd`**: the busy bus | yes (8024) |
| AON GPIO `0xf0419c80` pins 4, 5, 14 | `nexus gpio` (14 = SW1, [`gpio.md`](gpio.md)) | pin 4: yes (6) |

The wake-timer bit (4 of `0xf0410640`) did not show in STATUS while masked
([`system-blocks.md`](system-blocks.md)).

## What each action fires (tested)

Stock box, 2026-10-04, Android 11, `/proc/interrupts` read over `adb` once a second
while one thing was done at a time. "Background" lines move on their own
and were filtered out:

| Background (idle) | Rate |
|---|---|
| `VEC` (47), `BVNF_0` (49) | 50 per second each (502 in 10.05 s): one per display frame at 1080p50 |
| `iicd` (AON BSC `0xf0419c40` bit 2) | ~10 per second |
| `arch_timer`, `UPG_TMR` (101), `BSP` (32), `SCPU` (33), `eth0` (129, cable in) | continuous |

| Action | What moved |
|---|---|
| remote button | `kbd1` (AON L2 `0xf0419c00` bit 0): ~3 per press |
| SW1 (front button) | AON GPIO 14 (`nexus gpio`): one per edge, 2 per press. The box goes to standby / wakes |
| SW4 | **nothing**: Linux polls it (`gpio_keys_polled`) |
| HDMI cable out | `HDMI_TX` (59) +1 |
| HDMI cable in | `HDMI_TX` +53, then +1 |
| standby → wake (HDMI output back on) | `HDMI_TX` +54, then +1 |
| USB keyboard out (USB-A port) | OHCI1 (127) +4 |
| USB keyboard in | EHCI1 (126) +2, then OHCI1 +4, then OHCI1 +47 (enumeration) |

The HDMI hot-plug arrives on the HDMI transmitter's own interrupt; AON GPIO
4 and 5 (also claimed by Nexus) did not move for any of these. The box has
no SD card slot.

## L2 controller registers (tested)

All ten read on the stock box at `BOLT>` on 2026-10-03, with [`../docs/evidence/sys/l2_probe.s`](../docs/evidence/sys/l2_probe.s)
(output next to it), twice, 1 s apart. **Every word past the DTB's
`reg` size aborts**, so the sizes below are exact.

**`brcm,l2-intc`** (`0xf0403000` 0x48, `0xf0201000` 0x30, `0xf0201a00`
0x30, `0xf0410640` 0x30, `0xf04d1200` 0x48): two or three copies of the same
six registers, `0x18` apart. Names from Linux `irq-brcmstb-l2.c` (the first
copy); the values are from the board:

| Offset in a copy | Register | Read |
|---|---|---|
| `+0x00` | STATUS | the same bits in every copy |
| `+0x04` / `+0x08` | SET / CLEAR | `0` |
| `+0x0c` | MASK_STATUS | every source masked |
| `+0x10` / `+0x14` | MASK_SET / MASK_CLEAR | `0` |

**`brcm,bcm7271-l2-intc`** (`0xf040a600`, `0xf0419c00`, `0xf040a640`,
`0xf0419c40`, `0xf0419000`, all 0x20): STATUS `+0x0`, MASK_STATUS `+0x4`,
MASK_SET `+0x8`, MASK_CLEAR `+0xc`, and a second copy at `+0x10`. MASK reads
`7`, `7f`, `7`, `f`, `3`: one bit per source, exactly as many as the DTB
lists.

State at `BOLT>` (stock box, display running):

| Controller | STATUS | MASK |
|---|---|---|
| sys `0xf0403000` | `0`, then `4` (`gisb_tea`, after the probe's aborts) | `ffffffff` |
| HIF `0xf0201000` | `03000000` (bits 24, 25) | `ffffffff` |
| HIF SPI `0xf0201a00` | `0` | `7f` |
| AON `0xf0410640` | `0` | `007fffff` |
| AVS `0xf04d1200` | `04000000` (bit 26) | `07ffffff` |
| the five UPG controllers | `0` | `7`, `7f`, `7`, `f`, `3` |

GIC at the same moment: ISENABLER0 = `0000ffff` (SGIs only), ISPENDR and
ISACTIVER all `0`.

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
| 0–2 | kbd1–3 (IR receivers). Bit 0 tested: set while kbd1 holds a received code ([`ir.md`](ir.md)) |
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
  → GIC SPI 66 = ID 98    distributor 0xffd01000             verified from EL2 (not reachable under BOLT's MMU)
  → CPU IRQ at EL2                                           verified ("Taking interrupts at EL2")
```

Test from BOLT: `e -w 0xf0419c9c ffffffff` (clear GPIO STAT),
`e -w 0xf0419c94 00004000` (GPIO MASK bit 14), press SW1. Result: GPIO STAT =
`00004000`, and L2 STATUS = `00000008` with the L2 mask still `7f`. The GPIO
interrupt reaches the L2 with no trigger-type (EC/EI/LEVEL) setup.

The GIC hop needs the GIC reachable: `ISENABLER3`/`ISPENDR3` bit 2 for ID 98
are at `0xffd0110c`/`0xffd0120c`. From 32-bit code under BOLT that means
mapping the `0xffd` megabyte in BOLT's page table ([`../docs/02-bolt.md`](../docs/02-bolt.md) §3) or
turning the MMU off. From 64-bit code (`go -64` / `boot -64 -el3`) the GIC is
reachable directly (next section).

## GIC state at handoff (read from AArch64)

Read with an AArch64 probe ([`../docs/evidence/gic64b_probe.s`](../docs/evidence/gic64b_probe.s); output in
[`../docs/evidence/gic_probe_el2_el3.txt`](../docs/evidence/gic_probe_el2_el3.txt)), started by `go -64` (EL2, non-secure
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
  these PPI slots are not wired to Cortex-A53 timers (general GIC-400
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
entry to a program started at EL3, `SCR_EL3` = `0x131`: NS = 1 (the lower exception
levels are non-secure) and RW = 0 (the level below EL3 runs AArch32). That
matches BOLT's world being **non-secure AArch32** (inferred: this is the
SCR state left by the EL3 monitor).

## Taking interrupts at EL2 (tested)

Tested on 2026-10-15 on the modified box with kmon's `irq` command
([`../extras/mon/irq.c`](../extras/mon/irq.c), [`../extras/mon/README.md`](../extras/mon/README.md)), started with BOLT's `go -64`
(EL2). The GIC is at `0xffd01000`/`0xffd02000` in kmon's identity map
(Device memory).

### What it takes

| Step | Register | After BOLT | Set to |
|---|---|---|---|
| Vector table | `VBAR_EL2`; IRQ from the current EL on SP_EL2 = vector `+0x280` | garbage | the program's own |
| Route IRQs to EL2 | `HCR_EL2` bit 4 (IMO) | `0` (`HCR_EL2` = `80000002`) | `1` |
| CPU mask | `DAIF.I` | masked | cleared (`msr daifclr, #2`) |
| CPU interface priority mask | `GICC_PMR` `0xffd02004` | **`00`: nothing gets through** | wrote `ff`, reads back `f0` |
| CPU interface enable | `GICC_CTLR` | `1` (group 1 on) | left as it is |
| Distributor, per ID | `ISENABLER`, `IPRIORITYR` byte, `ITARGETSR` byte (SPIs only) | disabled, priority `00`, target `00` | enabled, `a0` (reads back `a0`), `01` |
| Handler | read `GICC_IAR` `0xffd0200c` (ID in bits 0–9), clear the source, write the same value to `GICC_EOIR` `0xffd02010` | | |

- **IMO decides it.** With the timer pending in the GIC (`ISPENDR` bit set,
  `GICC_HPPIR` = `1a` = ID 26), `GICC_PMR` open and `DAIF.I` clear, the CPU
  did **not** take the interrupt while IMO was 0. Setting IMO = 1 made it
  taken at once.
- The `ITARGETSR` bytes for IDs 0–31 read `01`: the boot CPU is GIC CPU
  interface 0, so SPIs are targeted at `01`.
- `ICFGR` reads "level" for IDs 26, 98 and 102.
- No spurious interrupts (ID 1023) were counted in any of the tests below.

### Sources

| Source | ID | Set-up | Clearing it in the handler | Result |
|---|---|---|---|---|
| EL2 physical timer (CNTHP) | 26 (PPI) | `CNTHP_CVAL_EL2` = now + period, `CNTHP_CTL_EL2` = 1 | `CNTHP_CVAL_EL2` += period | 10 ms period: exactly +100 per second for 5 s |
| UART0 receive | 102 | UART `IER` (`0xf040c004`) bit 0 | read RBR until LSR bit 0 is 0 | one interrupt per byte. A 64 KB `kstb-send` arrived intact (CRC matched, 0 bytes dropped, 5.7 s, the same as polled); the CPU sleeps in WFI between bytes |
| SW1 | 98 | AON GPIO EI bit 14, MASK bit 14; AON L2 `MASK_CLEAR` (`0xf0419c0c`) bit 3 | AON GPIO STAT bit 14 (write 1) | one interrupt on the press and one on the release |
| IR remote (kbd1) | 98 | NEC set-up ([`ir.md`](ir.md)); AON L2 `MASK_CLEAR` bit 0 | write 0 to bit 0 of kbd1 STATUS `0xf0419900` | one interrupt per IR frame ([`ir.md`](ir.md)) |

- The AON L2's `MASK_CLEAR` (`+0xc`) and `MASK_SET` (`+0x8`) work as in the
  Linux driver: MASK_STATUS went from `7f` to `76` with bits 0 and 3
  cleared, and to `77` with only bit 3 cleared.
- **SW1 edge:** after BOLT the AON GPIO EC, EI and LEVEL registers
  (`0xf0419c8c`, `0xf0419c90`, `0xf0419c98`) are all `0`. That gives one
  interrupt per press and **none on release**: the handler only ever saw the
  pin low, apart from bounce. Setting EI bit 14 (`0xf0419c90` = `00004000`)
  gives both edges, as under stock Android.

