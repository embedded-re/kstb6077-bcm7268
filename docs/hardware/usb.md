# USB

The BCM7268 has one USB block at `0xf0b00000` with five host controllers
(EHCI ×2, OHCI ×2, xHCI) and a device controller (BDC, disabled in the
DTB). BOLT sets up the PHY and clocks at boot, and its `go` resets the
controllers but leaves that set-up in place. A bare-metal program can then
drive the controllers with the standard specs (EHCI 1.0, OHCI 1.0a,
xHCI 1.0), with no Broadcom-specific code. That last point is tested for
OHCI up to port enable; transfers are not tested yet.

Tested on the modified box on 2026-10-02. Probes are in `../bolt/raw/usb/`.

## Blocks (stock DTB `boot/stock_dtb.dts`, capability registers read)

| Base | DTB node | Size | Capability registers | Meaning |
|---|---|---|---|---|
| `0xf0b00200` | `usb-phy@f0b00200` (`brcm,usb-phy`, `ipp`/`ioc` = 1, `has_xhci`) | `0x100` | not read | Broadcom USB control / PHY block |
| `0xf0b00300` | `ehci_v2` (IRQ SPI `0x5a`) | `0xa8` | `01000010` `00001111` `0000a027` | EHCI0: v1.0, op regs at +0x10, 1 port, 1 companion, 64-bit |
| `0xf0b00400` | `ohci_v2` (SPI `0x5b`) | `0x58` | `00000110`, RhDescA `02000901` | OHCI0: v1.0, 1 port, per-port power, POTPGT 2 (4 ms) |
| `0xf0b00500` | `ehci_v2` (SPI `0x5e`) | `0xa8` | same as EHCI0 | EHCI1 |
| `0xf0b00600` | `ohci_v2` (SPI `0x5f`) | `0x58` | same as OHCI0 | OHCI1 |
| `0xf0b01000` | `xhci_v2` (SPI `0x5c`) | `0x1000` | `01000020` `0200021f` `14000052` `07ff0002` `0250f17d`, DBOFF `8c0`, RTSOFF `4a0` | xHCI v1.0, op regs at +0x20, 2 ports, 31 slots, 64-byte contexts, 2 scratchpad pages, ext. caps at +0x940 |
| `0xf0b02000` | `bdc_v2` (`status = "disabled"`) | `0xfc4` | not read | device-mode controller |

GIC IDs = SPI + 32: EHCI0 122, OHCI0 123, xHCI 124, EHCI1 126, OHCI1 127
(matches `../stock-firmware.md`). Clocks (DTB): `sw_usb20` (`usb0_freerun`,
`usb0_gisb`, `sys_108/54/scb_usb20`) and `sw_usb30`, gated in
`0xf04e049c–0xf04e04bc`.

## Which port is which (tested)

| Pair | Wired to | Evidence |
|---|---|---|
| **EHCI0 + OHCI0** | internal **Bluetooth** adapter `0a5c:2045` (full speed) | at `BOLT>`: EHCI0 PORTSC `00003000` (owner = companion), OHCI0 port `00000103` (connected, enabled) |
| **EHCI1 + OHCI1** | the **USB-A port** | with a low-speed USB keyboard (`0461:0010`) plugged in: OHCI1 port `00000303` (connected, enabled, low speed); EHCI1 handed it over (`00003000`) |
| xHCI ports 1–2 | unknown | `000002a0` (powered, RxDetect, empty) with and without the keyboard. A SanDisk stick `0781:558a` on the USB-A port came up as `New high speed device connected to bus 1` (EHCI1), not on the xHCI. Caution: If that stick is USB 3 (not checked), the port's USB 3 lines don't reach the xHCI, or BOLT doesn't use them |

Low- and full-speed devices (keyboards, mice, the BT adapter) are handled by
the OHCI. High-speed devices (USB 2 sticks) are handled by the EHCI. EHCI's
CONFIGFLAG decides who owns a port at first; after reset, it is 0 and every
port belongs to the OHCI.

BOLT's bus numbers in `show usb` (inferred from its reset messages; bus 1 =
EHCI1 confirmed by the high-speed stick): 0 = EHCI0, 1 = EHCI1, 2 = OHCI0 (BT), 3 = OHCI1 (USB-A low/full speed), 4 = xHCI.
BOLT's own keyboard driver picks up a keyboard (`USBHID: Keyboard Configured.`).

## What BOLT does (from its code, `../bolt/bolt.md` §11)

- **Start-up** (`usb init`, `0x0703a4c2`): reads the DTB nodes (`usb-phy@`,
  `ipp`, `ioc`, `has_xhci`, `ehci@`/`ohci@`/`xhci@`/`bdc@`) into a table at
  `0x0706b010` (bases + types: EHCI `0x100`, OHCI `0`, xHCI `0x200`, BDC
  `0x301`), then creates one driver per controller. Env vars: `XHCIOFF`,
  `EHCIOFF`, `OHCIOFF`, `BDCOFF` skip a controller; `USBDBG` prints
  `USB @ 0x%x: IPP polarity is %s, IOC is %stive polarity` and the controller
  addresses; `USBIPP`/`USBIOC` override the DTB values. `usb init -o/-oo/-u/-uu`
  print OHCI/USBD debug messages.
- **Before `go`/`boot`**: the launch path (`0x0701cdec`) closes the network, then
  calls `0x07011d5e`, which calls `0x0703a7a0`. That one calls every
  controller driver's stop routine (function pointer at `ops + 0x2c`). `usb
  exit` ends with the same call.

## State after `go -64` (tested: `usb_state_probe.s`)

| Register | At `BOLT>` | After `go -64` |
|---|---|---|
| EHCI0/1 USBCMD | `00080b09` (run) | `00080b00` (stopped: the reset default) |
| EHCI0/1 USBSTS | `00000008` | `00001000` (HCHalted) |
| EHCI0/1 CONFIGFLAG | `00000001` | `00000000` |
| EHCI0/1 PORTSC | `3000` / `1000` | `2000` / `2000` (port power off, owner = companion) |
| OHCI0/1 HcControl | `000000bf` (Operational, lists on) | `00000000` (UsbReset) |
| OHCI0/1 port 1 | `103` / `100` (keyboard out) | `0` / `0` (unpowered) |
| xHCI USBCMD / USBSTS | `1` / `0` | `0` / `1` (halted) |
| xHCI PORTSC 1/2 | `2a0` | `2b0` (powered, bit 4 PR set) |

Every register still reads without an abort, so the block stays clocked.
The controllers are back at their reset values, so a driver starts as it
would after its own reset.

## OHCI1 from bare metal (tested: `ohci_port_probe.s`, keyboard plugged in)

Steps, OHCI 1.0a spec, offsets from `0xf0b00600`:

| Step | Write | Read back |
|---|---|---|
| 1. controller reset | HcCommandStatus `+0x08` = 1 | `0` after 1 ms (done) |
| 2. HCCA (256 bytes, 256-aligned, zeroed) | HcHCCA `+0x18` = `0x11000000` | |
| 3. frame timing, Operational | HcFmInterval `+0x34` = `27782edf`, HcPeriodicStart `+0x40` = `2a2f`, HcControl `+0x04` = `80` | HcControl `80`; HcFmNumber `+0x3c` `08` → `13` in 10 ms (1 frame/ms) |
| 4. power | HcRhStatus `+0x50` = `10000` (LPSC), HcRhPortStatus1 `+0x54` = `100` (SetPortPower), wait 20 ms | |
| 5. wait for connect | poll `+0x54` bit 0 | `00010301`: connected, powered, low speed, connect changed |
| 6. port reset (after 100 ms debounce) | `+0x54` = `10` (SetPortReset), poll bit 20 | `00110303`: **enabled**, reset done |

Nothing Broadcom-specific was needed (no PHY or `0xf0b00200` writes): the
PHY and clocks from BOLT's start-up survive `go -64`.

## Open

- Control transfers on OHCI1 (EDs/TDs in RAM, `GET_DESCRIPTOR`, `SET_ADDRESS`,
  `SET_CONFIGURATION`), then HID boot-protocol keyboard reports from an
  interrupt endpoint (8-byte reports). With the MMU off, RAM is uncached, so
  DMA needs no cache maintenance. With the MMU map from `display.md`, the
  ED/TD/buffer area must be mapped non-cacheable, or cleaned/invalidated.
- Which connector, if any, the xHCI ports serve (a USB stick test).
- EHCI1 with a high-speed device (USB stick).
- The `0xf0b00200` control block (not read: no register names yet).
- Would a cold start (no BOLT `usb init`, e.g. `USB` skipped) leave the PHY
  off? Not needed while we boot through BOLT.
