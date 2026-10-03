# Peripheral blocks: every known hardware block on the BCM7268

Every hardware block found so far, from three sources:

1. **The device trees** (`boot/stock_dtb.dts`, `boot/original_dtb.dts`): every
   node with an address. They name the "open" hardware Linux drives.
2. **BOLT's code**: the peripheral addresses in its literal pools and data
   tables (from the RAM dump, `../bolt/bolt.md` §11). These include blocks
   the DTBs don't name (display, transport DMA).
3. **A read of the first word of each block** from EL2 with an abort-safe
   probe (`../bolt/raw/periph/periph_probe.s`, output
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

## The four switched-off blocks (investigated 2026-10-02)

Probes (read-only, abort-safe): `clk_gate_probe.s`, `swinit_probe.s`,
`pcie_usbctrl_probe.s` in `../bolt/raw/periph/`, outputs next to them.

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

### V3D GPU: off by its power island, switched on and off (tested)

Register ranges (Linux binding `brcm,bcm-v3d.yaml`, example `brcm,7268-v3d`;
the stock `nexus.ko` uses the same addresses as bus `0x212xxxxx`): hub
`0xf1200000`, bridge (reset control) `0xf1204000`, GCA cache controller
`0xf1204100`, core0 `0xf1208000`; interrupts SPI 78 and 77.

After BOLT, every V3D register aborts, the bridge included (`v3d_probe.s`),
although the V3D clocks are on (see "Clocks" below). The switch is the
**power island control register `0xf041d020`**, found in the stock driver
(`nexus.ko`: `BVC5_P_HardwareBPCMPowerUp` / `…PowerDown`, bus `0x2041d020`;
`docs/stock-drivers.md`). `v3d_power_probe.s` used Nexus's sequence:

| Step | Write `0xf041d020` | Wait for | Took | `0xf041d020` after |
|---|---|---|---|---|
| after BOLT | — | | | `424e0908` |
| power up | `0x00001d00` | `(v & 0x74000000) == 0x34000000` | 1,649 µs | `3c001908` |
| power down (Nexus does it only if bit 26 is set) | `0x00000b00` | `(v & 0x72000000) == 0x42000000` | 1,648 µs | `424e0908` |

Powered up, the GPU answers (abort before and after):

| Register | Value | Meaning (Linux `v3d_regs.h` field names) |
|---|---|---|
| bridge `0xf1204000` | `00000200` | revision 2.0 |
| hub IDENT0 `0xf1200008` | `42554856` | ASCII "VHUB" |
| hub IDENT1 `0xf120000c` | `000e1133` | version 3, revision 3 = **V3D 3.3**; 1 core, 1 host; bits 17–19 set (TFU, TSY, MSO) |
| hub IDENT2 `0xf1200010` | `00000100` | bit 8: has an MMU |
| hub IDENT3 `0xf1200014` | `00000100` | IP revision 1 |
| core0 IDENT0 `0xf1208000` | `03443356` | ASCII "V3D" + version 3 |
| core0 IDENT1 `0xf1208004` | `41101423` | VPM 4 KB, 16 semaphores, 1 TMU, 4 QPUs per slice, 2 slices, revision 3 |
| core0 IDENT2 `0xf1208008` | `40078121` | not decoded |

This matches the DTB's `brcm,v3d-v3.3.1.0`.

**Clocks** (`clkgen_dump.s`, read after BOLT; register meanings from Nexus's
`BCHP_PWR_P_HW_ControlId`): V3D PLL regulator `0xf04e01d4` = `1`, PLL power
`0xf04e01e4` = `1`, PLL reset `0xf04e01e8` = `0` (released), channel 0
`0xf04e01c0` = `0000000c` (not held, divider 6), core clock mux `0xf04e04d0`
= `0`, V3D clock enables `0xf04e04c8` = `0000001f`. Powering the island was
enough; no clock was changed.

### V3D GPU: reset and default registers (tested 2026-10-02)

`../bolt/raw/v3d/v3d_reset_probe.s` (output next to it) powers up, then
replays the stock driver's bring-up: `BVC5_P_HardwareResetV3D` and
`BVC5_P_HardwareSetDefaultRegisterState` from `nexus.ko`. It reads the
registers after each step and powers down at the end. No access aborted.

**Reset** (Nexus's order; the last write is from Linux `v3d_reset_by_bridge`):

| Write | Meaning (Linux `v3d_regs.h`) | Result |
|---|---|---|
| `0xf12041b0` = `1` | GCA `SAFE_SHUTDOWN` enable | `0xf12041b4` (`SAFE_SHUTDOWN_ACK`) read `3` within the 100 ms timeout |
| `0xf1204008` = `1`, then `0` | bridge `SW_INIT_0` (bit 0: `V3D_CLK_108_SW_INIT`) | `0xf12041b0` and `…b4` read `0` again: the GCA was reset too |
| `0xf1200000` = `f` | hub `AXICFG` (`MAX_LEN` 15) | |

Right after power-up, every register already reads its reset value. The
reset changed none of the 34 registers read: a power-up starts the GPU from
reset.

**Reset values** (after power-up, and the same after the reset):

| Register | Value | Meaning |
|---|---|---|
| GCA `0xf1204100` / `0xf120410c` / `0xf1204120` | `00010002` / `00111400` / `00100000` | `+0x0c` is `CACHE_CTRL`; the others have no Linux name |
| GCA `0xf12041d0` / `0xf12041d4` | `00000001` / `00000000` | Nexus writes them from its settings (values not known) |
| hub `AXICFG` `0xf1200000` | `0000000f` | |
| hub `INT_STS` / `INT_MSK_STS` `0xf1200050` / `…5c` | `0` / `0` | no hub interrupt pending, none masked |
| `TFU_CS` `0xf1200400` | `00002000` | `NFREE` = 32, `CVTCT` = 0, not busy |
| `TFU_SU` `0xf1200404` | `0` | |
| `MMUC_CONTROL` `0xf1201000` | `0` | MMU cache off |
| `0xf1201100`, `MMU_CTL` `0xf1201200`, `MMU_PT_PA_BASE` `0xf1201204` | `0` | **GPU MMU off**, no page table |
| core `MISCCFG` `0xf1208018` | `00000006` | `QRMAXCNT` = 3 |
| core `L2TFLSTA` / `L2TFLEND` `0xf1208034` / `…38` | `0` / `ffffffff` | |
| core `INT_STS` / `INT_MSK_STS` `0xf1208050` / `…5c` | `0` / `0` | |

**Default registers** (Nexus's 15 writes in order, minus `0xf12041d0` and
`0xf12041d4`, whose values come from Nexus settings):

| Write | Meaning | Reads back |
|---|---|---|
| `0xf1208060` = `ffffffff`, `0xf1208058` = `ffffffff`, `0xf1208064` = `7` | core `INT_MSK_SET` (mask all), `INT_CLR`, `INT_MSK_CLR` (unmask bits 0–2) | `INT_MSK_STS` = `00ff0038`: the core has interrupt bits 0–5 and 16–23, and bits 0–2 are unmasked |
| `0xf1204100` = `000e0000` | GCA `+0x00` | `000e0000` |
| `0xf1204120` = `000f0002` | GCA `+0x20` | `000f0002` |
| `0xf1208018` = `1` | core `MISCCFG`: `OVRTMUOUT`, `QRMAXCNT` 0 | `00000001` |
| `0xf1200000` = `f` | hub `AXICFG` | `0000000f` |
| `0xf1201000` = `1` | `MMUC_CONTROL`: `ENABLE` | `00000001` |
| `0xf1208138` = `1` | core `CLE_RFC` | `00000000` |
| `0xf12081b0` = `3` | core `+0x1b0` (no Linux name) | `00000001` |
| `0xf1200470` = `3` | hub `+0x470` (no Linux name) | `00000001` |
| `0xf1208034` = `0`, `0xf1208038` = `ffffffff` | core `L2TFLSTA`, `L2TFLEND` | unchanged |

Three registers read back something other than what was written:
`CLE_RFC` (`0`), core `+0x1b0` (`1`) and hub `+0x470` (`1`).

### V3D GPU: first job, a TFU conversion (tested 2026-10-02)

The TFU (texture formatting unit) reads an image from memory and writes it
back in one of the GPU's tiled layouts. It needs no shader and no control
list. `../bolt/raw/v3d/v3d_tfu_probe.s` (output next to it) does the
bring-up above, then runs one job with the **GPU MMU off** (its reset state):
the GPU uses physical addresses, and no page table is needed.

Sources: the register order and fields come from `nexus.ko`
`BVC5_P_HardwareIssueTFUJob`. The format codes come from the name tables in
the stock `libGLES_nexus.so` (`v3d_maybe_desc_tfu_type`, `…_iformat`,
`…_oformat`, `…_rgbord`). Some of the codes:

| Field | Codes |
|---|---|
| type (`ICFG` bits 9–15) | 0 `r8`, 2 `rg8`, **4 `rgba8`**, 6 `rgb565`, 7 `rgba4`, 8 `rgb5_a1`, 9 `rgb10_a2`, 14 `rgba16`, 18 `rgba16f`, 31 `rgba32f`, 32–38 ETC2/EAC, 39–47 YUV, 48–50 BC1–3, 64–77 ASTC, 90–92 YUV 3-plane |
| input format (`ICFG` bits 18–21) | **0 `raster`**, 1 `sand_128`, 2 `sand_256`, 11 `lineartile`, 12/13 `ublinear_1/2`, 14 `uif_no_xor`, 15 `uif_xor` |
| output format (`IOA` bits 3–5) | **3 `lineartile`**, 4/5 `ublinear_1/2`, 6 `uif_no_xor`, 7 `uif_xor` (no raster output) |
| RGB order (`ICFG` bits 16–17) | 0 `rgba`, 1 `abgr`, 2 `argb`, 3 `bgra` |

The job. The CPU wrote the buffers first (CPU MMU off, so they went straight
to DRAM):

| Step | Value |
|---|---|
| input `0x02000000` | 16×16 pixels, 32 bits each, pixel (x, y) = `0xaa00yyxx` |
| output `0x02100000` | 64 KB filled with `0xdeadbeef` |
| `0xf1200424` `COEF0` | `0` |
| `0xf1200420` `IOS` | `00100010` (height 16 << 16, width 16) |
| `0xf120041c` `IOA` | `02100018` (address, output format 3) |
| `0xf1200414` `IIS` | `00000010` (stride 16 **pixels**) |
| `0xf1200418` `IUA`, `0xf1200410` `ICA` | `0` |
| `0xf120040c` `IIA` | `02000000` |
| `0xf1200408` `ICFG` | `00000801` (`IOC`, type 4, input format 0): **this write starts the job** |

Result:

- `TFU_CS` went from `00002000` to `00012000`: `CVTCT` (conversions done)
  counted to 1. The poll saw it at its first read. (The 1.7 ms the probe
  printed is UART time, not job time: see "M2MC 2D blitter: palette-8".)
- The hub's `INT_STS` `0xf1200050` read `00000002` (`TFUC`: TFU done). The
  CPU's IRQs were masked, so no interrupt was taken.
- The job registers `IIA` … `COEF0` read `0` both before and after the job.
  `ICFG` read back `00000801`.
- Exactly 256 words (1 KB) of the output changed, nothing in the other 63 KB.
  The CPU saw the result straight away. A GCA flush afterwards (`0xf120410c`
  = `00111401`; the flush bit cleared itself) changed nothing.
- **Layout of `lineartile`** for 32-bit pixels: the image is cut into 4×4-pixel
  utiles (64 bytes each, pixels in raster order inside). The utiles are stored
  in Z order (Morton): utile 0 = (0,0), 1 = (4,0), 2 = (0,4), 3 = (4,4),
  4 = (8,0), and so on. All 256 words match this, and each input pixel
  appears exactly once.

### V3D GPU: first render job, a clear with no shaders (tested 2026-10-03)

`../bolt/raw/v3d/v3d_render_probe.s` (output next to it) runs a render job
with no binner and no shaders. The GPU clears one 64×64 tile to a colour in
its tile buffer, then stores the tile to RAM as a raster image. The GPU MMU
is off. Like the TFU job, this needs no page table.

Sources: the control lists follow the stock `libGLES_nexus.so`
(`create_cls_and_flush`, `glxx_hw_create_generic_tile_list`). The packet
names, sizes and fields come from its `v3d_cl_print_*` / `v3d_cl_pack_*`
functions and the opcode table `v3d_maybe_desc_cl_opcode`. The submission
follows `nexus.ko` (`BVC5_P_HardwareIssueRenderJob`,
`BVC5_P_HardwarePrepareForJob`).

Render control list (90 bytes, at `0x01001200` inside the program):

| Bytes | Packet |
|---|---|
| `79 00 40 00 40 00 40 00 0e` | tile rendering mode config, common: 1 render target, 64×64, 32 bpp, early-Z off, store render target 0 only |
| `79 02 08 1b f0 00 00 20 02` | colour config, render target 0: internal type 8-bit, output `rgba8` (27), memory format raster, address `0x02200000` |
| `79 01 00 00 00 00 00 30 02` | Z/stencil config: 32-bit float, address `0x02300000` (not stored) |
| `79 04 00 ff 00 ff 00 00 00` | clear colour: bytes 2–5 = word 0 = `0xff00ff00` |
| `79 06 00 00 40 00 00 00 00` | clear part 3: raster padded width 64 pixels |
| `79 03 00 00 00 80 3f 00 00` | Z/stencil clear values: stencil 0, depth 1.0 |
| `7c 00 00 00` | tile coordinates (0,0) |
| `1d 08 02 00 00 00 00` | store general: buffer none, clears on (a "dummy tile", as libGLES emits) |
| `13` | clear VCD cache |
| `7e 04` | tile list initial block size 64, chain |
| `7a 00 00 01 01 01 10 00 10` | supertile config: 1×1 tiles per supertile, frame 1×1 supertiles and 1×1 tiles, raster order |
| `14 <start> <end>` | generic tile list `0x01001280`–`0x01001283` (end exclusive) |
| `17 00 00` | supertile (0,0) |
| `0d` | end render |

Generic tile list (run for each tile): `7d` (implicit tile coordinates),
`18` (store subsample: render target 0 as set in the common config), `12`
(return). The CPU MMU is off, so the CPU's writes to the lists reach DRAM
directly. Each list is followed by 32 zero bytes, as libGLES does.

Submission:

| Write | Meaning |
|---|---|
| `0xf1208030` = `1` | `L2TCACTL`: flush the L2T cache |
| `0xf1208024` = `0f0f0f0f` | `SLCACTL`: flush the slice caches |
| `0xf1208058` = `7` | core `INT_CLR` |
| `0xf1208178` = `1` | `CT1QCFG` (Nexus writes `1` when no binner output is used) |
| `0xf1208164` = `01001200` | `CT1QBA`: control list start |
| `0xf120816c` = `0100125a` | `CT1QEA`: control list end; **this write starts the job** |

Result:

- Core `INT_STS` `0xf1208050` bit 0 (`FRDONE`, frame done) was set when the
  poll first looked (the printed 1.7 ms is UART time). `CLE_RFC` `0xf1208138`
  counted from 0 to 1.
  `CT1CA` = `CT1EA` = `0100125a`: the list was read to its end. `CT1CS` read
  `0` (stopped at the end, no error).
- The colour buffer held exactly 4096 words (64×64) of `ff00ff00`, in rows
  of 256 bytes. The 48 KB after it and the whole 64 KB depth buffer still
  read `deadbeef`: nothing else was written.
- The stored word equals the clear word.

**Byte order** (`v3d_order_probe.s`, output `v3d_order_output.txt`): the
same job, with the clear packet `79 04 00 11 22 33 44 00 00`. The clear word
is packet bytes 2–5, so the word was `0x33221100`. Every word of the tile
read `33221100`: the clear word's lowest byte goes to the lowest address,
with no reordering. Nothing outside the tile changed. Which byte is red was
found with a 565 output (next section): byte 0.

### V3D GPU: rendering straight into the framebuffer (tested 2026-10-03)

The same clear-only render job can store its tile as RGB565 into a surface
laid out like the framebuffer (1920×1080, pitch 3840). Three render control
list changes from the job above:

| Packet | Change |
|---|---|
| colour config `79 02 08 07 f0 <address>` | output format **7** (`bgr565`) instead of 27 (`rgba8`) |
| clear part 3 `79 06 00 00 80 07 00 00 00` | bytes 4–5: raster row stride **1920** pixels (was 64) |
| clear colour `79 04 <word> 00 00 00` | the clear word in bytes 2–5 |

The packet layouts come from libGLES `v3d_cl_tile_rendering_mode_cfg_indirect`
and `v3d_cl_rcfg_clear_colors`. libGLES's own tables
(`v3d_pixel_format_to_rt_format`) give `bgr565` internal type 8-bit and
32 bpp, the same as `rgba8`, so byte 2 of the colour config stays `08`. In
libGLES's name table, format 7 is `bgr565` and format 27 is `rgba8`.

**Scratch RAM first** (`../bolt/raw/v3d/v3d_fb_scr_probe.s`, outputs
`v3d_fb_scr_output_<word>.txt`). The tile went to `0x031dcb40` inside a 4 MB
area at `0x03000000` filled with `deadbeef`, at the place (928,508) has on
screen. Every run changed exactly 4096 halfwords: 64 rows of 64 pixels, with
3840 bytes from row to row. Pixel 64 of each row and the row below the tile
were unchanged. The depth buffer was unchanged.

| Clear word | Bytes 0–3 | Every pixel |
|---|---|---|
| `000000ff` | `ff 00 00 00` | `f800` |
| `0000ff00` | `00 ff 00 00` | `07e0` |
| `ff000000` | `00 00 00 ff` | `0000` |
| `ffffffff` | `ff ff ff ff` | `ffff` |
| `20408000` | `00 80 40 20` | `0408` |
| `f8f80000` | `00 00 f8 f8` | `001e` |

So the clear word is **byte 0 red, byte 1 green, byte 2 blue, byte 3
alpha**. Format 7 stores ordinary RGB565 with red in the top bits, the same
layout as the framebuffer. Alpha is dropped. The 8-bit values are rounded,
not cut: `f8` became blue 30 (248 × 31 / 255 = 30.2), `80` became green 32.

**Then the screen** (`v3d_fb_tv_probe.s`, output `v3d_fb_tv_output.txt`):
the same job with the clear word `000000ff` and the tile address `0x7dce8240`
= (928,508) in the framebuffer `0x7db0b700`. A 64×64 red square appeared in
the middle of the splash screen. Reading back: the tile was `f800`. The
pixels to its right and below it still held the splash background (`07e0`).

### V3D GPU: first triangle (tested 2026-10-03)

A binner job followed by a render job draws a flat-coloured triangle. There's
no vertex shader: the vertices are given in screen pixels (an "NV" shader
record). There's one fragment shader, copied from libGLES. Probe:
`../bolt/raw/v3d/v3d_tri_scr_probe.s` (scratch RAM), then
`v3d_tri_tv_probe.s` (screen).

**Sources.** Packet names and which list may hold them come from libGLES
`v3d_desc_cl_opcode`, `v3d_cl_instr_ok_in_bin` and
`v3d_cl_instr_ok_in_render`. `vertex_array_prims` (`24`) is allowed only in
the binning list, so the binner is needed. Packet layouts come from libGLES's
`v3d_cl_pack_*` functions. The draw packets come from `glxx_draw_rect`, and
the binning list start from `create_cls_and_flush`.

**Binner job** (Nexus `BVC5_P_HardwareIssueBinnerJob` order; register names
from Linux `v3d_regs.h`):

| Write | Meaning |
|---|---|
| `0xf1208030` = `1`, `0xf1208024` = `0f0f0f0f`, `0xf1208058` = `7` | cache flushes, `INT_CLR`, as for the render job |
| `0xf120830c` = `0` | `PTB_BPOS`: no overflow memory |
| `0xf1208170` = `04000000` | `CT0QMA`: tile allocation memory |
| `0xf1208174` = `00100000` | `CT0QMS`: its size, 1 MB |
| `0xf1208160` = binning list start | `CT0QBA` |
| `0xf1208168` = binning list end | `CT0QEA`: **starts the binner** |

Done: core `INT_STS` bit 1 (`FLDONE`) was set when the poll first looked.
`CT0CA` = `CT0EA` (the list was read to its end), `CLE_BFC` `0xf1208134` = 1,
and `PTB_BPCA` `0xf1208300` = `04003000` (12 KB of tile memory used).

**Binning control list:**

| Bytes | Packet |
|---|---|
| `78 12 00 10 04 01 10 00 00` | tile binning mode config: tile state array `0x04100000` (4 KB, zeroed) with auto-init, initial block 64 bytes, block 128 bytes; **width 1, height 1 tiles**; 1 render target, 32 bpp |
| `13`, `06` | clear VCD cache, start tile binning |
| `0e 00`, `5c 00000000` | wait transform feedback 0, occlusion query counter off (as libGLES) |
| `6b 0000 0000 4000 4000` | clip window (0,0) 64×64 |
| `6d 00000000 0000803f` | clip Z 0.0 to 1.0 |
| `60 03 70 00` | config bits: both faces, depth function "always" |
| `57 00000000` | colour write masks: all on |
| `6c` + 8 × `00` | viewport offset 0 |
| `49 44` | VCM cache size |
| `44` + record address \| 2 | `nv_shader`: the record below, 2 attribute arrays |
| `24 04 03000000 00000000` | `vertex_array_prims`: triangles, 3 vertices, first 0 |
| `04` | flush |

The tile counts in the config are **counts, not minus one**: libGLES computes
`((pixels - 1) >> shift) + 1`. A first run with `00 00 00` there read its
whole list, but `FLDONE` never came, `PTB_BPCA` moved by `0xaa000` and no
triangle was drawn (`v3d_tri_scr_output_run1_tiles0.txt`).

**NV shader record** (60 bytes, 32-byte aligned; values from libGLES
`v3d_create_nv_shader_record`, layout from `v3d_unpack_shadrec_gl_main` /
`_gl_attr`):

| Words | Value |
|---|---|
| 0–1 | `0`, `00010001` |
| 2 | address of 32 bytes of default attribute values: vec4(0,0,0,0), vec4(0,0,0,1.0) |
| 3, 4 | fragment shader code address, fragment shader uniforms address |
| 5–8 | `0` (no vertex or coordinate shader) |
| 9–11 | attribute 0: defaults address, `00000408`, `0` |
| 12–14 | attribute 1: vertex data address, `0000440b`, stride 12 |

Vertices are 12 bytes each: X and Y in pixels as fixed point (`<< 8`), then Z
(`0`). The triangle is (8,8), (56,8), (32,56).

**Fragment shader:** libGLES `v3d_clear_shader_color`, the path for 8-bit
render targets. It's 6 instructions:
`3c403186bb800000 3c003188b682d000 3c403186bb800000 3c203187b682d000`
`3c003186bb800000 3c003186bb800000`. Decoded with the V3D QPU field layout
(inferred), they are: load a uniform, write it to the tile buffer with the
next uniform as the write config, load a uniform, write it and end the
thread, two `nop`s. Uniforms: `R | G << 16` as half floats, `ffffffff`,
`B | A << 16`. Blue: `0`, `ffffffff`, `3c003c00`.

**Render job:** the RGB565 framebuffer-layout job from the previous section
(tile cleared to red), plus:
- `7b 00 00 00 04` in the render control list: tile list base, set 0,
  `0x04000000` (the tile allocation memory).
- `15 00` in the generic tile list after `7d`: branch to the binner's list
  for this tile.

**Result in scratch RAM:**
- Exactly 4096 halfwords changed: 2944 red `f800` and 1152 blue `001f`.
  1152 is the triangle's area (48 × 48 / 2).
- Per row, the blue pixels run from x = 8–55 on row 8, narrowing by one
  pixel on each side every two rows, down to 31–32 on row 54. Rows 0–7 and
  55–63 have none (`v3d_tri_scr_output_spans.txt`).

**On the screen:** the same jobs with the tile at (928,508) in the
framebuffer showed a red square with a blue triangle pointing down.
Reading the framebuffer back gave the same per-row spans
(`v3d_tri_tv_output.txt`).

### V3D GPU: our own first QPU program (tested 2026-10-03)

The triangle job from the previous section, with a fragment shader written
and encoded here instead of copied from libGLES
(`../bolt/raw/v3d/v3d_grad_scr_probe.s`, output `v3d_grad_scr_output.txt`).
The encoder/disassembler is `tools/re/qpu.py`. Its instruction layout
follows Mesa's `qpu_pack.c`. libGLES's own tables match Mesa's V3D 3.3
tables entry for entry: the signal table, the magic write-address names
and the small immediates (see `gpu.md`).

| Word | Instruction |
|---|---|
| `3c003180bb808000` | `fxcd r0` (pixel x as a float) |
| `55e03001bbe0c022` | `fycd r1 ; fmul r0, r0, 2^-6` (small immediate) |
| `55e03046bbe40022` | `fmul r1, r1, 2^-6` |
| `3c00318835808000` | `vfpack tlbu, r0, r1`: R and G as half floats, first tile-buffer write, with the next uniform (`ffffffff`) as its config |
| `030031c63c000000` | load immediate into `tlb`: `3c000000` = B 0.0, A 1.0 |
| `3c203186bb800000` | thread switch (end of program) |
| `3c003186bb800000` ×2 | `nop` (delay slots) |

The plain `fmul` (21) and `vfpack` (53) opcode numbers were built from
Mesa's pack rules; this run confirms them.

Result: the tile was cleared to black. Exactly the 1152 triangle pixels got
a colour, with red rising from left to right, green from top to bottom, and
blue 0. All 1152 values equal this model: R = x/64 and G = y/64, with x
and y the pixel's **integer** coordinates (no +0.5), each rounded to a half
float, then **rounded** to 8 bits and on to RGB565. Truncating instead gives
970 mismatches; using x + 0.5 gives 467.

So `fxcd`/`fycd` give the integer pixel coordinates as floats. A shader can
write the tile buffer with `tlbu` (config from the uniform stream) followed
by `tlb`, and the load immediate works as a full 32-bit move into a magic
register.

### M2MC 2D blitter: registers, reset and a solid fill (tested 2026-10-03)

The stock `nexus.ko` (`BGRC_` module) drives the M2MC at bus `0x209b0000`,
which is CPU **`0xf09b0000`**. The code names one core only.

**After BOLT** (`../bolt/raw/m2mc/m2mc_read_probe.s`, read-only, output next
to it), the block is powered and answers. Register meanings come from
Nexus's `BCHP_PWR` code and `BGRC_` functions:

| Register | Value | Meaning |
|---|---|---|
| `0xf04e04e8` | `00000007` | M2MC0 clocks: bit 0 `SYSTEM_M2MC0`, bits 1–2 `HW_M2MC0`, all on |
| `0xf04e0510` | `00000000` | M2MC0 clock mux select |
| `0xf0404628` | `00000000` | M2MC0 SRAM power: bit 0 = 0 is on, bit 1 = 0 is not busy |
| `+0x0c` / `+0x10` / `+0x14` / `+0x18` / `+0x1c` | `0` | list control, list status, first packet address, current packet address, blit status (names from Nexus's watchdog message) |
| `+0x60` / `+0x64` / `+0x68` / `+0x6c` | `02008020` / `20` / `0` / `00300000` | no name; Nexus overwrites them at reset |
| `+0xb0` | `00000002` | no name; Nexus writes a value that depends on the memory configuration |
| `+0x2f0`, `+0x1808` | `0` | no name |

**Reset** (`m2mc_fill_probe.s`, Nexus `BGRC_P_ResetDevice` order):

1. `+0x1808` = 1, then 0.
2. `+0x2f0` = 1.
3. Wait until `+0x1c` reads non-zero. It read `01000000` when the probe first
   looked (the printed 1.7 ms is UART time).
4. `+0x1808` = 1, then 0.
5. `+0x2f0` = `100`, `+0x60` = `82409024`, `+0x64` = `24`, `+0x6c` =
   `00100000`, `+0x68` = `111`. All read back as written; `+0x1c` read `0`
   again.

`+0xb0` was left at its value `2`.

**How work is given to it:** a list of packets in RAM. Write the first
packet's physical address to `+0x14`, then `6` to `+0x0c`. When it's done,
`+0x10` reads `2`, `+0x18` holds the last packet's address and `+0x1c` reads
`0`.

**The packet** (Nexus `BGRC_PACKET_P_WriteHwPkt`; 32-byte aligned, 492 bytes
for all groups). It starts with 2 words: the next packet's address (`1` =
last packet), then a group mask (`7ff0` = groups 14 to 4). After that come
the groups from bit 14 down. The words below are what this probe sent; they
were rebuilt from Nexus's packet code (`BGRC_PACKET_P_ProcessSwPacket`,
`…ResetState`, `…ProcSwPktFillBlit`):

| Group (bit) | Words | Sent |
|---|---|---|
| source feeder (14) | 19 | `1`, 12 × `0`, `00211e01`, `0`, `0`, `00211e01`, `ff`, `ffff0000`: a constant-colour source |
| destination feeder (13) | 12 | all `0` except word 9 = `1e01`: no destination read |
| output feeder (12) | 10 | `1`, `0`, `02400000` (address), `80` (pitch 128), `0`, `0`, `0`, `565`, `000b0500`, `1000`: RGB565 |
| blit (11) | 20 | `00024000`, then three rectangles (source, destination, output), each `y << 16 \| x` and `width << 16 \| height`, height rounded up to 16 after the first and third; last word `00020000` |
| scaler (10) | 13 | all `0` (off) |
| blend (9) | 4 | `201`, `205`, `0`, `fa`: colour = source × 1, alpha = source alpha × 1 |
| ROP (8) | 5 | `cc`, 4 × `0` |
| colour keys (7, 6) | 5 + 5 | `0` |
| filter coefficients (5) | 16 | `0`, `400`, `0`, `200`, repeated 4 times |
| colour matrix (4) | 12 | `0` |

RGB565 is described by the 3 words `565`, `000b0500`, `1000` (channel
widths, channel positions, flags), from Nexus's
`s_BGRC_PACKET_P_DevicePixelFormats` (format 1).

**Result:** a 16×8 red rectangle at (8,4) in a 64×32 RGB565 test surface at
`0x02400000`, pitch 128. The 64 KB around it was filled with `deadbeef`
first.

- The list had finished when the probe first looked: `+0x10` =
  `2`, `+0x18` = the packet address, `+0x1c` = `0`.
- Exactly the 128 pixels of the rectangle read `f800` (red). No other
  halfword in the surface or in the rest of the 64 KB changed.
- The first run sent the size as `height << 16 | width` (`00080010`) and got
  an 8-wide, 16-tall rectangle at the same corner
  (`m2mc_fill_output_first_run.txt`). So the size word is
  **`width << 16 | height`**, as `BGRC_PACKET_P_ProcSwPktFillBlit` builds it.
- The block stays powered after the probe, as BOLT left it.

### M2MC 2D blitter: copy and 2× scaling (tested 2026-10-03)

**Source surface:** a 16×8 RGB565 surface at `0x02400000`, pitch 32, with
pixel (x, y) = `y << 8 | x`. Each output area was filled with `deadbeef`
first.

**Source feeder words** (19 words; Nexus `BGRC_PACKET_P_ProcessSwPaket`,
source feeder case; the rest of the group is `0`):

| Word | Value |
|---|---|
| 0 | `1` (source on) |
| 1 | `0` (address bits 32 and up) |
| 2 | source address |
| 5 | pitch in bytes |
| 11, 12, 13 | `565`, `000b0500`, `00211001` (RGB565) |
| 16 | `00211e01` |

**Rectangles:** source and output rectangles are words 1–2 and 11–12 of
the blit group: position `y << 16 | x`, size `width << 16 | height`. Words
3, 10 and 13 are heights rounded up to 16.

**Scaler group** (13 words):

| Word | Value |
|---|---|
| 0 | `14` |
| 5, 6, 7, 8 | horizontal phase as 24 bits, step, phase as 32 bits, step |
| 9, 10, 11, 12 | the same four values for vertical |

- The step is source size `<< 20` / output size; 2× is `0x80000`.
- The phase is (step − `0x100000`) / 2; 2× is `-0x40000`, which is `00fc0000`
  as 24 bits and `fffc0000` as 32 bits.
- Bit 31 of blit word 0 turns the scaler on.

`m2mc_scale2_probe.s` (output `m2mc_scale2_output.txt`) runs one list of
three chained packets:

| Packet | What | Result |
|---|---|---|
| 1 | copy 16×8 → 16×8 at `0x02500000`, scaler off (blit word 0 = `0`) | **exact copy** |
| 2 | 16×8 → 32×16 at `0x02510000`, scaler word 0 = `04000014` (**bit 26: filter off**) | **every pixel exactly doubled** horizontally and vertically (nearest neighbour) |
| 3 | 16×8 → 32×16 at `0x02520000`, scaler word 0 = `14`, filter words `0, 400, 0, 200` (bilinear) | blended values between neighbours, e.g. `0040` between rows `0000` and `0100` |

In all three, nothing outside the output image changed.

**Several packets in one list:** word 0 of each packet holds the next
packet's address, and only the last packet has `1` there and bit 14 set in
blit word 0. After the list, `+0x18` held the last packet's address.

**Writing `6` to `+0x0c` again does not start a second list.** An earlier
run tried three separate lists (`m2mc_scale2_output_restart.txt`). After the
first list, `+0x14` = next packet and `+0x0c` = `6` left `+0x18` at the
first packet: the 100 ms wait timed out, and nothing new was drawn. Nexus
doesn't restart a list either: it links new packets onto the last one and
writes `3` to `+0x0c`.

**Filter words `0, 400, 0, 400`** (an earlier run,
`m2mc_scale_probe.s` / `m2mc_scale_output.txt`) did not give point sampling.
The 32×16 output came out about twice as bright: blue reached `1f` where the
source's highest blue is `0f`. To scale without filtering, use bit 26 of
scaler word 0.

### M2MC 2D blitter: 320×200 → 1600×1000 onto the TV (tested 2026-10-03)

`../bolt/raw/m2mc/m2mc_tv_probe.s` (output `m2mc_tv_output.txt`) scales a
320×200 RGB565 test image 5× with the filter off (nearest neighbour).

**Striping:** with widths over 128 pixels, the blitter works in vertical
stripes. The stripe settings come from Nexus `BGRC_PACKET_P_SetScaler`,
worked out by hand from the disassembly. They go in blit words 14–19:

| Word | Value | Meaning |
|---|---|---|
| 14, 15 | `0077fff8` | input stripe width, = step × output stripe >> 4 |
| 16 | `600` | output stripe width, = ((128 − 2 × overlap) << 20) / step, rounded down to even |
| 17, 18 | `4` | overlap |
| 19 | `00030000` | bit 16: striping on (bit 17 is set at reset) |

The other words:
- Scaler word 0 = `04000014`.
- Step `0x33333` both ways.
- Phase `00f99999` (24 bits) and `fff99999` (32 bits).
- Source size `320 << 16 | 200`, output size `1600 << 16 | 1000`, rounded
  heights 208 and 1008.

**Run 1, scratch RAM:** the output went into a framebuffer-sized area at
`0x03000000` (pitch 3840, filled with `deadbeef`), at the same place as on
screen. Every one of the 1,600,000 output pixels equals source pixel
(x/5, y/5). Exactly 1,600,000 halfwords of the 4 MB area changed, so nothing
outside the rectangle was written.

**Run 2, the screen:** the CPU cleared the framebuffer (`0x7db0b700`,
1920×1080, pitch 3840) to black. A second M2MC reset followed: after a reset,
`+0x0c` = `6` starts a new list again. Then the blitter drew the same packet
with the output at (160,40) = `0x7db31040`. The picture showed on the TV,
centred: the white border, the colour steps and the blue checkerboard, each
source pixel as a sharp 5×5 block.

Each list had finished when the probe first looked. The screen list's real
time was measured later: 1.65 ms (next section).

The test image's red channel is `(x >> 3) & 31`. It wraps to 0 at source
column 256, which made a hard edge 320 pixels from the right of the picture.
That edge is in the source image itself.

### M2MC 2D blitter: palette-8 lookup and timing (tested 2026-10-03)

The M2MC converts an 8-bit indexed image to RGB565 through a 256-entry
palette, and can scale it in the same packet. Probes and outputs are in
`../bolt/raw/m2mc/` (`m2mc_pal*`).

**Palette-8 source format.** Nexus `BGRC_PACKET_P_ConvertPixelFormat` maps
BPXL `0x12e40008` to BM2MC format **33**. Its table entry
(`s_BGRC_PACKET_P_DevicePixelFormats`) is `00030008, 0, 00001c00`. The source
feeder words 11–13 are then:

| Word | Value |
|---|---|
| 11 | `00030008` |
| 12 | `0` |
| 13 | `00211c01` (table word 2, ORed with `00210000` and bit 0, as Nexus does) |

**Palette.** Group bit 1 of the packet (mask `7ff2` instead of `7ff0`): 2
words, the palette's address, then `0`. They come after the colour matrix
group, at the end of the packet. Each palette entry is a 32-bit word
**`AARRGGBB`**.

**Test** (`m2mc_pal4_probe.s`):
- Source: a 16×8 index image, index = y·16 + x.
- Palette: entry i = `ff00a030 | i << 16`, so red = i.
- Output: 16×8 RGB565.

Every pixel came out as `(i >> 3) << 11 | 0506`: red = i, green = `a0`,
blue = `30`. Nothing outside the image changed. The 8-bit to 5- or 6-bit
step cuts off the low bits (index 7 gave red 0, index 8 gave red 1).

**Where the palette goes.** After the list, the M2MC's registers
`0xf09b0400`–`0xf09b05fc` held palette entries 0–127, in order
(`m2mc_pal_probe.s` read `+0x000`–`+0x5fc` only). So the block copies the
palette from RAM into its own registers. The same read-back shows that the
packet's groups land in registers starting at `0xf09b0104`. The source feeder
address is at `+0x10c`, the output feeder at `+0x180`, the blit group at
`+0x1a8` and the blend group at `+0x230`. That is `0x104` plus Nexus's
`s_BGRC_PACKET_P_DeviceRegisterOffsets`, and the palette group is at
`0x104 + 0x2fc` = `+0x400`. Reads of `+0xb4`–`+0xfc`, `+0x1f8` and
`+0x2f4`–`+0x3fc` aborted (synchronous external abort).

**Format 27 is not palette-8.** BPXL `0x01390008` maps to format 27
(`00058000, 0, 00000e00`). With it, the palette was still loaded, but the
colour came from the source's constant colour (source feeder word 18). With
the constant `ff123456`, every pixel was `11aa` (`m2mc_pal2_probe.s`). From
its BPXL code, format 27 is probably A8 (alpha only); that is inferred, not
tested.

Word 13 changes (`m2mc_pal3_probe.s`, format 27, constant `ff123456`):

| Word 13 | Output | R, G, B (8-bit) |
|---|---|---|
| `00210e01` (Nexus) | `11aa` | `12, 34, 56`: the constant |
| `00200e01` (bit 16 cleared) | `81aa` | `80, 34, 56` |
| `00010e01` (bit 21 cleared) | `1410` | `12, 80, 80` |
| `00000e01` (both cleared) | `8410` | `80, 80, 80` |

With `00210001` (bits 9–11 cleared) the output was `0000`. Read together
with the format table, bits 9–12 of word 13 probably switch channels 0–3
off: `0e00` leaves only the alpha channel, `1c00` (palette-8) only
channel 0, which holds the index. This is inferred.

**320×200 palette image → 1600×1000 on the TV** (`m2mc_paltv_probe.s`).
These are the `m2mc_tv_probe.s` packets with the palette-8 source (pitch
320) and a palette.
- In scratch RAM, every output pixel equalled the CPU's own palette lookup of
  source pixel (x/5, y/5). Exactly 1,600,000 halfwords changed.
- On the screen, the picture showed centred.

**Time.** The probe read `CNTPCT_EL0` (27 MHz) right before writing `6` to
`+0x0c`, then polled until `+0x18` = the packet, `+0x10` = `2` and `+0x1c` =
`0`, with no UART output in between. The whole lookup + 5× scale +
framebuffer write took **44,494 and 44,513 ticks = 1.65 ms** (two runs:
`m2mc_paltv_output.txt`, `m2mc_paltv_output_run2.txt`).
That is about 0.97 billion output pixels per second.

**The "1.7 ms" in earlier probe outputs is UART time.** The probes' `poll`
prints `"  poll ok after us: "` before it reads the end time. These 20
characters take 20 × 86.8 µs = 1,736 µs at 115,200 baud. That is exactly the
`000006c8` every poll printed. Those polls only show that the job was
already done at the first read.

### M2MC 2D blitter: new lists without a reset (tested 2026-10-03)

Nexus `BGRC_PACKET_P_ProcessSwPktFifo` gives the blitter new work while it
keeps running. It writes the first new packet's address into word 0 of the
last packet it sent before, then writes **`3`** to `+0x0c`. Only the very
first batch uses `+0x14` and `6`.

`../bolt/raw/m2mc/m2mc_cont_probe.s` (output `m2mc_cont_output.txt`), with
one M2MC reset at the start and three packets. Each packet is the 16×8
palette-8 → RGB565 packet from the palette section, with word 0 = `1` and
bit 14 set in blit word 0.

| Step | What the CPU does | Output |
|---|---|---|
| 1 | `+0x14` = A, `+0x0c` = `6`. A uses palette 1 (red = i) | `0506 … 7d06` at `0x02500000` |
| 2 | A's word 0 = B in RAM, `dsb`, `+0x0c` = `3`. B uses palette 2 (blue = i) | `0000, 0001, … 000f` at `0x02510000` |
| 3 | B's word 0 = C, `dsb`, `+0x0c` = `3`. C uses palette 1 | `0506 … 7d06` at `0x02520000` |

- After each step, `+0x18` held that step's packet, `+0x10` read `2` and
  `+0x1c` read `0`.
- All three outputs were exact, and nothing outside them changed.
- Each packet loaded its own palette.
- `+0x0c` read `6` after the start and `2` after each `3`.
- `+0x14` kept the first packet's address.

So after one reset, each new frame's packet can be linked onto the previous
one and started with `+0x0c` = `3`.

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

### The whole block (tested 2026-10-03)

Every word of `0xf0400000–0x7ff`, read by `../bolt/raw/sys/addrmap1_probe.s`
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
- Using the V3D GPU further (its MMU, binner jobs, shaders), and the M2MC 2D blitter further (copies, scaling, the framebuffer).
- The display pipeline blocks' names (`0xf0645000`, `0xf0650000`, `0xf06e0000…`).
- The other Nexus multimedia blocks (video decoder, audio DSP, transport) are
  listed by the memory controller but have no known address. (The M2MC's is
  `0xf09b0000`, above.)
- Powering up PCIe (Wi-Fi).
