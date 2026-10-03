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

It runs under BOLT, so no setup is needed.

**Alarm (tested 2026-10-03, `../bolt/raw/sys/addrmap1_probe.s`):** write
ALARM = COUNTER + 3. EVENT bit 0 turned `1` in the second COUNTER reached
ALARM (`0xd0`) and stayed `1`. Writing `1` to EVENT cleared it. ALARM reads
`0` after a reset.

The DTB routes the wake timer to the AON L2 controller `0xf0410640`
(`sys_pm`) bit 4. With that controller fully masked (mask `007fffff`, as
after BOLT), its status register stayed `0` while EVENT was set, so status
there is not visible while masked (unlike the AON UPG controller
`0xf0419c00`, `interrupts.md`). Unmasking it was not tried.

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

## Reset reason (tested)

**AON `0xf041006c`** holds the reset history: one bit per cause. The
first-stage loader prints it as `RR:`. BOLT (`0x07012110`) reads it once,
keeps the value at **`0x07069844`** in its own RAM, then writes `1` and `0`
to AON `0xf0410000`, which clears the history. That's why the register reads
`0` once BOLT is running. BOLT passes the saved value to Linux as the DTB
`/bolt` `reset-history` (and the names as `reset-list`).

Tested 2026-10-03 (`../bolt/raw/sys/sys_probe.s`, output next to it):

| Reset | Banner `RR:` | `0x07069844` at `BOLT>` | `0xf041006c` at `BOLT>` |
|---|---|---|---|
| power-on | `00000003` | `00000003` | |
| watchdog | `00000040` | `00000040` | `0` (probe, before the reset) |
| software master reset | `00000200` | `00000200` | |

To read the last reset reason from `BOLT>`: `d -w 0x07069844 1` (BOLT's
RAM, always mapped).

Bit names, from BOLT's table at `0x070422c4` (`{mask, name}` × 24, used for
`reset-list`). The three bits marked tested were seen on the board; the
others are BOLT's names only:

| Bit | Name | | Bit | Name |
|---|---|---|---|---|
| 0 | `power_on` (tested) | | 12 | `undervoltage_1` |
| 1 | `main_chip_input` (tested, set with 0 at power-on) | | 13 | `overvoltage_1` |
| 2 | `tap_in_system` | | 14 | `overtemp` |
| 3 | `front_panel_4sec` | | 15 | `cpu_ejtag` |
| 4 | `s3_wakeup` | | 16 | `scpu_ejtag` |
| 5 | `smartcard_insert` | | 17 | `gen_watchdog_1` |
| 6 | `watchdog_timer` (tested) | | 18 | `aux_chip_edge_0` |
| 7 | `pcie_0_hot_boot` | | 19 | `aux_chip_edge_1` |
| 8 | `pcie_1_hot_boot` | | 20 | `aux_chip_level_0` |
| 9 | `software_master` (tested) | | 21 | `aux_chip_level_1` |
| 10 | `security_master` | | 22 | `avs_watchdog` |
| 11 | `undervoltage_0` | | 23 | `mpm` |

Earlier candidates, now explained: `0xf0410008` (`03300200` after both
reset types) is not it; SUN_TOP `0xf0404310/14` are something else.

AON control `0xf0410000–0x1f` after a software reset:
`00000000 00000000 03300200 00000002 00000010 0000007f 002932e0 00107ac0`.
`0xf041006c` reads fine even though `0xf041002c` aborts.

## Straps (tested)

BOLT's banner `strap=%08x,%08x` prints SUN_TOP **`0xf040401c`** and
**`0xf0404020`** (`0x07024924`). This board: `00000f1e,00000000`, read the
same by the probe.

| Bits of `0xf040401c` | BOLT's use (`0x0701238c`, `0x070123bc`) | This board |
|---|---|---|
| 0–4 | boot device: 0–23 NAND, 24–27 SPI, 28–29 NOR, 30 **eMMC**, 31 SPI | `0x1e` = 30 = eMMC (the box boots from eMMC) |
| 9 | 1 = SATA disabled (banner "SATA is disabled"), 0 = "PCIe is disabled" | 1 |

The other bits (`0xf00`: 8, 10, 11) are not decoded.

## OTP fuses (read only; never write)

The banner prints the three OTP words and the name of every set field, from
BOLT's table at `0x070472a8` (`{bus address, mask, name}` × 37; bus
`0x2040xxxx` = CPU `0xf040xxxx`). The values below were read by the banner
and by the probe; the field names are BOLT's.

| Register | This board | Set fields |
|---|---|---|
| `0xf0404030` | `00000040` | `en_cr` (mask `0x60`) |
| `0xf0404034` | `00a02000` | `macrovision_disable` (23), `rv9_disable` (21), `mtsif_enc_ctl_disable` (13) |
| `0xf0404520` | `00000000` | none (the banner skips a zero word) |

Every field BOLT names:

| Register | Bit(s) | Name |
|---|---|---|
| `0xf0404030` | 4 | `rave_verify_enable` |
| | 5–6 | `en_cr` |
| | 7 | `en_testport` |
| | 8 | `audio_spdif_disable` |
| | 12 / 13 | `hvd0_disable` / `hvd1_disable` |
| | 21 / 27 | `usb_p1_disable` / `usb_p0_disable` |
| | 22 | `vc5_disable` (the V3D GPU) |
| | 23 | `av_output_disable` |
| | 24 | `vmxwatermarking_disable` |
| | 25 | `hdcp22_disable` |
| | 26 | `hdmi_rx_disable` |
| `0xf0404034` | 0 | `cpu_clk_lock_enable` |
| | 1 | `hdcp_disable` |
| | 2 | `cpu_dvfs_disable` |
| | 3 | `pcie_disable` |
| | 4–7 | `avs_adjust_voltage` |
| | 8 | `ddr_phy_dpfe` |
| | 9 | `avs_disable` |
| | 10 | `wlan_jtag_allow` |
| | 11–12 | `cpus_to_use` |
| | 13 | `mtsif_enc_ctl_disable` |
| | 14 | `wlan_dedicated_gpio_disable` |
| | 15–18 | `ephy_trim_in` |
| | 19 | `hdmi_pass_thru_disable` |
| | 20 | `ephy_trim_in_enable` |
| | 21 | `rv9_disable` |
| | 22 | `sata_disable` |
| | 23 | `macrovision_disable` |
| `0xf0404520` | 0 | `tc_hdr_disable` |
| | 1–4 | `wlan_radio_tx_disable` |
| | 5 | `dv_hdr_disable` |
| | 6–9 | `wlan_radio_rx_disable` |
| | 10 | `dtu_disable` |
| | 11 | `cwmwatermarking_disable` |
| | 12 | `tc_itm_disable` |

**Bond option**: the banner prints the low byte of **`0xf04e6134`** (clock
generator block). Probe: `15a08203`, banner: `bond option: 0x03`.

## General control registers (values only)

| Register | DTB name | Value (probe, at `go -64`) |
|---|---|---|
| `0xf0404084` | `sun-top-ctrl-general-ctrl-1` | `00004000` |
| `0xf04040a4` | `sun-top-ctrl-general-ctrl-no-scan-0` | `00d9b7df` |

The DTB only lists them in the `s3 { syscon-refs }` node: registers saved
and restored across S3 suspend. Their bits are not decoded.

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
