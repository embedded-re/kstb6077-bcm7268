# Peripheral blocks: every known hardware block on the BCM7268

Every hardware block found so far, from three sources:

1. **The device trees** (`boot/stock_dtb.dts`, `boot/original_dtb.dts`): every
   node with an address. They name the "open" hardware Linux drives.
2. **BOLT's code**: the peripheral addresses in its literal pools and data
   tables (from the RAM dump, `../bolt/bolt.md` §11). These include blocks
   the DTBs don't name (display, transport DMA).
3. **A read of the first word of each block** from EL2 with an abort-safe
   probe (`../evidence/periph/periph_probe.s`, output
   `periph_probe_output.txt`), on the modified box, 2026-10-02.

Register-level details for blocks already in use are in `registers.md`.
The multimedia blocks (video decoder, 2D graphics, transport, audio DSP) are
driven by Broadcom's closed Nexus software; the DTB's memory-controller client
list names them (`bvn_*`, `hvd0`, `m2mc_0–2`, `raaga`, `aud_aio`, `v3d`,
`sid`, `xpt_*`, `vec_*`), but not their addresses.

## Reading the table

- **First word** = what the probe read at that address after `go -64`.
  **abort** = a synchronous external abort (`ESR_EL2 = 96000210`: data
  abort, external, on a read). The block is there but switched off or
  clock-gated. From BOLT's `d` an abort hangs the board; from the probe it
  is caught.
- A **revision** in the first word that matches the DTB's `compatible`
  string identifies the block for certain (marked "ID match").

## The blocks

| Address | Block | Source | First word | Notes |
|---|---|---|---|---|
| `0xf0200000` | SDHCI 0 host (removable-slot type; no card, no slot on the case; `storage.md`) | DTB `sdhci-brcmstb` | `00000000`; `+0xfc` = `10020000` | `+0xfe` = `1002`: SD host spec 3.00 (ID match) |
| `0xf0200100` | SDHCI 0 config | DTB | `40003c03` | |
| `0xf0200200` | SDHCI 1 host (**eMMC**) | DTB | `076a3018`; `+0xfc` = `10020000` | SD host spec 3.00 (ID match); BOLT reads `flash0` through it; 8-bit, 50 MHz (`storage.md`) |
| `0xf0200300` | SDHCI 1 config | DTB | `40003c03` | |
| `0xf0200154` / `354` / `400` | SDIO pin select / boot control (syscon) | DTB | `2` / `1` / `0` | |
| `0xf0201000` | HIF L2 interrupt controller | DTB `brcm,l2-intc` | `03000000` | status bits 24–25 set |
| `0xf0201a00` | HIF SPI L2 interrupt controller | DTB | `00000000` | |
| `0xf0203000` | NAND controller (disabled in DTB) | DTB `brcmnand-v7.2` | `00000702` | revision **7.2** (ID match). The box has no NAND |
| `0xf0203a00` / `3c00` | BSPI / QSPI (disabled) | DTB `spi-bcm-qspi` | `00000402` / `0` | |
| `0xf0205000` | CPU BIU control | DTB `brcmstb-cpu-biu-ctrl` | `00fffff2` | PSCI powers cores on via `+0x8c` (`cpu-cores.md`) |
| `0xf032c800` | BSP (security processor interface) | DTB `brcm,bsp-v4.2.5` | `00000000` | |
| `0xf0380000` | XPT security / SHA | BOLT (`XPTDMA: SHA !RDY`) | `+0x88` = `0` | used to hash images |
| `0xf0400000` | **GISB arbiter** (register bus) | DTB `brcm,gisb-arb` | `00000502` | **error capture**: see below |
| `0xf0402800` | **nothing**: a false hit in BOLT's code (see "The four switched-off blocks") | — | **abort** | not a BOLT address |
| `0xf0403000` | system L2 interrupt controller | DTB | `0` | `interrupts.md` |
| `0xf0404000` | SUN_TOP_CTRL (chip ID, reset, pin mux) | DTB | `72680010` | `system-blocks.md` |
| `0xf0408000` / `0xf0409000` | PWM 0 / 1 | DTB `bcm7038-pwm` | `00000022` | |
| `0xf040a300` / `a400` | I2C (BSC) ch0 = HDMI DDC / ch4 | Nexus `BI2C_*` | `0` | `i2c.md` |
| `0xf040a500` | main GPIO | DTB | | `gpio.md` |
| `0xf040a600` / `a640` | UPG L2 interrupt controllers | DTB | `0` / `0` | |
| `0xf040a6a8` | watchdog | DTB | | `system-blocks.md` |
| `0xf040c000` / `d000` / `e000` | UART0 / 1 / 2 | DTB | | `uart.md` |
| `0xf0410000` | AON control + AON SRAM `0xf0410200` | DTB | | `system-blocks.md` (`+0x2c` aborts) |
| `0xf0410640` | AON L2 interrupt controller | DTB | `0` | |
| `0xf0410700` / `0714` | AON pin mux / pad control | DTB | | `gpio.md` |
| `0xf0418000` | MSPI (SPI master) | DTB | `0` | |
| `0xf0419000`, `0xf0419c00`, `0xf0419c40` | AON UPG L2 interrupt controllers | DTB | `0` | `c00` = buttons/IR (`interrupts.md`) |
| `0xf0419900`, `0xf0419980`, `0xf0419a00` | IR receiver (KBD) channels kbd1–3 | Nexus `BKIR_*` | `0` | kbd1 = the remote (`ir.md`) |
| `0xf0419a80`, `0xf0419b00`, `0xf0419b80` | I2C (BSC) ch1–3; ch3 has a device at `0x67` (Si2168C demodulator per Nexus) | Nexus `BI2C_*` | `0` | `i2c.md` |
| `0xf0419c80` | AON GPIO | DTB | | `gpio.md` |
| `0xf041a080` | wake timer | DTB | | `system-blocks.md` |
| `0xf0452000` | HIF continuation (CPU boot addresses) | DTB `brcmstb-hif-continuation` | `0` | PSCI writes each core's start address at `+0x8/+0x10/+0x18` (`cpu-cores.md`) |
| `0xf0460000` | **PCIe** (Wi-Fi BCM43570) | DTB `bcm7268-pcie` | **abort** | held in reset by `+0x9210` bit 1; released: `726814e4` (PCI ID 14e4:7268) |
| `0xf0480000` | **GENET 0** Ethernet | DTB `genet-v5` | `06000000` | major 6 = GENET **v5** (ID match) (Linux numbering); `ethernet.md` |
| `0xf04a0000` | GENET 1 (disabled) | DTB | `06000000` | clocked, unused |
| `0xf04c4000` | AVS CPU data memory | DTB | `0` | |
| `0xf04d1100` / `1200` | AVS L2 interrupt / L2 controller | DTB | `0` / `04000000` | |
| `0xf04d1500` | temperature sensor | DTB | | `system-blocks.md` |
| `0xf04e0000–0xf04e7fff` | clock generator (PLLs, dividers, gates, muxes) | DTB (≈100 clock nodes), Nexus | `+0` = `00000016` | full read: `clkgen_dump_output.txt` (418 words answer, 7,774 abort) |
| `0xf0603000`, `0xf0604000` | display RDC (register DMA) | BOLT | | `display.md` (frame counter, flip) |
| `0xf0641000` | display GFD (graphics feeder) | BOLT | | `display.md` (`+0x5c` aborts) |
| `0xf0645000`, `0xf0650000`, `0xf06e0000–0xf06e7fff` | display pipeline (compositor/encoder, inferred) | BOLT's splash script | not read | written by the display set-up |
| `0xf06fa000` | HDMI transmitter | BOLT | | `audio.md` (N/CTS; `0x828`/`0x884…` break the picture) |
| `0xf09b0000` | **M2MC 2D blitter** | Nexus `BGRC_` (bus `0x209b0000`) | `+0x0c` = `0` | powered and clocked after BOLT; `2d-blitter.md` |
| `0xf0a00000` | XPT (transport) control | BOLT (`0x0700ede0`, called by the XPT DMA and SHA code) | `+0x21c` = `1f` | before XPT DMA, BOLT writes 1 to `+0xc0` and `+0xb4`, waits for `+0xbc` bit 0 to clear, then writes 1 to `+0xb8` |
| `0xf0a68000–0xf0a70fff` | XPT DMA (transport DMA) | BOLT (`XPTDMA: DMA !DONE`) | `0` | BOLT's memory-to-memory DMA for SHA |
| `0xf0b00200` | USB control / PHY | DTB | `00022033` | `usb.md` |
| `0xf0b00300–0xf0b01fff` | EHCI/OHCI/xHCI | DTB | | `usb.md` |
| `0xf0b02000` | USB device controller BDC (disabled) | DTB | **abort** | held in reset by `0xf0b00234` bit 23; released: `00100a05` |
| `0xf0b10100` | SATA PHY (original DTB only) | DTB | `0` | |
| `0xf0b12000` | **SATA AHCI** (original DTB only) | DTB `sata3-ahci` | `e9327f00` | AHCI CAP: 1 port, 64-bit, NCQ. BOLT has a SATA driver. Caution: No SATA connector seen on the board |
| `0xf0ca0000`, `0xf0cb0000` | audio (FMM, ring buffers, output ports) | BOLT | `0xf0ca0048` = `2800`, `0xf0cb0200` = `010843ff` | readable even without `pcm0` (`audio.md`) |
| `0xf1100000` | memory controller: general | DTB `memc-gen-rev-b.2.2` | `0000b220` | revision **B.2.2** (ID match) |
| `0xf1101000` | memory controller: arbiter | DTB | `00000148` | client list in the DTB |
| `0xf1102000` | memory controller: DDR | DTB `memc-ddr-rev-b.2.2` | `0000aa65` | |
| `0xf1108000` | DDR shim PHY | DTB | `4024201f` | |
| `0xf1109000` / `0xf1110000` | memory controller DTU config / map | DTB `memc-dtu-config-v1.1.0.0` | `00001100` / `80000000` | config **1.1** (ID match) |
| `0xf1120000` | DDR PHY | DTB `ddr-phy-v72.0` | `00004800` | `0x48` = **72** (ID match) |
| `0xf1200000–0xf120bfff` | **V3D GPU** (VideoCore 3D, v3.3): hub `0xf1200000`, bridge `0xf1204000`, GCA `0xf1204100`, core0 `0xf1208000` | DTB `bcm7268-v3d` (no `reg`) | **abort** everywhere | power island off; powered up via `0xf041d020`: hub IDENT1 `000e1133` = V3D 3.3 |
| `0xffd01000` | GIC-400 | DTB | | `interrupts.md` |
| `0xffe00000` | boot SRAM, 128 KB | DTB | `00000000` | all readable from EL2, almost all zero (`memory-map.md`) |

## The four switched-off blocks

Investigated on the modified box, 2026-10-02. Probes (read-only, abort-safe): `clk_gate_probe.s`, `swinit_probe.s`,
`pcie_usbctrl_probe.s` in `../evidence/periph/`, outputs next to them.

### Clocks are not the reason (tested)

Every clock gate the stock DTB lists for these blocks is **on**:

| Gate | Address | Value | DTB meaning | State |
|---|---|---|---|---|
| `pcie0_alwayson` | `0xf04e0450` | `0` | bit 0, 1 = off | on |
| `sys_108/54/gisb/scb_pcie0` | `0xf04e0458` | `f` | bits 0–3, 1 = on | all on |
| `osc_cml_usb_sat_pci` (AON) | `0xf0410074` | `00c00078` | bit 4, 1 = on | on |
| `usb0_gisb` | `0xf04e04a4` | `1` | bit 0, 1 = on | on |
| `sys_108/scb_usbd` (BDC) | `0xf04e04bc` | `3` | bits 0–1, 1 = on | on |
| for comparison: `sys_*_usb20`, `sys_*_usb30`, GENET `0xf04e03e0` | | `7`, `3`, `000fffff` | | on (these blocks work) |

The DTBs list no clocks or power domains for the V3D GPU.

### Reset: BOLT's reset registers (from BOLT's code)

BOLT puts blocks into reset with **SUN_TOP_CTRL `0xf0404318` (set)** and
releases them with **`0xf040431c` (clear)**, one bit per block:

| Bit | Where in BOLT | What (inferred) |
|---|---|---|
| 29 | `0x07012344`, just before every `go`/`boot` | pulsed (set, delay, clear) |
| 26, 27 | `0x07011e48`, when the board's PHY type is `INT` | the internal Ethernet PHY / GENET |
| 18 | `0x0701303c` (`SSBL_SEC` code), `0x0700ec52` | set, then cleared |

Both registers read `0` (write-only), and `0xf0404320–0x33c` read `0`.
`0xf0404314` reads `0b` (`8b` after a watchdog reset): probably reset history,
not reset status. No bank-0 reset status was found, so this doesn't show
whether the switched-off blocks are held here.

### PCIe and the USB device controller: held in reset (tested)

`reset_release_probe.s` released one reset bit at a time, read the block,
and put the bit back. Register names in quotes are from the Linux drivers
(`drivers/pci/controller/pcie-brcmstb.c`, `drivers/phy/broadcom/phy-brcm-usb-init.c`,
`drivers/usb/gadget/udc/bdc/bdc.h`); the values are from the board.

| | Before | Bit released | Bit restored |
|---|---|---|---|
| PCIe `0xf0469210` ("RGR1_SW_INIT_1") | `3` | `1` (bit 1 cleared) | `3` |
| `0xf046406c` ("MISC_REVISION") | abort | **`00000304`** | abort |
| `0xf0460000` | abort | **`726814e4`** | abort |
| USB control `0xf0b00234` ("USB_PM") | `00700000` | `00f00000` (bit 23 set) | `00700000` |
| BDC `0xf0b02000` ("BDCCFG0") | abort | **`00100a05`** | abort |
| BDC `0xf0b02004` | abort | `00000000` | abort |

- **PCIe**: bit 1 of `0xf0469210` holds the PCIe controller in reset.
  Released, the core answers: `0xf0460000` = `726814e4` is a PCI ID,
  vendor `14e4` (Broadcom), device `7268` (this chip): the controller's own
  configuration header. Bit 0 (Linux: PERST, the reset line to the card) was
  left set; the Wi-Fi chip's power (AON GPIO 21/26) wasn't touched.
- **USB device controller (BDC)**: bit 23 of `0xf0b00234` holds it in reset
  (0 = reset). Bits 20–22 (USB 2.0 and xHCI resets, Linux) are already 1.
  `0xf0b00290` ("USB_DEVICE_CTL1", port mode in bits 1:0) reads `00050500`:
  port mode 0 = host in Linux's naming.
- Putting each bit back made the block abort again. The probe kept running
  and printing through both tests (the display and USB host were not checked).
- The Linux driver for this chip family matches: `0x72680000` → family
  7271A0, where `USB_PM` bit 23 is `BDC_SOFT_RESETB`.

The rest of the USB control block (`0xf0b00200–0xf0b002fc`), as read:

```
+00 00022033  +04 484a1023  +08 000c0020  +0c 10002400
+10 10002200  +14 80013000  +18 00003000  +1c 00000000
+20 00000000  +24 00000000  +28 e0000000  +2c 01020102
+30 00000000  +34 00700000  +38 f8000000  +3c 00000000
+40 0000001f  +44 00000000  +48 00000000  +4c 00000000
+50 0008e38e  +54 00000000  +58 00000000  +5c 00000031
+60 00190018  +64 20000004  +68 09700971  +6c 00000000
+70 10021002  +74 00000000  +78 00000000  +7c 00000000
+80 abort     +84 abort     +88 abort     +8c abort
+90 00050500  +94 20000004  +98 00a4cb80  +9c abort
+a0 0000b000  +a4 00000000  +a8 00000000  +ac 00013010
+b0 00000000  +b4 00000000  +b8 … +ec abort
+f0 d058298a  +f4 c2640201  +f8 00000315  +fc 00000002
```

### V3D GPU: off by its power island

Every V3D register aborts after BOLT, although its clocks are on. The GPU
sits behind a power island, switched by `0xf041d020`. Power-up, reset and
everything driven since: [`gpu.md`](gpu.md).

### `0xf0402800`: not a block (correction)

It isn't a BOLT address. The 32-bit value `0xf0402800` is the byte pattern of
two common Thumb instructions (`… f040` + `2800` = `cmp r0, #0`) and appears
about 100 times in BOLT's code. The one hit taken for a register was code
read as a literal pool. Nothing names this address, so it should not have
been read. The read aborted: nothing answers there.

## GISB arbiter error capture (tested)

Every peripheral access goes over the GISB register bus. When an access
fails, the arbiter records it. After the probe's four aborts:

| Register | Value | Meaning |
|---|---|---|
| `0xf04007ec` | `f0402800` | **the address of the last failed access** |
| `0xf04007f8` | `00000040` | the bus master that did it, one bit per master: bit 6 = `cpu_0` (below) |
| `0xf04007e4` | `00000000` | |
| `0xf0400008` | `000278d0` | probably the GISB timeout |

BOLT prints these as `GISB Address`, `GISB Data`, `GISB Master` (its code at
`0x07023354`). So after a crash, `d -w 0xf04007ec 4` in BOLT should show
which address caused it, if the board can be brought back without power loss
(not tried across a watchdog reset).

### The whole block (tested)

Every word of `0xf0400000–0x7ff`, read on 2026-10-03 by `../evidence/sys/addrmap1_probe.s`
(output next to it):

| Range | Result |
|---|---|
| `+0x000–0x1e4` | readable. `+0x000` = `00000502` (revision), `+0x008` = `000278d0`, `+0x1d4` = `000000fb`, `+0x1d8` = `77983d77`, `+0x1dc` = `3fbcfffe`, `+0x1e0` = `0000003d`, `+0x1e4` = `018a02c1`; all others `0` |
| `+0x1e8–0x7e0` | **abort** (every word) |
| `+0x7e4–0x7fc` | readable: `0`, `0`, `f04007e0`, `0`, `0000083d`, `00000040`, `0` |

The capture registers then held the probe's own last abort: `+0x7ec` =
`f04007e0` (address), `+0x7f8` = `40` (master `cpu_0`), `+0x7f4` =
`0000083d` (status after a failed read; bits not decoded).

**Master numbers.** The DTB's `brcm,gisb-arb-master-mask = <0x18a0fc3>` has
12 set bits for its 12 `brcm,gisb-arb-master-names`, in order. The CPU's
aborts gave bit 6 = `cpu_0` (tested); the others are the DTB's (not tested):

| Bit | Master | | Bit | Master |
|---|---|---|---|---|
| 0 | `bsp_0` | | 10 | `rdc_0` (display register DMA) |
| 1 | `scpu_0` | | 11 | `hvd_0` (video decoder) |
| 6 | `cpu_0` (tested) | | 17 | `raaga_0` (audio DSP) |
| 7 | `webcpu_0` | | 19 | `pcie_0` |
| 8 | `jtag_0` | | 23 | `bbsi_spi` |
| 9 | `ssp_0` | | 24 | `avs_0` |

The DTB lists no V3D master.

## Bus addresses ("RDB")

PSCI prints register addresses like `RDB: 20452008` and `@ 2020508c`. These
are **bus addresses**: the same registers the CPU reaches at `0xf0452008`
(HIF continuation) and `0xf020508c` (CPU BIU control). The CPU's peripheral
window `0xf0000000` corresponds to bus address `0x20000000` (tested: both
match DTB blocks; the rule is inferred from these two).

## The abort-safe probe

`periph_probe.s` is reusable for any new address list:

- A vector table at `VBAR_EL2`. The synchronous handler records `ESR_EL2`
  and adds 4 to `ELR_EL2`, which skips the faulting load, then returns.
- After each read: `dsb sy; isb`, unmask SError (`msr DAIFClr, #4`), mask it
  again. A delayed error from that read is taken right there and attributed
  to the right address.
- All four aborts on this board were synchronous (`ESR 96000210`); no SError
  was seen.
- The watchdog stays armed in case an access never completes.

## Open

- Bringing PCIe further up (PERST, the Wi-Fi chip's power) and using the BDC.
- Using the V3D GPU further: see "Not mapped yet" in [`gpu.md`](gpu.md).
- The display pipeline blocks' names (`0xf0645000`, `0xf0650000`, `0xf06e0000…`).
- The other Nexus multimedia blocks (video decoder, audio DSP, transport) are
  listed by the memory controller but have no known address.
