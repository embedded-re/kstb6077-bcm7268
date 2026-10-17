# Linux on the KSTB6077 (arm64, mainline 6.18)

A mainline arm64 Linux kernel with a busybox initramfs, started from kmon.
Unlike the old 32-bit port ([`../docs/history/linux-port.md`](../docs/history/linux-port.md)), it runs
64-bit on top of our own firmware: kboot's EL3 monitor answers PSCI, and
the device tree is written by hand from the tested chapters in `docs/`.

Tested on the modified box on 2026-10-17 unless marked otherwise.

| | |
|---|---|
| Kernel | Linux 6.18.57 (LTS), `ARCH=arm64`, built with clang 23 / ld.lld (`LLVM=1`), no gcc |
| Config | `alldefconfig` (upstream defaults) + [`kstb.config`](kstb.config): everything built in, no modules; Image 10.5 MB ("Kernel config base" below) |
| Device tree | [`kstb6077.dts`](kstb6077.dts) (no BCM7268 tree exists upstream) |
| Userspace | Alpine 3.24.2 aarch64 (busybox, musl, apk): on `ROOT` (mmcblk0p8, ext4), or the minirootfs as a 4 MB initramfs; [`rootfs/`](rootfs/) adds `inittab`, `kstb-rc` and `fstab` |
| Boot | from `kmon>`: **`linux`**, kernel and DTB from the eMMC (`KRN`, `DTB`), root on `ROOT`; or over TFTP for testing ([`../tools/kstb-linux`](../tools/kstb-linux)) |
| Time to init | `Run /sbin/init` at 0.44 s (serial only) and 0.93 s (all drivers below) after the kernel starts |

## Build and boot

```
mkdir -p linux/build/dl && cd linux/build/dl
curl -LO https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-6.18.57.tar.xz
curl -LO https://dl-cdn.alpinelinux.org/alpine/latest-stable/releases/aarch64/alpine-minirootfs-3.24.2-aarch64.tar.gz
cd .. && tar xf dl/linux-6.18.57.tar.xz && cd ../..

make -C linux tftp                          # ~3.5 min the first time; copies linux-* into doom/build
tools/kstb-linux --root --watchdog 150      # board at kmon>, network up: root on ROOT
tools/kstb-linux --watchdog 150             # the same with the initramfs instead
```

With `--watchdog`, run `echo V > /dev/watchdog` in Linux within the time
given: Linux's driver doesn't notice the watchdog kmon armed.

SHA-256 of the two downloads: kernel `5bd9acdc…f98c` (matches kernel.org's
`sha256sums.asc`), Alpine `9bf70a7f…c773` (matches `latest-releases.yaml`).
`linux/build/` is not tracked.

`kstb-linux` sends one `tftp get` at a time and waits for `kmon>` before the
next. Lines sent while kmon is still busy get mixed up (seen once: two
commands merged into one). It compares each file's CRC with kmon's.

Load addresses ([`layout.h`](layout.h), all below `0x06000000`, kmon's
`tftp` limit):

| Address | File |
|---|---|
| `0x02000000` | `Image`; the kernel runs in place (header: text offset 0, image size `0x7d0000`) |
| `0x04800000` | `initramfs.cpio.gz` (the DTB's `linux,initrd-end` is set from its size at build time) |
| `0x05e00000` | `kstb6077.dtb` (initramfs), or `kstb6077-root.dtb` (`root=/dev/mmcblk0p8 rootwait ro`, no initrd; `kstb-rc` checks it, then mounts it read-write) |
| `0x05f00000` | `boot.bin`: `x0` = DTB, `x1`–`x3` = 0, jump to the Image |

kmon's `go` already leaves the MMU and caches off with the data cache
cleaned, at EL2, as the arm64 boot protocol asks; only the registers differ
(kmon passes `x1 = 'KMON'`, `x2` = its resume address), hence `boot.bin`.

## What works

| Part | DTS node | Result |
|---|---|---|
| Serial console | `serial@f040c000` (`ns16550a`, `reg-shift 2`, 81 MHz) | `earlycon` from the first line, then `ttyS0` at 115200 with its interrupt (ID 102); root shell on `ttyS0`. `earlycon` has no baud rate, so it keeps BOLT's divisor |
| 4 cores | `psci` (`arm,psci-0.2`, `smc`) | `smp: Brought up 1 node, 4 CPUs`, `CPU: All CPU(s) started at EL2`: CPU_ON through kboot's EL3 monitor (`0x8600ff00` → `4b423303` checked in kmon first) |
| Timer | `timer` (PPIs 13, 14, 11, 10) | `arch_timer: cp15 timer running at 27.00MHz (phys)`; CNTFRQ as kboot sets it. Interrupts on all 4 cores |
| GIC | `arm,gic-400` at `0xffd01000`/`0xffd02000` | interrupts and IPIs on all 4 cores (`/proc/interrupts`) |
| Memory | `0x00000000–0x7d5fffff` | `Memory: 1996464K/2054144K available`; `free -m`: 1958 MB. Reserved `no-map`: page 0 and the EL3 monitor `0x06400000–0x0640ffff` |
| Reboot | (PSCI) | `reboot -f` → PSCI SYSTEM_RESET → `RR:00000200` (software reset, not the watchdog) → BOLT → STARTUP → kboot → kmon, slot A confirmed |
| Ethernet | `ethernet@f0480000` (`brcm,genet-v5`), MDIO, PHY at 1 | `Broadcom BCM7268 PHY revision: 0x01`, `Link is Up - 100Mbps/Full`, DHCP from the router (192.168.1.33), ping to the PC 0.25 ms |
| eMMC | `mmc@f0200200` (`brcm,sdhci-brcmstb`, 8-bit, `no-1-8-v`) | `mmcblk0: mmc0:0001 DG4008 7.28 GiB`, p1–p10, `boot0`/`boot1`, `rpmb`; `DATA` (p9, FAT32) mounted read-only and listed (`kboot.bin` 21,704 bytes, `doom1.wad`, …); read ~43 MB/s (256 MB in 5.9 s). Nothing was written |
| Watchdog | `watchdog@f040a6a8` (`brcm,bcm7038-wdt`, no clock: driver default 27 MHz) | registered as `watchdog0`. The driver doesn't notice a watchdog that kmon left running; `echo V > /dev/watchdog` (start, then magic close) stopped it: still up at 164 s with kmon's 150 s armed |
| USB | `usb-phy@f0b00200` (`brcm,brcmstb-usb-phy`, `ctrl` only, `brcm,has-eohci`, ipp/ioc 1), EHCI0/1 (`brcm,bcm7445-ehci`), OHCI0/1 (`generic-ohci`) | all four controllers start with `ehci-brcm` / `ohci-platform`, no clocks in the tree. USB-A port: hub `0bda:5411` (high speed), behind it the SanDisk Ultra `0781:558a` as `sda` (250 GB, read 35.6 MB/s) and the keyboard `0461:0010` (low speed, through the hub's transaction translator on EHCI1) as `input0`–`input2`; typing into the HDMI shell works (checked by the user, "Keyboard behind the hub" below). Internal port: Bluetooth `0a5c:2045` on OHCI0 (no driver built) |
| L2 interrupts | `interrupt-controller@f040a600`, `@f0419c00`, `@f040a640`, `@f0419c40` (`brcm,bcm7271-l2-intc`, GIC SPIs 65, 66, 63, 64) | `irq_brcmstb_l2: registered L2 intc` for all four (UPG main, AON, BSC, BSC AON). The AON one carries SW1 below (`/proc/interrupts`: `f0419c80.gpio 14 Edge SW1 standby`) |
| GPIO | `gpio@f040a500` (32, 32, 19, 2) and `gpio@f0419c80` (28, 6), `brcm,brcmstb-gpio`, interrupts through UPG main L2 bit 0 / AON L2 bit 3 | `gpiochip0`–`3` (main) and `gpiochip4`–`5` (AON); each bank is one chip with 32 lines, the DT numbers pins across the banks (AON bank 0 pin 18 = `<&aon_gpio 18 …>`). `libgpiod` from apk works on them |
| Front LEDs | `leds` (`gpio-leds`): `green:power` AON 18, `red:power` AON 17, `blue:status` AON 16, all active-low, open-drain | `/sys/class/leds/*/brightness`: blue on, red on, green off, then back (seen on the box by the user). Green keeps BOLT's state (on) at boot, blue starts off. Triggers `timer`, `heartbeat`, `default-on` and `mmc0` are built in |
| Buttons | `keys` (`gpio-keys`): SW1 AON 14 → `KEY_POWER`; `keys-polled` (`gpio-keys-polled`, 100 ms): SW4 AON 7 → `KEY_BLUETOOTH` | `evtest`: SW1 4 presses → 4 × `KEY_POWER` 1/0 and 8 interrupts (one per edge); SW4 3 presses → 3 × `KEY_BLUETOOTH` 1/0. Polled as in the stock DTB. Nothing acts on the keys yet |
| I2C | `i2c@f040a300`, `@f0419a80`, `@f0419b00`, `@f0419b80`, `@f040a400` (`brcm,brcmstb-i2c`, 97.5 kHz), interrupts on the BSC L2s as Nexus pairs them | `iica`…`iice registered in interrupt mode`, `/dev/i2c-0`…`4` (`i2c-tools` from apk). `i2cdetect -r`: bus 0 `0x30 0x3a 0x50` (HDMI DDC), bus 3 `0x67`, bus 2 nothing, the same as the bare-metal scan ([`../docs/hardware/i2c.md`](../docs/hardware/i2c.md)); one interrupt per address on `iica`, `iicc`, `iicd` (112 per scan). Buses 1 and 4: every transfer `Operation timed out`, no interrupt. EDID read from bus 0 (below) |
| UART1, UART2 | `serial@f040d000`, `@f040e000` (`ns16550a`, as UART0), IDs 103, 104 | `ttyS1`, `ttyS2` (`base_baud = 5062500`). Internal loopback (`TIOCM_LOOP`) at 115200: 1052 bytes each way, intact, with their interrupts counting. Pins unknown ([`../docs/hardware/uart.md`](../docs/hardware/uart.md)) |
| Temperature | `thermal@f04d1500` (`brcm,avs-tmon`, 28 nm constants) on the AVS L2 `interrupt-controller@f04d1200` (`brcm,l2-intc`, SPI 25) bit 6; `thermal-zones` with the stock trips (95, 110 °C passive) and 125 °C critical | `/sys/class/thermal/thermal_zone0/temp`: 53.1 °C idle, 62.8 °C after 90 s with all 4 cores busy. Interrupt tested: trip 0 set to 54.5 °C, one `brcmstb_thermal` interrupt when the load crossed it. No cooling device (no cpufreq yet) |
| RTC | `waketimer@f041a080` (`brcm,brcmstb-waketimer`, 27 MHz) on the `sys_pm` L2 `interrupt-controller@f0410640` (`brcm,l2-intc`, SPI 61) bit 4 | `rtc0`; `hwclock -w` / `-r` work. Only an uptime clock: the counter restarts at every reset (the date written before `reboot` read 44 s at `kmon>`), and an alarm sets the timer's EVENT bit but raises no interrupt while running (the L2 bit is a wake-up source). Time comes from NTP (`kstb-rc`) |
| IR receiver | `ir@f0419900` (`brcm,bcm7268-kbd`, our own driver [`drivers/kstb-ir.c`](drivers/kstb-ir.c), a module) on the AON L2 bit 0 | `rc0`, input device "KSTB6077 IR receiver", key map `rc-kaon-kstb6077` (the stock one, 45 keys). The Kaon remote, checked by the user: 1, 2, 3, OK, VOL+ held, RED each gave its scancode (`MSC_SCAN 20081c` = `KEY_1` …) and key; one interrupt per IR frame, repeats every 108.7 ms. The keys type into the TV's shell. Loaded at boot by `kstb-rc` ("Our own drivers" below) |
| PCIe | `pcie@f0460000` (`brcm,bcm7445-pcie`: the driver's generic STB layout), MSI through the controller (SPI 51), INTA–INTD SPIs 46–49, outbound `0xc0000000–0xdfffffff`, inbound 2 GB at 0 | `link up, 2.5 GT/s PCIe x1`, root port `14e4:7268`, Wi-Fi `14e4:aa31` on bus 1 ([`../docs/hardware/peripherals.md`](../docs/hardware/peripherals.md)) |
| Wi-Fi | `pci@0,0/wifi@0,0` (`brcm,bcm4329-fmac`: the stock MAC `90:f8:91:e7:1b:65`); `brcmfmac` and `cfg80211` as modules | BCM43570 r2, firmware 7.124.22 from `linux-firmware-brcm`, the stock board file. Scan: 2.4 and 5 GHz networks. WPA2 with `wpa_supplicant`: associated at 2462 MHz, −65 dBm, DHCP lease from the router, ping to the Internet over `wlan0` 13 ms, 4.0 MB download in 1.00 s with Ethernet down (rx rate 72.2 Mbit/s). "Wi-Fi" below |
| Bluetooth | (USB, OHCI0) `btusb` + `btbcm` as modules | BCM43569A2, patched with the stock `BCM43569.hcd` (build 0000 → 0172). `UP RUNNING`; BLE scan (`hcitool lescan`, 12 s): 86 advertisements from 3 devices; classic inquiry: 2 devices. The patch is offered once per boot ("Bluetooth" below) |
| HDMI console | `chosen/framebuffer@7db0b700` (`simple-framebuffer`, 1920 × 1080, `r5g6b5`, stride 3840) | `fb0: simplefb registered!`, `Console: switching to colour frame buffer device 240x67`; the boot log and a shell on `tty1` show on the TV, typed into with the USB keyboard (checked by the user) |

Linux reports `GPT:partition_entry_array_crc32 values don't match`: the
backup GPT at the end of the eMMC still describes the old layout
([`../docs/hardware/display.md`](../docs/hardware/display.md)). The partitions come from the primary
table (p10 = `splash` is there).

## Starting without the PC: `KRN`, `DTB` and kmon's `linux` (2026-10-17)

`KRN` (p6, 64 MB) and `DTB` (p7, 4 MB) held the old 32-bit port's Android
boot image and DTB. Each now holds a `KSLT` header sector (kboot's format,
[`../kboot/slot.h`](../kboot/slot.h): magic, size, CRC32, a sequence number, a name) and
then the raw data: the kernel `Image`, and `kstb6077-root.dtb`.

```
make -C linux tftp                     # also build/out/krn.img and dtb.img (tools/make-kslt)
# in Linux on the box, with the two files fetched (wget):
kstb-flash-kernel krn.img dtb.img      # checks GPT names, KSLT magic, size; dd; reads back
# at kmon>:
linux info                             # both headers
linux                                  # read both, check the CRCs, start the kernel
```

kmon finds `KRN` and `DTB` in the GPT by name, reads them to `0x02000000`
and `0x05e00000`, checks the CRCs, the Image's `ARM\x64` magic and the
DTB's `d00dfeed`, stops interrupts, USB and the network, and starts the
kernel with the MMU off, `x0` = the DTB, `x1`–`x3` = 0 (`run_linux` in
[`../mon/start.S`](../mon/start.S)). kmon only reads these partitions; its write guard
still allows only `splash` and `DATA`. Linux updates its own kernel with
`kstb-flash-kernel` (in `/usr/sbin`).

Tested: `linux info` showed both headers; `linux` read the 8.5 MB Image in
289 ms (the 10.5 MB `alldefconfig` one in 389 ms), CRCs matched, Linux
mounted `ROOT` and got DHCP. First from RAM, then from slot A (seq 45) after
a reset. Slot A seq 45 and golden seq 46 both hold `kmon-linux` (CRC
`ad98d6b1`). Starting Linux stays manual: `linux` at `kmon>` (the user's
choice).

## `ROOT` checked at every boot

The kernel mounts `ROOT` read-only (`ro` in `kstb6077-root.dtb`).
`kstb-rc` runs `fsck.ext4 -p` on it, then remounts it read-write. If fsck
repaired something (exit 2–3) it reboots to kmon instead of running on it;
if it can't (exit ≥ 4) it says so: boot the initramfs (`kstb-linux`) and
run `fsck.ext4 -f -y /dev/mmcblk0p8`. `e2fsprogs` is installed on `ROOT`.

Two things had to be in place, both tested:

- `/dev/root` as a link to the real device: `/proc/mounts` lists the root
  as `/dev/root`, which has no node, and e2fsck then doesn't see that the
  device is the read-only root. `kstb-rc` creates the link.
- `CONFIG_BLK_DEV_WRITE_MOUNTED=y` (default `y`): without it the kernel
  refuses to open a mounted block device for writing, so `fsck -p` failed
  with `Resource busy` even on the read-only root. `allnoconfig` had turned
  it off; see "Kernel config base".

Tests: `ROOT: clean, 619/344064 files` at a normal boot. Unclean reset: an
8 MB file written and synced, a small file written without `sync`, then
`reboot -f`. Next boot: `EXT4-fs: recovery required on readonly
filesystem … recovery complete`, `ROOT: clean`, read-write; the 8 MB file's
MD5 unchanged and the small file there. Before this, the same kind of reset
had left orphaned inodes that needed `e2fsck -f -y` by hand. A full
`e2fsck -f -n` from the initramfs after that repair: clean.

## Kernel config base

The first builds used `allnoconfig`: every option off unless
`kstb.config` turns it on. That silently dropped options whose upstream
default is `y`, and two of them broke things here: `USB_EHCI_TT_NEWSCHED`
(the keyboard behind the hub, below) and `BLK_DEV_WRITE_MOUNTED` (fsck of
the root). A scan also found inotify, swap, compaction, namespaces, the
deadline I/O scheduler, PAN and others off. Since then the base is
`alldefconfig`: upstream defaults, then `kstb.config`. 982 options on
instead of about 600; Image 10.5 MB instead of 7.9 MB. Re-tested on it: 4
cores, USB with the keyboard behind the hub (`qh8-1c01`), Ethernet, eMMC,
watchdog, framebuffer, the initramfs path. `CONFIG_SUSPEND` is on, but our
EL3 monitor offers no PSCI suspend, so Linux has no sleep state to use;
CPU hotplug uses PSCI CPU_OFF, which it has.

## Root file system on `ROOT` (2026-10-17)

`ROOT` (mmcblk0p8, 1 GiB then; 5290 MiB since the repartitioning,
[`../docs/hardware/storage.md`](../docs/hardware/storage.md)) held no file system, only an old swap header
labelled `SWAP`. It was installed from the initramfs Linux, on the box:

```
ntpd -n -q -p pool.ntp.org              # the clock starts at 1970; TLS needs the date
apk add e2fsprogs                       # into RAM
mkfs.ext4 -F -L ROOT /dev/mmcblk0p8     # only after checking PARTNAME=ROOT and 2097152 sectors
mount /dev/mmcblk0p8 /mnt/root
wget https://dl-cdn.alpinelinux.org/alpine/v3.24/releases/aarch64/alpine-minirootfs-3.24.2-aarch64.tar.gz
sha256sum …                             # 9bf70a7f…c773, as on the PC
tar xzf alpine-minirootfs-3.24.2-aarch64.tar.gz -C /mnt/root
wget http://<PC>:<port>/rootfs-overlay.tar && tar xf rootfs-overlay.tar -C /mnt/root
umount /mnt/root
```

Alpine's busybox has no `tftp` applet (`busybox-extras` has one); the 10 KB overlay came from a
short-lived `python3 -m http.server` on the PC (SHA-256 checked on both
sides). `UUID 435cda17-99fa-492f-ba47-d04cc34286ea`.

Tested with `kstb-linux --root`: `EXT4-fs (mmcblk0p8): mounted filesystem …
r/w`, `VFS: Mounted root (ext4 filesystem)` at 1.1 s, DHCP, the clock from
NTP at boot (`kstb-rc`), `apk update` and `apk add nano` from the Internet;
973 MB, 893 MB free. A file written in `/root` and `nano` were still there
after `reboot` (busybox init: `umount -a -r`, PSCI reset, kmon, boot again),
and ext4 mounted without a journal recovery.

## EDID over I2C

```
i2ctransfer -y 0 w1@0x50 0 r128      # block 0; w1@0x50 0x80 r128 for block 1
```

The TV's 256 bytes, both checksums correct, decoded with `edid-decode` on
the PC: `PHL`, "PHILIPS FTV", week 12 of 2017, 64 × 36 cm; preferred
timing 1920×1080 at 60 Hz (148.5 MHz); CTA-861 block with 1080p50 as the
native VIC 31, then 1080p60/24/25/30, 1080i, 720p50, basic audio, YCbCr
4:4:4 and 4:2:2. Reading the DDC left the picture alone (kmon's HDMI
set-up doesn't use the bus).

## Modules: our own drivers and the ones that need ROOT

Blocks with no mainline driver get one in [`drivers/`](drivers/), built as an
external module against this kernel, so the kernel tree stays as
downloaded and a driver can be reloaded without a reboot (`rmmod`,
`modprobe`). Of the kernel's own drivers only those that load files from
ROOT when they probe are modules (`cfg80211`, `brcmfmac`, Bluetooth); everything else
stays built in. `kstb.config` turns on `MODULES` and `MODULE_UNLOAD`, and
turns off the few options that would otherwise become `=m` (`HW_RANDOM`,
`BCM2712_MIP`, `EFIVAR_FS`).

```
make -C linux tftp       # also build/out/modules.tar: /lib/modules/<release>, depmod'ed
kstb-modules             # on the box: fetch it over TFTP, replace /lib/modules/<release>
```

`kstb-rc` loads at boot every module whose device is there (`modprobe` of
each `/sys/bus/*/devices/*/modalias`: device tree, PCI, USB). Tested: after
`kstb-flash-kernel` and `reboot`, kmon's `linux` started the kernel from
`KRN`, and `kstb_ir` (1.97 s) and `brcmfmac` (Wi-Fi firmware up at 2.59 s)
were loaded without the PC. Fetch the modules with every kernel: they must
come from the same build, and nothing checks it (no `MODVERSIONS`).

### `kstb-ir`: the IR receiver

[`drivers/kstb-ir.c`](drivers/kstb-ir.c) sets up channel kbd1 as Nexus does
(interrupt enable, CIR on, the 27 NEC timing parameters,
[`../docs/hardware/ir.md`](../docs/hardware/ir.md)) and registers an rc-core scancode device. Per
interrupt it reads STATUS and DATA0 and writes STATUS back with bit 0
clear. A frame with STATUS bit 6 (the first of a press) becomes
`rc_keydown()`, any other `rc_repeat()`. The Kaon remote sends extended NEC
with address bytes `20 08`, so DATA0 `e31c0820` is scancode `0x20081c`
(rc-core's NECX form); the key map is the stock `kaon_301rcu_6077` with
the 42 codes received in `ir.md` plus TUNER, PLAY and PREVIOUSSONG from the
stock file. Unloading turns the decoder off (CMD bits 4, 5 cleared).

Before the driver first ran, kbd1 already read CMD `37`, CIR_ADDR `1b`:
set up earlier (the block is in the always-on part of the chip); what set
it was not checked.

## Wi-Fi

The chip is a BCM43570 (rev 2) on PCIe; mainline `brcmfmac` drives it with
Cypress's firmware from linux-firmware. On the box:

```
apk add linux-firmware-brcm wireless-regdb iw wpa_supplicant
iw dev wlan0 scan | grep SSID
wpa_passphrase <ssid> <passphrase> > /etc/wpa_supplicant/wpa_supplicant.conf
wpa_supplicant -B -i wlan0 -c /etc/wpa_supplicant/wpa_supplicant.conf
udhcpc -i wlan0
```

Wi-Fi is not started at boot; Ethernet stays the default route (lower
metric).

What it took:

- **The board file.** Without one the firmware boots (`RTE (PCIE-MSG_BUF)
  7.124.22 … on BCM43570 r2`, "Firmware init done") but never answers the
  first command: `Timeout on response for query command`, the dongle's
  MAILBOXINT (`BAR0 + 0x48`) stays `0`, then `Firmware has halted or
  crashed`. The same with MSI and with INTA (`pci=nomsi`), so not an
  interrupt problem. The stock firmware's `nvm.txt` (board type `0x0743`,
  antenna and power tables) as `brcmfmac43570-pcie.kaon,kstb6077.txt` (the
  name brcmfmac tries first: chip + the DT's first `compatible`) fixed it;
  `make` puts it into `rootfs-overlay.tar`.
- **The MAC.** `nvm.txt` carries Broadcom's placeholder `00:90:4c:0c:c1:54`;
  a DT node for the endpoint with `brcm,bcm4329-fmac` and
  `local-mac-address` gives `wlan0` the box's own address.
- **Modules.** brcmfmac loads its firmware from ROOT when it probes, so it
  and cfg80211 are modules (`kstb-rc` loads it by its PCI modalias);
  `FW_LOADER_COMPRESS_ZSTD`, since Alpine ships `*.bin.zst`.
- The stock DTB's Wi-Fi supplies (AON GPIO 21, 26) are left out: the chip
  works without them.

`brcmf_inetaddr_changed: fail to get arp ip table err:-52` at DHCP time is
harmless (the firmware has no ARP offload).

## Bluetooth

```
apk add bluez bluez-deprecated     # bluetoothctl; hciconfig, hcitool
hciconfig hci0 up
hcitool lescan
```

`btbcm` sends the patch file at every probe. On this chip the patch's last
command, Launch RAM, makes it leave USB and come back already patched,
so with the file in place it would be patched again, endlessly (seen: five
rounds before the file was removed). `kstb-rc` therefore keeps the patch
outside the firmware path (`/lib/firmware/kstb/BCM43569.hcd`) and offers it
for one probe: copy it to `brcm/BCM-0a5c-2045.kaon,kstb6077.hcd` (the first
name btbcm tries), reload `btusb`, wait for the chip to leave USB, delete
the copy. Tested from `KRN` with kmon's `linux`: one patch at 2.77 s, the
chip back at 14.2 s as build 0172 (btbcm first waits 10 s for a reset reply
from the chip that has gone), no further patch. The BD address is the
chip's default (`43:56:9A:20:1F:AC`).

## Registers from Linux: `kstb-reg`

[`rootfs/usr/sbin/kstb-reg`](rootfs/usr/sbin/kstb-reg) (needs `apk add python3`) reads and writes
32-bit registers through `/dev/mem`, like kmon's `md` / `w`:

```
kstb-reg f04d1500 3            # 3 words
kstb-reg -w f0419c94 4000      # write, read back
```

A read of an abort hole kills only the program (`Bus error`, SIGBUS), not
Linux: `kstb-reg f04d1500 10` died at TMON `+0x0c`, the system kept
running. Small files from the PC: `apk add busybox-extras`, then
`busybox-extras tftp -g -r <file> 192.168.1.38` from the TFTP root.

## Two consoles

The serial line and the TV each have their own shell (`inittab`): `ttyS0`
for the serial line, `tty1` for the HDMI console and the USB keyboard. Kernel
messages go to both (`console=tty0 console=ttyS0`), but what is typed on
one shell doesn't show on the other.

## Keyboard behind the hub

The first builds left the keyboard dead: every key press ended in
`usb 2-1.4: reset low-speed USB device`. usbmon showed its interrupt-IN
transfers completing with `-71`; a debug print of the qTD token gave
`00008d44`: Halted + Missed Micro-Frame, PID IN, 0 bytes left (the 8-byte
report had arrived). The QH in `/sys/kernel/debug/usb/ehci/f0b00500.usb/periodic`
was `qh8-0601`: start-split in microframe 0, complete-splits in 1 and 2 only.

Cause: `allnoconfig` (the config base then) turns off `CONFIG_USB_EHCI_TT_NEWSCHED` (default `y`).
The old scheduler puts the complete-splits at Y+1 and Y+2, too early for a
low-speed device behind the hub's TT. With it on, the QH is `qh8-1c01`
(complete-splits at Y+2, Y+3, Y+4): no missed microframes and no resets,
typing works. `kstb.config` now sets it, and `USB_DEFAULT_PERSIST` (also
default `y`). The other default-`y` options `allnoconfig` drops in the
drivers used here are PS/2 keyboard/mouse (`SERIO`), legacy ttys and eMMC
power-sequence helpers, none of which this box needs.

`kstb.config` also turns on `DEBUG_FS`, `USB_MON` and `DYNAMIC_DEBUG`;
`kstb-rc` mounts debugfs.

## Not done

- xHCI (not described; its ports are unknown, [`../docs/hardware/usb.md`](../docs/hardware/usb.md)).
- HDMI audio, the GPU (`brcm,7268-v3d` exists in mainline, not tried).
- Hardware RNG: none found for the host; the stock firmware asks the security processor (Nexus `NEXUS_RandomNumber_Generate`, HSM command `BCMD_cmdType_eUSER_RANDOM_NUMBER`).
- Logins and SSH: both consoles are root shells without a password.
- Starting Linux by itself at power-on: today it is `linux` typed at `kmon>`.
- KVM and KASLR are off; nothing tested them.
