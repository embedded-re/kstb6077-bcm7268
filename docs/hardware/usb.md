# USB

The BCM7268 has one USB block at `0xf0b00000` with five host controllers
(EHCI ×2, OHCI ×2, xHCI) and a device controller (BDC, disabled in the
DTB). BOLT sets up the PHY and clocks at boot, and its `go` resets the
controllers but leaves that set-up in place. A bare-metal program can then
drive the controllers with the standard specs (EHCI 1.0, OHCI 1.0a,
xHCI 1.0), with no Broadcom-specific code. That is tested for OHCI1: control
transfers, enumeration and a HID keyboard's interrupt endpoint.

Tested on the modified box on 2026-10-02. Probes are in [`../evidence/usb/`](../evidence/usb/).

## Summary

| | |
|---|---|
| Controllers | EHCI0 `0xf0b00300`, OHCI0 `0xf0b00400`, EHCI1 `0xf0b00500`, OHCI1 `0xf0b00600`, xHCI `0xf0b01000`; device controller (BDC) `0xf0b02000`, held in reset |
| Control / PHY | `0xf0b00200`, set up by BOLT; survives `go` |
| Ports | USB-A = EHCI1 + OHCI1; internal Bluetooth = EHCI0 + OHCI0; xHCI ports unknown |
| State after `go -64` | every controller reset and stopped, still clocked |
| Tested | OHCI1 from bare metal: port enable, control transfers, enumeration of a keyboard and a USB stick, keyboard reports from an interrupt endpoint (kmon `usb`) |
| Not tested | bulk transfers, EHCI, xHCI, interrupts from the USB controllers |

## Blocks (stock DTB [`boot/stock_dtb.dts`](../../boot/stock_dtb.dts), capability registers read)

| Base | DTB node | Size | Capability registers | Meaning |
|---|---|---|---|---|
| `0xf0b00200` | `usb-phy@f0b00200` (`brcm,usb-phy`, `ipp`/`ioc` = 1, `has_xhci`) | `0x100` | read in full ([`peripherals.md`](peripherals.md), "PCIe and the USB device controller") | Broadcom USB control / PHY block |
| `0xf0b00300` | `ehci_v2` (IRQ SPI `0x5a`) | `0xa8` | `01000010` `00001111` `0000a027` | EHCI0: v1.0, op regs at +0x10, 1 port, 1 companion, 64-bit |
| `0xf0b00400` | `ohci_v2` (SPI `0x5b`) | `0x58` | `00000110`, RhDescA `02000901` | OHCI0: v1.0, 1 port, per-port power, POTPGT 2 (4 ms) |
| `0xf0b00500` | `ehci_v2` (SPI `0x5e`) | `0xa8` | same as EHCI0 | EHCI1 |
| `0xf0b00600` | `ohci_v2` (SPI `0x5f`) | `0x58` | same as OHCI0 | OHCI1 |
| `0xf0b01000` | `xhci_v2` (SPI `0x5c`) | `0x1000` | `01000020` `0200021f` `14000052` `07ff0002` `0250f17d`, DBOFF `8c0`, RTSOFF `4a0` | xHCI v1.0, op regs at +0x20, 2 ports, 31 slots, 64-byte contexts, 2 scratchpad pages, ext. caps at +0x940 |
| `0xf0b02000` | `bdc_v2` (`status = "disabled"`) | `0xfc4` | not read | device-mode controller |

GIC IDs = SPI + 32: EHCI0 122, OHCI0 123, xHCI 124, EHCI1 126, OHCI1 127
(matches [`../stock-firmware.md`](../stock-firmware.md)). Clocks (DTB): `sw_usb20` (`usb0_freerun`,
`usb0_gisb`, `sys_108/54/scb_usb20`) and `sw_usb30`, gated in
`0xf04e049c–0xf04e04bc`.

## Which port is which (tested)

| Pair | Wired to | Evidence |
|---|---|---|
| **EHCI0 + OHCI0** | internal **Bluetooth** adapter `0a5c:2045` (full speed) | at `BOLT>`: EHCI0 PORTSC `00003000` (owner = companion), OHCI0 port `00000103` (connected, enabled) |
| **EHCI1 + OHCI1** | the **USB-A port** | with a low-speed USB keyboard (`0461:0010`) plugged in: OHCI1 port `00000303` (connected, enabled, low speed); EHCI1 handed it over (`00003000`) |
| xHCI ports 1–2 | unknown | `000002a0` (powered, RxDetect, empty) with and without the keyboard. A SanDisk stick `0781:558a` on the USB-A port came up as `New high speed device connected to bus 1` (EHCI1), not on the xHCI. Caution: If that stick is USB 3 (not checked), the port's USB 3 lines don't reach the xHCI, or BOLT doesn't use them |

Confirmed under stock Android (2026-10-04, [`interrupts.md`](interrupts.md)): plugging the
keyboard into the USB-A port fired EHCI1 (ID 126) first, then OHCI1 (ID
127): EHCI1 sees the connection and hands the low-speed device to its
companion OHCI1. Unplugging fired only OHCI1.

xHCI root hubs under the stock 4.9 kernel: the USB 2 side reports **no
ports** (`hub 1-0:1.0: config failed, hub doesn't have any ports!`), the
USB 3 side **2 ports** (`hub 2-0:1.0: 2 ports detected`). So the xHCI only
has USB 3 (SuperSpeed) ports; USB 2 traffic on any connector goes through
EHCI/OHCI.

Low- and full-speed devices (keyboards, mice, the BT adapter) are handled by
the OHCI. High-speed devices (USB 2 sticks) are handled by the EHCI. EHCI's
CONFIGFLAG decides who owns a port at first; after reset, it is 0 and every
port belongs to the OHCI.

BOLT's bus numbers in `show usb` (inferred from its reset messages; bus 1 =
EHCI1 confirmed by the high-speed stick): 0 = EHCI0, 1 = EHCI1, 2 = OHCI0 (BT), 3 = OHCI1 (USB-A low/full speed), 4 = xHCI.
BOLT's own keyboard driver picks up a keyboard (`USBHID: Keyboard Configured.`).

## What BOLT does (from its code, [`../bolt/bolt.md`](../bolt/bolt.md) §11)

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

## State after `go -64` (tested: [`usb_state_probe.s`](../evidence/usb/usb_state_probe.s))

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

## OHCI1 from bare metal (tested: [`ohci_port_probe.s`](../evidence/usb/ohci_port_probe.s), keyboard plugged in)

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

## Transfers and a keyboard on OHCI1 (tested)

Tested on 2026-10-15 on the modified box with kmon's `usb` command
([`../../mon/usb.c`](../../mon/usb.c)): polled, no interrupts, MMU and caches on. The EDs,
TDs, HCCA and buffers sit in a 2 MB block at `0x00600000` that kmon maps
Normal non-cacheable, so no cache maintenance is needed.

Bring-up as in the table above (reset, HCCA, frame timing, Operational, port
power, port reset), then the standard OHCI 1.0a lists:

| What | How |
|---|---|
| Control transfer | one ED on the control list (`HcControlHeadED` `+0x20`), a SETUP TD (DATA0), an optional data TD (DATA1, buffer rounding on), a status TD (DATA1, opposite direction), an empty tail TD. Start: `HcControl` CLE (bit 4), `HcCommandStatus` CLF (bit 1). Done when the ED's HeadP reaches the tail; each TD's condition code (bits 28–31) is `0` |
| List pointers | written only with CLE off (the spec's rule) |
| Interrupt endpoint | one ED with direction IN hung from all 32 HCCA interrupt slots (polled every frame), `HcControl` PLE (bit 2). Each finished TD becomes the new tail and the old tail gets the next request; the data toggle is carried in the ED |

Enumeration that worked: `GET_DESCRIPTOR` (device, 8 bytes) at address 0,
`SET_ADDRESS` 1, `GET_DESCRIPTOR` (device, 18), configuration (9 bytes, then
`wTotalLength`), `SET_CONFIGURATION`, then for the keyboard `SET_PROTOCOL` 0
(boot) and `SET_IDLE` 0. No request was STALLed.

| Device | Port status after reset | Descriptors |
|---|---|---|
| keyboard `0461:0010` | `00130303`: connected, enabled, **low speed** | USB 1.10, ep0 8 bytes; 59-byte configuration, 2 HID interfaces: 0 = boot keyboard (03/01/01), endpoint `81` interrupt 8 bytes / 10 ms; 1 = 03/00/00, endpoint `82` interrupt 8 bytes / 10 ms |
| USB stick `0781:558a` | `00110103`: connected, enabled, **full speed** | USB 2.10, ep0 64 bytes; mass storage (08/06/50 = SCSI, bulk-only), endpoints `81` IN and `02` OUT, bulk 64 bytes |

The stick is a high-speed device (BOLT puts it on EHCI1). With EHCI1 not
running (CONFIGFLAG 0), the port belongs to OHCI1 and the stick fell back to
full speed there.

Keyboard reports (8 bytes, boot protocol): byte 0 = modifiers, bytes 2–7 =
up to six usage codes of the keys held down. Seen on this keyboard:

| Byte 0 bit | Key |
|---|---|
| 0 / 1 / 2 / 3 | left Ctrl / Shift / Alt / GUI |
| 4 / 5 / 6 / 7 | right Ctrl / Shift / Alt / GUI |

- One report per change (`SET_IDLE` 0), none while nothing changes.
- Usages seen: letters `04`–`1d`, digits `1e`–`27`, Enter `28`, Esc `29`,
  Backspace `2a`, Tab `2b`, space `2c`, punctuation `2d`–`38` (including
  `32`, the extra ISO key), CapsLock `39`, F1–F12 `3a`–`45`, `46`–`4e`
  (Print Screen … Page Down), arrows `4f`–`52`, keypad `53`–`63`, Menu `65`.
- Several keys at once that the keyboard can't resolve give `01` in all six
  key bytes (HID ErrorRollOver).
- Not tested: Shift or Ctrl together with a letter (only alone), the
  keyboard LEDs (`SET_REPORT`).

## Open questions

- Bulk transfers (the USB stick) and EHCI1 for high-speed devices.
- The OHCI1 interrupt (ID 127) instead of polling.
- Which connector, if any, the xHCI ports serve (a USB stick test).
- EHCI1 with a high-speed device (USB stick).
- The `0xf0b00200` control block: read in full ([`peripherals.md`](peripherals.md)), but
  only `USB_PM` (`+0x34`) and `USB_DEVICE_CTL1` (`+0x90`) have names.
- Would a cold start (no BOLT `usb init`, e.g. `USB` skipped) leave the PHY
  off? Not needed while programs start through BOLT.
