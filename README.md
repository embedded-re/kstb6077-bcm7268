# KaonMedia KSTB6077 — Bare-Metal Reverse Engineering

**Author:** embedded-re
**Repo:** github.com/embedded-re/KAON-KSTB6077

---

## Status

The Linux port is **abandoned**. It was given up on **because of IRQ interrupts** —
interrupt handling under the vendor kernel (4.1.45) and Broadcom DTB
(`brcm,brahma-b53`, L2 intc chain) never came together, so ownership moved to
bare metal where there is full control over the CPU, the GIC, and interrupt
routing with no PSCI / DT indirection in the way.

Bare-metal assembly is now the **primary work**. Current milestone: a UART
monitor in `assembly/boot.s` running from BOLT via raw `go` jump.

## Why bare metal

- Kernel/DTS interrupt path (GIC + `brcm,l2-intc` set-up by the vendor DT boot)
  was a persistent dead end
- Program the GIC directly instead — no `enable-method`, no PSCI
- See `LINUX_PORT.md` for the abandoned Alpine migration history

## Files

| Path | Notes |
|---|---|
| `rescue.txt` | BOLT session dumps — printenv, help, dt show, boot logs. Key reference |
| `boot/original_dtb.dts` | **Original vendor DTB as dumped by BOLT `dt show`** (extracted from `rescue.txt`) — the one to trust |
| `boot/dtb.dtb` / `Decompiled_dtb.dts` | **Patched** variant (injected bootargs, renamed USB nodes) — NOT the original |
| `boot/sysinit.txt` | BOLT autoboot script (USB stick) |
| `assembly/boot.s` | Bare-metal UART monitor (BCM7268, UART @ 0xf040c000) |
| `assembly/build.sh` | Builds the monitor (linked/loaded at `0x01000000`) |
| `assembly/bootstrap.elf` / `.bin` | Built monitor images — copy `bootstrap.bin` to the USB stick as `boot.bin`; BOLT: `load -loader=raw -addr=0x01000000 usbdisk0:boot.bin` then `go 0x01000000` |
| `docs/` | PCB layout + Android recovery screenshots |

## Interrupt hardware (from the DTS)

| Device | Address | Notes |
|---|---|---|
| GIC (`arm,cortex-a15-gic`) | dist `0xffd01000`, cpu `0xffd02000` | root interrupt-parent, 3 cells — program directly |
| `brcm,l2-intc` | `0xf0403000` (sys), `0xf0201000`, `0xf0201a00` (hif-spi), `0xf0410640`, `0xf04d1200`, `0xf040a600` | soc-level interrupt muxes |
| GPIO intc | `0xf040a500` / `0xf0419c80` | `brcm,brcmstb-gpio` banks |
| CPUs | `enable-method "brcm,brahma-b53"` | bypassed in bare metal — GIC programmed directly |

SoC: Broadcom **BCM7268b0** (4x Cortex-A53 "B53"). System console UART
**0xf040c000** (ttyS0, 115200 8N1). RAM 2 GB.

## ⚠️ BOLT MMU: unmapped addresses hang the board

BOLT runs with the MMU on, and `go` leaves BOLT's page tables in place, so the
bare-metal monitor inherits them. An address that isn't mapped in them aborts,
and the board has to be power-cycled.

- **GIC (`0xffd01000` / `0xffd02000`) is NOT mapped.** `d -w 0xffd01000 4`
  gave `CPU exception: ABT`, `dfsr 00000005` (translation fault), `dfar ffd01000`.
  The address matches the DTB; BOLT simply doesn't map it.
- BOLT runs in SVC mode with IRQ and FIQ disabled (`cpsr 800001d3`).
- Before any GIC or interrupt work in bare metal, the monitor has to turn the
  MMU off or install its own page tables that map `0xffd00000`.
- Known-safe (already accessed): `0xf04xxxxx` peripherals used in this README,
  and AON SRAM.

## GPIO map (found by experiment)

The DTB does not describe LEDs/IR — on Broadcom STBs those belong to Nexus, not
the device tree. Pins below were found by poking registers from BOLT with
`d -w` / `e -w`.

Register layout (`brcm,brcmstb-gpio`, see Linux `drivers/gpio/gpio-brcmstb.c`):
banks are 0x20 bytes apart; per bank `+0x00` ODEN (open-drain), `+0x04` DATA,
`+0x08` IODIR (1 = input, 0 = output), `+0x14` MASK, `+0x1c` STAT.

| Controller | Base | Banks (pins) |
|---|---|---|
| AON GPIO | `0xf0419c80` | 28, 6 |
| Main GPIO | `0xf040a500` | 32, 32, 19, 2 |

| Function | Pin | Notes |
|---|---|---|
| LED1 green (power) | AON bank 0 bit 18 | open-drain, active-low, set up as output by BOLT |
| LED1 red | AON bank 0 bit 17 | open-drain, active-low, set up as output by BOLT; green + red = orange |
| LED3 blue | AON bank 0 bit 16 | active-low, **input after BOLT** — set ODEN, set DATA (off), then clear IODIR |
| SW1 (front standby button) | AON bank 0 bit 14 | input, active-low (reads 1 at rest) |
| SW4 (reset/recovery button) | AON bank 0 bit 7 | input, active-low (0 = pressed) |
| WiFi power (`vreg-wifi-pwr`) | AON bank 0 bit 21 | from DTB; input after BOLT |
| WLAN power (`vreg-wlan-pwr`) | AON bank 0 bit 26 | from DTB; input after BOLT |

BOLT state after boot: AON bank 0 ODEN `00060000`, DATA `001340b8`,
IODIR `0ff9ffff` (only bits 17/18 are outputs). Every other GPIO is an input.
Use read-modify-write; do not turn unknown inputs into outputs.

Finding inputs: STAT (`+0x1c`) latches pin changes even with interrupts
masked, and is write-1-to-clear. Clear it (`e -w 0xf0419c9c <current value>`),
press the button once, dump — the newly set bit is the pin.

IR: `IR1` on the PCB is a receiver only — there is no IR transmitter on the
board, although the SoC has an IR blaster block (`irb`). Receiver not mapped:
remote presses change no GPIO DATA/STAT bit, and the AON L2 intc at
`0xf0419c00` (kbd1/2/3 = bits 0–2) stays at status `0`, mask `7f` — BOLT does
not enable the IR receiver block. Needs the block's registers (Nexus RE).

## Interrupt chain: SW1 → GIC (partly verified from BOLT)

```
SW1 (AON GPIO bank 0 bit 14)
  → GPIO STAT & MASK      0xf0419c9c / 0xf0419c94          ✅ verified
  → AON L2 intc bit 3     0xf0419c00 ("gio", upg_main_aon)  ✅ verified
  → GIC SPI 66 = ID 98    dist 0xffd01000 (DTB: <0x0 0x42 ...>)  ❌ not reachable (unmapped in BOLT MMU)
  → CPU
```

AON L2 intc (`brcm,bcm7271-l2-intc`, level variant; per Linux
`drivers/irqchip/irq-brcmstb-l2.c`): `+0x0` STATUS, `+0x4` MASK_STATUS,
`+0x8` MASK_SET, `+0xc` MASK_CLEAR (there is no clear register; the source
must be cleared). Bits: 0–2 kbd1–3, 3 gio, 4 ldk, 5 icap, 6 spare. BOLT leaves
them all masked (`7f`).

Test: clear GPIO STAT (`e -w 0xf0419c9c ffffffff`, write-1-to-clear), set GPIO
MASK bit 14 (`e -w 0xf0419c94 00004000`), press SW1. Result: GPIO STAT =
`00004000` and **L2 STATUS = `00000008`, with the L2 mask still `7f`**. So
L2 STATUS shows the raw (pre-mask) state, and the GPIO interrupt reaches the
L2 with no trigger-type (EC/EI/LEVEL) setup. The level-type L2 stays asserted
until GPIO STAT is cleared.

The next hop (L2 → GIC, ID 98 → ISENABLER3/ISPENDR3 bit 2 at
`0xffd0110c`/`0xffd0120c`) needs the GIC mapped. See the MMU warning above.

## Timer, temperature, watchdog (verified from BOLT)

Register layouts are taken from the Linux 6.6 drivers named below; addresses
come from the DTB. All three were tested with `d -w` / `e -w`.

| Block | Base | Linux driver |
|---|---|---|
| Temperature (AVS TMON, `brcm,avs-tmon`) | `0xf04d1500` | `drivers/thermal/broadcom/brcmstb_thermal.c` |
| Wake timer (`brcm,brcmstb-waketimer`) | `0xf041a080` | `drivers/rtc/rtc-brcmstb-waketimer.c` |
| Watchdog (`brcm,bcm7038-wdt`) | `0xf040a6a8` | `drivers/watchdog/bcm7038_wdt.c` |

**Temperature**: `+0x00` STATUS. Bit 11 = valid, bits 10:1 = code.
Temperature (m°C) = `410040 − code × 487` (28 nm constants).
Example: `00000dd8` → code 748 → 45.8 °C. This agrees with BOLT's own reading
(`T=+45.277C` at the next boot).

**Wake timer**: `+0x04` COUNTER (seconds since power-on), `+0x0c` PRESCALER
(`019bfcc0` = 27,000,000, i.e. 27 MHz), `+0x10` PRESCALER_VAL (sub-second
countdown), `+0x08` ALARM, `+0x00` EVENT. It runs under BOLT, so no setup is
needed.

**Watchdog**: `+0x0` TIMEOUT (in 27 MHz ticks), `+0x4` CMD. Start by writing
`ff00` then `00ff`; stop by writing `ee00` then `00ee`. Reading CMD returns the
ticks left. The order matters: a wrong first value makes it ignore the second.
For a 10 s reboot:

```
e -w 0xf040a6a8 1017df80
e -w 0xf040a6ac 0000ff00
e -w 0xf040a6ac 000000ff
```

After a watchdog reset, the first-stage boot log prints `RR:00000040` (a normal
power-on prints `RR:00000003`). This looks like a reset-reason register, with
bit 6 meaning watchdog.

## Chip ID, software reset, AON SRAM (verified from BOLT)

| What | Address | Result |
|---|---|---|
| Family ID / product ID (`sun-top-ctrl` +0x0 / +0x4, per Linux `drivers/soc/bcm/brcmstb/common.c`) | `0xf0404000` / `0xf0404004` | `72680010` / `72680010` |
| Software reset (DTB `reboot` node → `brcmstb-reboot.c`) | `0xf0404304` reset-source enable, `0xf0404308` SW master reset | write `1` to `+0x304`, then `1` to `+0x308` → immediate reboot |
| AON SRAM (DTB `aon-ctrl`, 1 KB) | `0xf0410200`–`0xf04105ff` | survives software and watchdog resets (tested at `0xf04105fc`) |

**Reset reason (`RR:` in the first-stage boot log)**: each reset type
printed a different value, so this is almost certainly a reset-history
register:

| RR | Reset |
|---|---|
| `00000003` | power-on |
| `00000040` | watchdog |
| `00000200` | software master reset |

Where the value is read from is not yet known. `0xf0410008` is **ruled
out**: it read `03300200` after a software reset (low bits matched
`RR:00000200`), but still read `03300200` after a watchdog reset that printed
`RR:00000040`. The `0x200` match was a coincidence, or this is a different
register. The `sun-top-ctrl` reset area after a watchdog reset
(`d -w 0xf0404300 0x40`) read:
`00000000 00000110 00000000 00000000 0000005c 0000008b 00000000 ...` (rest zero).
No word equals `0x40`. `+0x304` (reset-source enable) is `0x110`: BOLT's
setting, and the `1` written earlier was cleared by the reset. `+0x310` = `0x5c`
has bit 6 set, but so do other bits, so this is inconclusive. To test it,
dump again after a software reset and see whether `+0x310` changes. It is
also possible that early boot clears the history before BOLT runs.

**AON SRAM**: Linux keeps suspend-resume magic in its first words, so use the
end of the region. It survives both a software reset and a watchdog reset
(tested at `0xf04105fc`). It has not been tested across a full power-off.

**ARM generic timer**: CP15 registers, no MMIO address, so it can't be tested
from BOLT. Its frequency is not yet confirmed; 27 MHz is expected.

AON pin-mux `0xf0410700` (4-bit fields, 8 pins/reg) reads
`00000000 00000000 00112222 00000100` under BOLT. The GPIO function is not
always `0` (LED pins 16–18 are `2`), so fields cannot be decoded without docs.
Main GPIO bank 1 STAT bits 8/9 stay set and do not clear.

## DTS provenance ("which one is original?")

The canonical device tree is the one BOLT was running when `rescue.txt` was
captured — dumped live via `dt show` (BOLT: `DT_SIZE a53e` = 42,258 B). It is
reproduced exactly in `boot/original_dtb.dts`. Its `chosen` node is vendor-stock:

```
chosen {
    stdout-path = "/rdb/serial@f040c000:115200";
};
```

No `bootargs`, no `no-map`, no `bl31` node. This is the tree to trust for
bare-metal work.

`boot/dtb.dtb` (`Decompiled_dtb.dts`) is a **later, patched variant**, not the
original — differences vs `original_dtb.dts`:

- `chosen` has an injected kernel cmdline (`root=/dev/mmcblk1p9 ... initcall_debug`)
- phantom root nodes `0x80000000{}`, `0x00000000{}`, `reg{}`, `memory{}`
- USB nodes renamed: `usb@`→`usb-phy@`, `ehci@`→`ehci_v2@`, `ohci@`→`ohci_v2@`, `bdc@`→`bdc_v2@`
- blob size differs (0x9af7 vs the resident 0xa53e)

Other patched derivatives seen elsewhere, also not originals:

- `dtb_working_fixed.dts` — original bootargs + `init=/bin/sh` (USB rescue variant)
- `dtb_usb_debian.dts` — `root=/dev/sda2` + `no-map`/`bl31@7df00000` memory reservations (Debian-from-USB variant)

## Boot chain (reference)

```
BOLT v1.34 (mmcblk0boot0, immutable)
  → sysinit.txt from USB FAT32 (boot/sysinit.txt)
  → load dtb.dtb + zImage, setenv DT_ADDRESS
  → go <addr> — raw jump, skipping the 2 KB image header
```

The bare-metal monitor loads the same way: BOLT `load` + `go`, then the GIC is
programmed from `assembly/boot.s`.