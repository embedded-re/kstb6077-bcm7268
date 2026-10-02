# Stock firmware (untouched KSTB6077)

Observed on a second, unmodified KSTB6077 running the stock Android TV
firmware: serial boot logs and a read-only BOLT session. Raw captures are in
`bolt/raw/stock/`:

| File | Contents |
|---|---|
| `android-normal-boot.txt` | full normal boot into Android |
| `android-recovery-to-bolt.txt` | normal boot → recovery (SW4) → "Reboot to bootloader" → `BOLT>` |
| `attempts/` | earlier captures: four normal boots, and one multi-boot log with `RR:00000000` resets and the recovery kernel |
| `help_summary.txt`, `help_android.txt`, `info_devices_env_rmem.txt` | the read-only BOLT session |
| `aon_read_abort.txt` | the AON control read that external-aborted at `0xf041002c` |

The stock DTB is in `../boot/stock_dtb.dts`.

## Boot chain

```
boot ROM → BFW / BBL (secure first stage) → BOLT v1.34
  → AUTOBOOT: no USB stick
  → STARTUP = "boot -elf -noclose -bsu flash0.bsu; android boot -rawfs"
       BSU (BOLT "sidecar" ELF from flash0.bsu) runs at 0x1800000:
         validates the GPT, adds the `android` commands to BOLT
         reads the boot reason (reboot reason + front-panel button)
         sets the wake timer to 1420070400 (2015-01-01 00:00 UTC)
         leaves the watchdog off ('WDT_TIMEOUT' env var not set)
  → android boot: Android boot image from flash0.boot (or flash0.recovery)
       kernel → 0x80000, ramdisk → 0x02208000
       adds the kernel command line, initrd and all 14 GPT partitions to the DTB at 0x07614000
  → "32 bit PSCI boot" → PSCI powers up CPU1–3 ("BOOT32")
  → Linux 4.1.45-1-15pre (32-bit ARM), Android 8.0.0
```

The modified box stops at `BOLT>` because its `STARTUP` variable is unset.

## Boot reasons and how to reach BOLT

| Boot reason | Trigger | Result |
|---|---|---|
| `normal` | nothing | `boot -loader=img -rawfs flash0.boot` |
| `recovery` | **SW4 held at power-on** (BSU checks the "front panel button state") | `flash0.recovery`, the Android recovery menu |
| `bootloader` | Linux `reboot bootloader` (recovery menu → "Reboot to bootloader") | the BSU tries fastboot, then **"stays in BOLT"** → `BOLT>` |

**Easiest way to a `BOLT>` prompt (any box): Ctrl-C.** BOLT cancels its
autostart when it receives Ctrl-C on the console, right after its banner
(`Automatic startup canceled via Ctrl-C`). `tools/kstb-bolt` sends Ctrl-C
continuously during a power-on or a software reset, which always hits the
window (tested from both, `bolt/raw/stock/ctrlc_cancel_after_reset.txt`).
Ctrl-C cancels only the autoboot / `STARTUP` step; BOLT's boot splash still
runs first, so HDMI comes up as usual (`hardware/display.md`).

Without serial access: hold SW4 while powering on, then in the recovery menu
choose "Reboot to bootloader".

How the reboot reason travels:

- Linux logs `brcmstb_reboot: cmd='bootloader', val=98` and does a software
  master reset (the next boot prints `RR:00000200`).
- The BSU then reports `boot_path = ab_bl_recovery; boot_reason = 98` → `boot reason = bootloader`.
- The value 98 survives the reset in an unidentified register.
  AON control `0xf0410000–0x27` matches the modified box, and `0xf041002c`
  gives an external abort (`bolt/raw/stock/aon_read_abort.txt`).
- The `misc` partition's BCB command was empty in an earlier capture. After
  recovery's "Reboot to bootloader" it reads `bootonce-bootloader` on every
  later boot, including normal ones. On this "legacy" boot path the BSU
  ignores it and never clears it.

In bootloader mode the BSU runs `android fastboot -transport=usb -device=flash0`.
It fails without a USB host on `usbdev0`, and then drops to `BOLT>`.

## Extra BOLT commands (added by the BSU)

| Command | Notes |
|---|---|
| `android boot [-rawfs] [-tee [-32]] [-i image]` | boots an Android boot image. `-tee` = "Android trusted boot with BL31/Trusty OS" (64-bit Trusty by default) |
| `android fastboot -transport=usb\|tcp -device=…` | fastboot over USB or **TCP** |

Full help text: `bolt/raw/stock/help_android.txt`.

## Environment: stock vs modified box

| Variable | Stock | Modified box |
|---|---|---|
| `STARTUP` | `boot -elf -noclose -bsu flash0.bsu; android boot -rawfs` | unset |
| `BT_PAIR` | `upg_gio_aon 7`: SW4 is the Bluetooth remote pairing button | unset |
| `DT_ADDRESS` / `DT_SIZE` | `7614000` / `b65e` (46,686 bytes) | `7613000` / `a53e` (42,258 bytes) |
| `PRODUCTNAME` | `KSTB6077` | unset |
| `A_QUIESCENT`, `A_DMVERITY_EIO` | `0`, `0` | unset |
| `EMMC_DEVNAME` | `f0200200.sdhci` | same |

BOLT `rmem` on the stock box also reserves `splash0` (`0x7db08000`, about 4 MB),
the boot-logo framebuffer.

## eMMC partitions (stock GPT)

| Partition | Offset (BOLT) | Size |
|---|---|---|
| macadr | `0x000004400` | 512 B |
| nvram | `0x000004600` | 64 KB |
| bsu | `0x000014600` | 942 KB |
| misc | `0x000100000` | 1 MB |
| hwcfg | `0x000200000` | 1 MB |
| factory_settings | `0x000300000` | 16 MB |
| splash | `0x001300000` | 12 MB |
| metadata | `0x001f00000` | 8 MB |
| cache | `0x002700000` | 1024 MB |
| recovery | `0x042700000` | 64 MB |
| boot | `0x046700000` | 64 MB |
| system | `0x04a700000` | 1528 MB |
| vendor | `0x0a9f00000` | 224 MB |
| userdata | `0x0b7f00000` | 4353 MB |

## Stock DTB (`../boot/stock_dtb.dts`)

This is the tree Linux actually receives: BOLT's base tree
(`boot/original_dtb.dts`) after BOLT's `dt bolt` fix-ups and the BSU's
additions. Differences from the base tree:

- `/memreserve/` entries for PSCI (`0x06400000`, 64 KB) and the splash
  framebuffer (`0x7db08000`)
- `model = "KM_SH368AT"`, and a `serial-number` property
- CPUs: `enable-method = "psci"` (base tree: `brcm,brahma-b53`),
  `clock-frequency = 0x62b48e00` (1656 MHz), operating points
- a `memory` node: 2 GB at 0
- `reserved-memory`: `BL31@7df00000` (1 MB) and `SRR@7e000000` (32 MB, `no-map`)
- **`gpio_keys_polled` → `BT_PAIR`: AON GPIO 7 (SW4), active-low, Linux key
  code `0x18f` (`KEY_GREEN`), polled every 100 ms**
- Ethernet: `phy-mode = "internal"`, a `brcm,28nm-ephy` PHY node at address 1, MAC addresses
- Wi-Fi PCIe node `pci@1,0` with its MAC address
- SDHCI: `no-1-8-v`, `sdhci,auto-cmd12`; eMMC `non-removable`, `bus-width = 8`
- SATA nodes removed (the strap says "SATA is disabled")
- about 30 `brcm,pmap-multiplier/-divider/-mux` clock nodes with values, in
  the clock block `0xf04e0000–0xf04e0510`
- a `/bolt` node: version, build, `board-id = 0x24`, `box = "1"`,
  `bootdev = "emmc"`, **`reset-list = "software_master"`,
  `reset-history = <0x200>`**, `timer-wdog = <0xf040a680>`

So BOLT itself decodes the reset reason (`0x200` = software master reset) and
exports it in the DTB.

## Stock kernel (Linux 4.1.45-1-15pre, Apr 14 2020)

| Observation | Log line |
|---|---|
| 32-bit kernel; **all CPUs start in HYP mode** | `CPU: All CPU(s) started in HYP mode.` |
| 4 CPUs via PSCI v0.2 | `psci: PSCIv0.2 detected in firmware`, `Brought up 4 CPUs` |
| CPU ID | `ARMv7 Processor [420f1000]` |
| generic timer 27 MHz | `Architected cp15 timer(s) running at 27.00MHz` |
| interrupts work | 10 × `irq_brcmstb_l2: registered L2 intc` |
| UARTs | `ttyS0 … irq = 102`, `ttyS1 … irq = 103`, `ttyS2 … irq = 104`, all `base_baud = 5062500` (81 MHz / 16) |
| USB IRQs | xHCI 124, EHCI 122/126, OHCI 123/127 |
| PCIe | root `14e4:7268`, Wi-Fi `14e4:aa31`, IRQ 78, link 2.5 Gbps x1 |
| Wi-Fi | DHD driver: `chipnum 0xaa32` (= **BCM43570**), device `0x43d9`, firmware `7.35.143.172` |
| Ethernet | `Broadcom BCM7268 PHY revision: 0x01` |
| eMMC | `mmc1: DG4008 7.28 GiB`, boot0/boot1/RPMB 4 MiB each |
| SDHCI | `f0200000.sdhci` (SD, mmc0), `f0200200.sdhci` (eMMC, mmc1) |
| front-panel button | `input: gpio_keys_polled` |
| Nexus (proprietary) | `nexus driver initialized`, then `NexusIrHandler`, `Nexus Remote`, `NexusPower`, `NexusHdmiCecDevice` inputs: **IR, power and HDMI-CEC are handled by Nexus** |

Kernel command line (boot image): `mem=2000m@0m mem=40m@2008m
ramoops.mem_address=0x7D000000 ramoops.mem_size=0x800000 bmem=384m@510m
cma=32m brcm_cma=768m@1232m`. Resulting reservations: ramoops 8 MB at
`0x7d000000`, bmem 384 MB at `0x1fe00000`, CMA 768 MB at `0x4d000000` and
32 MB at `0x4b000000`.

| Image | Android | Security patch | Kernel size | Ramdisk size |
|---|---|---|---|---|
| `flash0.boot` | 8.0.0 | 4-2020 | 4,936,264 | 1,678,303 |
| `flash0.recovery` | 8.0.0 | 12-2018 | 4,869,000 | 16,028,697 |

The recovery image carries its own kernel build: `4.1.45-1-15pre … #1 SMP
Fri Feb 1 16:40:17 KST 2019`. The normal boot image's kernel is dated
Apr 14 2020.

## Reset codes seen on the stock box

| RR | Situation |
|---|---|
| `00000003` | power-on |
| `00000200` | software master reset (Linux `reboot bootloader`) |
| `00000000` | resets that interrupted BOLT during AUTOBOOT, before anything loaded. ⚠️ Probably quick power-cycles while timing the button; cause unconfirmed |

## HDMI splash

With a valid `splash` partition, BOLT itself drives HDMI during a normal boot:
`SPLASH BMEM init @ 7defffff`, `Loaded BMP: W=1920 H=1080`. The modified box
prints `bad file 'flash0.splash'` because that partition was repurposed.
The same display can be brought up by hand from the prompt with any BMP, and
it keeps running after `go -64`. See `hardware/display.md`.
