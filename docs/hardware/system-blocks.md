# System blocks: timers, temperature, watchdog, chip ID, reset, AON SRAM

Addresses come from the original DTB. Register layouts match the Linux
drivers named below. Every block was exercised from BOLT with `d -w` / `e -w`.

| Block | Base | DTB compatible | Linux driver |
|---|---|---|---|
| Temperature (AVS TMON) | `0xf04d1500` | `brcm,avs-tmon` | `drivers/thermal/broadcom/brcmstb_thermal.c` |
| Wake timer | `0xf041a080` | `brcm,brcmstb-waketimer` | `drivers/rtc/rtc-brcmstb-waketimer.c` |
| Watchdog | `0xf040a6a8` | `brcm,bcm7038-wdt` | `drivers/watchdog/bcm7038_wdt.c` |
| SUN_TOP_CTRL | `0xf0404000` | `brcm,brcmstb-sun-top-ctrl` | `drivers/soc/bcm/brcmstb/common.c`, `drivers/power/reset/brcmstb-reboot.c` |
| AON control + SRAM | `0xf0410000` / `0xf0410200` | `brcm,brcmstb-aon-ctrl` | |

## Temperature sensor

`+0x00` STATUS: bit 11 = valid, bits 10:1 = code.
Temperature (m°C) = `410040 − code × 487` (28 nm constants).
Example: `00000dd8` → code 748 → 45.8 °C, matching BOLT's own reading
(`T=+45.277C`).

## Wake timer

| Offset | Register |
|---|---|
| `+0x00` | EVENT |
| `+0x04` | COUNTER: seconds since power-on |
| `+0x08` | ALARM |
| `+0x0c` | PRESCALER: `019bfcc0` = 27,000,000 (27 MHz) |
| `+0x10` | PRESCALER_VAL: sub-second countdown |

It runs under BOLT, so no setup is needed. EVENT and ALARM have not been
tested.

## Watchdog

| Offset | Register |
|---|---|
| `+0x0` | TIMEOUT, in 27 MHz ticks |
| `+0x4` | CMD. Start: write `ff00` then `00ff`. Stop: `ee00` then `00ee`. Reading it returns the ticks left |

A wrong first value makes the watchdog ignore the second. Examples:

```
e -w 0xf040a6a8 1017df80      10 s  (270,000,000 ticks)
e -w 0xf040a6a8 202fbf00      20 s  (540,000,000 ticks)
e -w 0xf040a6ac 0000ff00
e -w 0xf040a6ac 000000ff
```

Arming the watchdog before running experimental code makes the board reboot
itself back to BOLT if the code hangs.

## Chip ID

`0xf0404000` (family ID) and `0xf0404004` (product ID) both read `72680010`:
BCM7268, revision B0.

## Software reset

The DTB `reboot` node points at SUN_TOP_CTRL `+0x304` / `+0x308`:

```
e -w 0xf0404304 00000001      reset source enable
e -w 0xf0404308 00000001      software master reset → immediate reboot
```

BOLT's value at `+0x304` is `0x110`.

## Reset reason

The first-stage boot log prints `RR:` with a different value for each reset
type:

| RR | Reset |
|---|---|
| `00000003` | power-on |
| `00000040` | watchdog |
| `00000200` | software master reset |

The register it's read from is not identified:
- `0xf0410008` is ruled out. It read `03300200` after both a software reset
  and a watchdog reset.
- SUN_TOP_CTRL `0xf0404300–0x33f` after a watchdog reset:
  `00000000 00000110 00000000 00000000 0000005c 0000008b 00000000 …`.
  `+0x310 = 0x5c` includes bit 6 but is inconclusive. The next test is to
  re-dump after a software reset.
- Early boot may clear the history before BOLT runs.
- BOLT itself knows the value: in the DTB it hands to Linux, the `/bolt`
  node has `reset-history = <0x200>` and `reset-list = "software_master"`
  (stock box, after a software reset; `../stock-firmware.md`).

AON control `0xf0410000–0x1f` after a software reset:
`00000000 00000000 03300200 00000002 00000010 0000007f 002932e0 00107ac0`.

## AON SRAM

1 KB at `0xf0410200–0xf04105ff`. It keeps its contents across software and
watchdog resets (tested at `0xf04105fc`); it hasn't been tested across a
power-off. Linux keeps suspend-resume magic in its first words, so use the
end of the region.

## ARM generic timer

Read with system-register instructions (32-bit: `mrc p15` `CNTFRQ`/`CNTPCT`;
64-bit: `mrs CNTFRQ_EL0`/`CNTPCT_EL0`); it has no MMIO address. The DTB
describes it as `arm,armv8-timer`. **CNTFRQ_EL0 = `019bfcc0` = 27,000,000:
27 MHz**, the same clock as the wake timer and watchdog (read from AArch64
EL2 and EL3).
