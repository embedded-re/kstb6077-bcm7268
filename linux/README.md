# Linux on the KSTB6077 (arm64, mainline 6.18)

A mainline arm64 Linux kernel with a busybox initramfs, started from kmon.
Unlike the old 32-bit port ([`../docs/history/linux-port.md`](../docs/history/linux-port.md)), it runs
64-bit on top of our own firmware: kboot's EL3 monitor answers PSCI, and
the device tree is written by hand from the tested chapters in `docs/`.

Tested on the modified box on 2026-10-17 unless marked otherwise.

| | |
|---|---|
| Kernel | Linux 6.18.57 (LTS), `ARCH=arm64`, built with clang 23 / ld.lld (`LLVM=1`), no gcc |
| Config | `alldefconfig` (upstream defaults) + [`kstb.config`](kstb.config): built in, except our drivers and those that load files from ROOT (modules, below); Image 13.1 MB with ALSA and DRM ("Kernel config base" below) |
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

`make` first applies our kernel patches, [`patches/*.patch`](patches) (`-p1` from
the source root), each once: one that already reverse-applies is skipped
(stamp `build/linux-6.18.57/.kstb-patched`). Extracting the tarball over
an existing tree keeps the stamp: delete it then, or the patches stay
out. So far one patch, for the GPU reset ("GPU" below).

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
| Bluetooth | (USB, OHCI0) `btusb` + `btbcm` as modules | BCM43569A2, patched with the stock `BCM43569.hcd` (build 0000 → 0172), the box's own BD address `90:F8:91:E7:00:0C`. `UP RUNNING`; BLE scan (`hcitool lescan`, 12 s): 86 advertisements from 3 devices; classic inquiry: 2 devices. The patch is offered once per boot ("Bluetooth" below) |
| xHCI | `usb@f0b01000` (`brcm,bcm7445-xhci`, `generic-xhci`), `brcm,has-xhci` on the PHY, ID 124 | `Host supports USB 3.0 SuperSpeed`, a USB 3 root hub with 2 ports (no USB 2 ports). A SuperSpeed-capable stick straight in the USB-A port still came up on EHCI1 at high speed; the xHCI ports stayed in RxDetect: nothing is wired to them ([`../docs/hardware/usb.md`](../docs/hardware/usb.md)) |
| HDMI audio | `hdmi-audio@f0ca0000` (`kaon,kstb6077-hdmi-audio`, our own driver [`drivers/kstb-audio.c`](drivers/kstb-audio.c), a module) | ALSA card 0 `KSTB6077 HDMI`, 48 kHz, S32_LE, stereo. `aplay -d 30` took 30.00 s, also with 5 ms periods, no underrun; the default device converts other rates and formats (`plug`). Underrun, `rmmod`/`modprobe` and the load at boot from `KRN` tested. **Not yet heard**: nobody listened to the TV ("HDMI audio" below) |
| GPU (V3D 3.3) | `gpu@f1200000` (`brcm,7268-v3d`, mainline `v3d`, built in), hub/core interrupts SPI 78/77; power island `clock-controller@f041d020` (our module [`drivers/kstb-v3d-power.c`](drivers/kstb-v3d-power.c)) | `[drm] Initialized v3d 1.0.0`, `/dev/dri/card0`, `renderD128`; IDs `000e1133` / `03443356`. Binner + render jobs through the GPU MMU and both interrupts: clear and triangle pixel-exact ([`rootfs/usr/sbin/kstb-v3d-test`](rootfs/usr/sbin/kstb-v3d-test)). OpenGL ES 3.0 through Mesa 23.3.6 with our patch: a coloured and a textured triangle exact ("GPU" below) |
| GISB arbiter | `gisb-arb@f0400000` (`brcm,gisb-arb`, mainline `brcmstb_gisb`) on the sys L2 `interrupt-controller@f0403000` (`brcm,l2-intc`, SPI 59) bits 0 and 2 | a read of an abort hole is reported: `kstb-reg f04d150c` gave `GISB: target abort at 0xf04d150c [R ], core: cpu_0` and one TEA interrupt (and the program's `Bus error`) |
| HDMI display (KMS) | `display@f0603484` (`kaon,kstb6077-display`, our own driver [`drivers/kstb-drm.c`](drivers/kstb-drm.c), a module), vblank on the BVNF_0 L2 `interrupt-controller@f0606000` (`brcm,l2-intc`, SPI 17) bit 0; until it loads, `chosen/framebuffer@7db0b700` (`simple-framebuffer`) | `/dev/dri/card1` `kstb-drm`: 1920 × 1080 at 59.94 Hz, RGB565 (also BGR565, ARGB8888, ABGR8888, checked by the HDMI CRC), page flips at vsync (59.94 per second, none missed), the console on its fbdev emulation, scanout buffers from CMA; Mesa renders straight into them (kmsro). Seen on the TV by the user ("Display" below). Before it loads, simplefb shows the boot log on BOLT's buffer; the console and the USB keyboard on `tty1` work on both |

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
external module against this kernel, so the kernel tree only carries the
small fixes in [`patches/`](patches) and a driver can be reloaded without a reboot (`rmmod`,
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

Kbuild doesn't rebuild external modules when the kernel config changes. After
CMA was turned on (`DMA_CMA` adds a pointer to `struct device`), the old
`kstb-ir.ko` stored its `rc_dev` fields 8 bytes too low and failed to probe
(`-22`), and `kstb-audio` and `kstb-v3d-power` ran with the old layout too.
`make modules` now starts `build/drivers` over whenever the kernel's
`auto.conf` differs from the one it was built with.

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
the copy. The modalias loop skips this adapter, so `bt_patch` does the
only load (a reload in the middle of the first probe cost 11 s). Tested
from `KRN` with kmon's `linux`: one patch at 3.3 s, the chip back at
15.1 s as build 0172 (btbcm first waits 10 s for a reset reply from the
chip that has gone), no further patch.

**BD address.** The chip starts with its default `43:56:9A:20:1F:AC`. The
box's own address is in BOLT's environment, `BOARD_BTMAC 90:F8:91:E7:00:0C`
([`../docs/evidence/rescue.txt`](../docs/evidence/rescue.txt)). Once the patched chip is back, `kstb-rc`
brings `hci0` up, sends Broadcom's Write_BD_ADDR (`hcitool cmd 0x3f
0x001` with the address bytes reversed; the same command btbcm's
`set_bdaddr` uses), then down and up so the kernel reads it back, and
leaves `hci0` down. Tested from `KRN`: `BD Address: 90:F8:91:E7:00:0C` at
21 s, and an LE scan still finds devices with it. (`btmgmt public-addr`
does the same, but hangs when it has no terminal, as in `kstb-rc`.)

## HDMI audio

```
apk add alsa-utils
aplay -D hw:0 -f S32_LE -r 48000 -c 2 file.raw    # the hardware format
aplay file.wav                                     # anything else, converted by alsa-lib
```

[`drivers/kstb-audio.c`](drivers/kstb-audio.c) starts the audio block itself, with the
same register writes as kmon's [`../mon/audio_hw.c`](../mon/audio_hw.c) (BOLT's
splash routine, steps 1–10 in [`../docs/hardware/audio.md`](../docs/hardware/audio.md)), then
writes the HDMI N/CTS for 1080p59.94 (5824 / 140625), the only mode kmon
sets up. It doesn't depend on what kmon left running: it stops the DMA
first.

Ring buffer 0 plays a buffer in DRAM in a loop and ignores the write
pointer, so the ALSA buffer is the ring itself. At START the driver stops
the DMA, sets read = write = start = the ALSA buffer and end = start +
size − 1, and starts it again (the same steps that were tested from
`BOLT>`); the read pointer (bit 31 = wrap flag) is the position. No audio
interrupt is known, so an hrtimer calls `snd_pcm_period_elapsed` once per
period. At STOP, and whenever nothing plays, the ring loops over a 4 KB
buffer of zeros. The buffers come from `dma_alloc_coherent` in Linux's RAM:
the DMA reaches it (DMA mask 31 bits, bit 31 being the wrap flag).
Buffer up to 256 KB, periods from 1 KB, all multiples of 256 bytes (the
ring lengths tried before).

Tested on the box (ring registers read with `kstb-reg`):

| Test | Result |
|---|---|
| Probe | DMA on, ring 0 = `01868000–01868fff` (the zeros, in Linux's RAM), read pointer moving; `f06fa0c8–d0` = `040016c0 00022551 00022551` |
| `aplay -D hw:0 … -d 5 /dev/zero` | 5.02 s; ring `03440000–0346efff` (24064 frames), `hw_ptr` 100096 after ~2.1 s; afterwards back on the zeros |
| 30 s with `-B 20000 -F 5000` (5 ms periods) | 30.00 s, no underrun |
| default device, 44.1 kHz mono S16 | `plug`: rate and route conversion to 48 kHz stereo S32, mmap access; 3.02 s for 3 s |
| underrun (input stalled 2 s) | `underrun!!!`, the ring on the zeros during the gap, playback resumed |
| `speaker-test -D plughw:0 -t sine -f 440` | ran to the end |
| `rmmod` / `modprobe` | DMA off after `rmmod`; full start again at `modprobe` |
| boot from `KRN` | `kstb_audio` loaded by `kstb-rc` at 2.45 s, `aplay` 3 s in 3.05 s |

Not checked yet: the sound on the TV (nobody listened), and that the
picture stays intact (these are the writes kmon makes with the picture
fine, but the TV wasn't watched). Before the first listening test, keep the
TV volume low.

## Display: `kstb-drm`

[`drivers/kstb-drm.c`](drivers/kstb-drm.c) is a small atomic KMS driver for the
HDMI output BOLT's splash (or kmon's `video init`) leaves running
([`../docs/hardware/display.md`](../docs/hardware/display.md)). It doesn't set the display up; it only
changes what it scans out:

| | |
|---|---|
| Mode | one, fixed: 1920 × 1080 at 59.94 Hz (CEA VIC 16 timings, 148.352 MHz); one CRTC, an HDMI-A connector (always connected, EDID not read) |
| Plane | primary only, linear; `RG16`, `BG16`, `AR24`, `AB24` (RGB565, BGR565, ARGB8888, ABGR8888): the graphics feeder's format and pitch registers are set from the buffer (`gfd-format`, `gfd-pitch` in the device tree; without them, RGB565 with pitch 3840 only). Pitch a multiple of 256, source x 0. 32-bit pixels always have alpha (blended over the compositor's blue background), hence no `XR24` |
| Buffers | DMA GEM from CMA (`CMA_SIZE_MBYTES=64`, placed at `0x77000000`); dumb buffers, PRIME export |
| Page flip | the buffer's address into `0xf0603488` and `0xf060348c`; the RDC loads it into the GFD at the next frame |
| Vblank | BVNF_0 L2 bit 0, one per frame; a flip's event goes out at the first one after `0xf0641048` (the GFD's surface address) reads the new buffer |
| Plane or CRTC off | BOLT's buffer `0x7db0b700` is shown again, and the driver waits until the GFD has it before a buffer is freed |
| Console | fbdev emulation (`DRM_FBDEV_EMULATION`); at probe it removes simplefb (`aperture_remove_conflicting_devices` on BOLT's buffer) |

The DRM helpers it needs (DMA GEM, KMS helpers, the in-kernel clients) are
hidden Kconfig symbols an external module can't select:
[`patches/0002-drm-kconfig-helpers-for-kstb-drm.patch`](patches/0002-drm-kconfig-helpers-for-kstb-drm.patch) adds `DRM_KSTB`, which
selects them, and `kstb.config` turns it on.

The interrupt stays unmasked all the time: the L2 bit latches every frame
even while masked, so masking it while vblank is off and unmasking it on
demand fired at once for a frame long past. The first page flip after a
mode set then failed with `EBUSY` (the mode set's event had gone out a frame
late). Kept unmasked: 60 interrupts a second, and none of that.

Tested (2026-10-17/18, kernel over TFTP, then from `KRN`):

| Test | Result |
|---|---|
| probe | `Initialized kstb-drm 1.0.0`, `fb0: kstb-drmdrmfb frame buffer device`, scanning out `0x77500000`; simplefb gone; console seen on the TV by the user |
| `modetest -M kstb-drm -c -p` (Alpine `libdrm-tests`) | connector `HDMI-A-1` connected, the one mode, plane formats `RG16` |
| `modetest -M kstb-drm -s 37@35:1920x1080@RG16 -v` | `freq: 59.94Hz`; the console back afterwards |
| [`drm/kstb-flip-test.c`](drm/kstb-flip-test.c) (CPU-drawn white bar, 16 pixels per frame, two dumb buffers, page flip per frame) | `1199 flips in 20.00 s: 59.94 per second, 0 frames missed`; smooth, no tearing (the user, on the TV) |
| `rmmod kstb_drm` / `insmod` | GFD back on `0x7db0b700` after `rmmod`, no warning; console back after `insmod` |
| GPU straight to the screen ("Mesa" below) | spinning triangle at 59.94 Hz, seen by the user |

```
# on ROOT (libdrm-dev, build-base):
gcc -O2 -o kstb-flip-test kstb-flip-test.c -I/usr/include/libdrm -ldrm
./kstb-flip-test 20
```

**Formats, checked without the TV** (2026-10-18, kernel and new DTB over
TFTP; the same 11 of 11 after flashing them to `KRN`/`DTB` and starting with
`linux`): [`drm/kstb-fmt-test.c`](drm/kstb-fmt-test.c) shows one test picture in each format
and pitch and reads the HDMI transmitter's CRC of the output after each
([`../docs/hardware/display.md`](../docs/hardware/display.md), "HDMI CRC"). Same picture, same CRC:

```
RGB565                 pitch  3840: crc 7de8 7de8 7de8
BGR565                 pitch  3840: crc 7de8 7de8 7de8
ARGB8888               pitch  7680: crc 7de8 7de8 7de8
ABGR8888               pitch  7680: crc 7de8 7de8 7de8
ARGB8888 pitch 8192    pitch  8192: crc 7de8 7de8 7de8
RGB565 pitch 4096      pitch  4096: crc 7de8 7de8 7de8
ARGB8888 alpha 00      pitch  7680: crc 0790 0790 0790
ARGB8888 alpha 5a      pitch  7680: crc 3742 3742 3742
RGB565 again           pitch  3840: crc 7de8 7de8 7de8
ARGB8888 R/B swapped   pitch  7680: crc 13e3 13e3 13e3
RGB565 R/B swapped     pitch  3840: crc 13e3 13e3 13e3
OK: 11 of 11 as expected
```

The format registers are set when the plane is updated, the address at
the next frame by the display lists, so a flip that also changes the
format can show one frame of the old buffer read in the new format. Not
seen on the TV yet (the RGB565 reference picture neither).

## GPU

The V3D 3.3 runs with mainline's `v3d` driver (DRM render node, no
display). After BOLT its power island is off and every GPU register
aborts ([`../docs/hardware/gpu.md`](../docs/hardware/gpu.md), "Power island"). Mainline has no power
domain for it, and `PM_GENERIC_DOMAINS` can't be turned on without an
in-tree driver that selects it. But `v3d` enables its optional clock before
its first register access, so [`drivers/kstb-v3d-power.c`](drivers/kstb-v3d-power.c) offers the
island as a clock: prepare = Nexus's power-up (`0x1d00` to `0xf041d020`,
wait for `0x34000000` under mask `0x74000000`), unprepare = power-down.
The GPU node's `clocks` points at it; `v3d` defers until `kstb-rc` has
loaded the module, then probes (`island off (424e0908)`, then
`Initialized v3d 1.0.0 for f1200000.gpu on minor 0`).

`kstb-v3d-test` (python3, raw DRM ioctls) runs the bare-metal jobs from
`gpu.md` through the driver: buffers from `CREATE_BO` (GPU addresses from
`0x1000`, through the GPU MMU), the clear job, then the binner + render
triangle, `SUBMIT_CL`, `WAIT_BO`.

```
kstb-v3d-test             # IDs, clear, triangle; OK
kstb-v3d-test -n 1000     # 1000 triangle jobs: 0.196 s, 0.20 ms each
```

Results: the clear gave 4096 × `ff00ff00` and nothing outside the tile;
the triangle 1152 blue pixels with the bare-metal spans (row 8: x 8–55,
row 54: 31–32). Interrupts: GIC 109 (core) counts 2 per job (bin and
render done), GIC 110 (hub) the MMU errors. The bare-metal binning list
had `Number of Render Targets` = 0, which makes the binner write to
address 0: with the MMU on, `v3d` reported `MMU error from client ?
(0x40) at 0x0, write violation, pte invalid` for every such job; with 1
(as Mesa writes it) there is none ([`../docs/hardware/gpu.md`](../docs/hardware/gpu.md)).
`v3d` also prints `drm_sched_entity_init: called with uninitialized
scheduler` twice per open: it sets up a scheduler entity for every queue,
including the compute queue V3D 3.3 doesn't have. Harmless.

### Mesa (OpenGL ES)

Mesa removed V3D 3.3 (and 4.1) in 24.0, when V3D 7.1 support came in
(MR 25851). The last Mesa with it is 23.3; Alpine 3.24's Mesa can't drive
this GPU. Tried, on the box, in chroots on `ROOT` so the system's
packages stay as they are:

| Mesa | Where | Result |
|---|---|---|
| 23.3.6 (Alpine 3.19 package) | `/opt/alpine319` | `GL_RENDERER V3D 3.3`, `OpenGL ES 3.0 Mesa 23.3.6`, then a segfault compiling the first shader |
| 23.3.6 built with [`mesa/v3d33-fixes.patch`](mesa/v3d33-fixes.patch) ([`mesa/build-mesa.sh`](mesa/build-mesa.sh), 1038 build steps, over an hour on the box) | `/opt/alpine319/opt/mesa` | **works**: [`mesa/kstb-gles-test.c`](mesa/kstb-gles-test.c) prints `OK` (below) |
| 19.2.7 (Alpine 3.11 package) | `/opt/alpine311` (deleted 2026-10-17) | hangs in the driver (no kernel message); no debug symbols packaged |

The fixes in the patch, all for code paths only V3D 3.3 takes, each found
by a crash, a hang or a wrong picture on the box:

1. `vir_emit_last_thrsw`: a shader without texture lookups runs single
   threaded on 3.3 and has no THRSW; the code then dereferences
   `c->last_thrsw` = NULL (segfault in `v3d_nir_to_vir`).
2. `pixel_scoreboard_too_soon`: tile-buffer writes waited for a "last
   THRSW" that a single-segment shader never has; the scheduler padded
   NOPs until memory ran out (1.9 GB, OOM kill). Now such a shader may
   write the tile buffer from its second instruction, as the tested
   bare-metal shaders do.
3. Register allocation: uniform loads re-created for a spill got class
   "any"; on 3.3 a uniform can only be loaded to `r5` (there is no
   `ldunifrf`), so packing failed: `Failed to pack instruction 13: nop
   t22; ldunifrf (vp_x_scale)`.
4. Vertex and coordinate shader outputs. 3.3 has no indexed VPM stores
   (`stvpm`): a VPM write setup, then each write to the `vpm` register
   goes to the next row. Since Mesa 20 the setup is a comment
   (`/* XXX: v3d33_vir_vpm_write_setup(c); */`), and the outputs were
   written where NIR stores them: with a varying, the colour came before
   Xs Ys Zs 1/Wc. Now the outputs are kept by VPM index and written at
   the end in that order, after the setup, unwritten ones as 0.0 (as in
   Mesa 19's `emit_vert_end`). A scheduler rule keeps the setup 3
   instructions away from an `ldvpm`.
5. **VPM writes in the thread end are lost**: in the final THRSW
   instruction and its two delay slots. Mesa's scheduler put the last
   writes there (coordinate shader: Ys; vertex shader: Zs and 1/Wc), so
   the binner used Ys left in the VPM by the previous job. That is what
   looked like random positions: right within one process (the leftover
   was the same triangle), different after another program ran. Now no
   VPM write may go into those slots (`qpu_inst_valid_in_thrend_slot`);
   the shaders end with THRSW on a nop. Found with `kstb-v3d-gl` below.

Found with dEQP (2026-10-18, "dEQP" below):

6. **`Blend Cfg` is 4 bytes on 3.3**, not 3: byte 3 bits 4–5 are
   `vg_mode` (0 for GL). From Broadcom's `v3d_cl_pack_blend_cfg` and
   `v3d_cl_print_blend_cfg` in the stock `libGLES_nexus.so`
   (`stock/release/hal/egl`), whose field names are `a_eqn a_src a_dst
   c_eqn c_src c_dst vg_mode`; bytes 0–2 match Mesa's XML. With 3 bytes the
   control list lost sync after the packet: every blended draw hung the GPU
   (`V3D_ERR_STAT: 0x00001000`, reset after the timeout, nothing drawn).
   The other 3.3 packets Mesa emits have the same payload sizes as
   Broadcom's packers (compared by the highest byte each packer stores).
7. No `PIPE_CAP_INDEP_BLEND_FUNC` on 3.3 (one `Blend Cfg` for all render
   targets). Mesa then also drops `GL_EXT/OES_draw_buffers_indexed`, which it
   listed without `EXT_draw_buffers2`: `glDisablei(GL_BLEND, i)` gave
   `GL_INVALID_ENUM` and dEQP stopped after its first case.
8. No TFU on 3.3: `glGenerateMipmap` through the TFU left level 1 black
   (the TFU counts the jobs, `TFU_CS` conversion count 2 after two, but the
   output isn't where Mesa reads it). The draw-based fallback is right
   ([`mesa/kstb-tfu-test.c`](mesa/kstb-tfu-test.c): `level0`, `manual`, `tfu` all OK).
9. Uniform loads copied out of `r5` in the next instruction: two uniforms
   as the two sources of one instruction (`vfpack tlb, unif, unif` in the
   clear shader) failed register allocation, so every scissored clear
   segfaulted in `v3d_get_compiled_shader`.
10. Indirectly indexed temporary arrays become if-ladders, not scratch:
    scratch's TMU writes gave `MMU error ... write violation` at random
    addresses.

Results with the patch (2026-10-17): 256×256 FBO, triangle with red,
green and blue corners: 18432 lit pixels (its area), corners
241,8,6 / 7,242,6 / 4,6,245, centre 84,85,86; 5 runs in a row and 20
draws in one process all exact; the same with a texture lookup
(`KSTB_TEX=1`); a constant colour (`KSTB_FLAT=1`) too. 100 frames at
1920×1080 in 0.163–0.197 s. No MMU error, no kernel message. The test
copies its 256×256 picture to `/dev/fb0`, centred at (832,412): seen on
the TV by the user, centred, corners right (2026-10-17). simplefb's
buffer starts inside a page (`0x7db0b700`) and an `mmap` of `/dev/fb0`
starts at the page, so pixel (0,0) is `smem_start & 0xfff` = `0x700`
bytes in; without that the picture landed 896 pixels early, wrapped
from the left edge to the right. The test only writes its own square:
`cat /dev/zero > /dev/fb0` clears the screen. Mesa's shader
cache (`~/.cache/mesa_shader_cache`) keeps shaders from earlier builds:
`MESA_SHADER_CACHE_DISABLE=true` while changing the compiler.

```
# on the box; mesa/ch319 is the chroot helper (in /usr/local/bin there)
ch319 'cd /root && LD_LIBRARY_PATH=/opt/mesa/lib LIBGL_DRIVERS_PATH=/opt/mesa/lib/dri ./kstb-gles-test'
```

**Installed on ROOT** (2026-10-18): `build-mesa.sh` with its default prefix
`/usr/local`, run on ROOT's Alpine 3.24 itself (gcc 15.2, Python 3.14,
meson from Alpine; the script `apk add`s the build packages, among them
`py3-packaging`, which Mesa's meson check for mako needs since Python
dropped `distutils`). No source changes beyond the two patches. musl's
loader searches `/usr/local/lib` before `/usr/lib` and Alpine's own Mesa
isn't installed, so programs need no `LD_LIBRARY_PATH`:

```
gcc -O2 -o kstb-gles-test kstb-gles-test.c -lEGL -lGLESv2 -lgbm
./kstb-gles-test        # libEGL.so.1 => /usr/local/lib/libEGL.so.1 ...; same pixels as in the chroot, OK
```

`kstb-kms-gles` (601 flips in 10.11 s), `kstb-flip-test` and `kmscube`
(below) run the same way. The chroot `/opt/alpine319` stays as it was.

A faster way for changes: cross-building on the PC (clang `--target=aarch64-alpine-linux-musl`,
lld, with `/usr/include`, `/usr/lib` and `/lib` copied from ROOT as the sysroot)
builds the same tree in about 3 minutes; meson needs `-Wl,--undefined-version`
for lld. Tested on the box (2026-10-18): the libraries and `v3d_dri.so`
in `/opt/mesa-x`, used with `LD_LIBRARY_PATH=/opt/mesa-x
LIBGL_DRIVERS_PATH=/opt/mesa-x/dri`; ROOT's own build in `/usr/local` is
unchanged (it has fixes 1–5 only). The dEQP run below used `/opt/mesa-x`.

**kmscube** (2026-10-18, built on ROOT from `gitlab.freedesktop.org/mesa/kmscube`
master, `meson setup build -Dgstreamer=disabled` with
`PKG_CONFIG_PATH=/usr/local/lib/pkgconfig`), `-D /dev/dri/card1 -c 300`:

| Options | Result |
|---|---|
| `-f AR24`, `-f RG16` | `Rendered 299 frames in 4.988 sec (59.940 fps)` each |
| `-A -f AR24` (atomic) | `no eglDupNativeFenceFDANDROID`: exits; Mesa's EGL here has no Android native fence |
| `-f AR24 -M rgba` (texture from a GBM buffer as a dma-buf EGLImage) | **kernel Oops**, `Unable to handle kernel NULL pointer dereference at virtual address 0000000000000024`; the board hung while printing it (no call trace on the serial line, no ping): power cycle needed |

The Oops is open: the call trace never came out. That path (a linear
texture from a dma-buf) differs from the working ones in two ways: v3d
copies the texture to a tiled shadow copy, trying the TFU first
(`v3d_tfu_blit`, no version check), and kmsro imports the buffer into
`kstb-drm` too (`renderonly_create_gpu_import_for_resource`).

**A GPU hang: reset, then the GPU works again** (with
[`patches/0001-v3d-reset-only-existing-queues.patch`](patches/0001-v3d-reset-only-existing-queues.patch)). Before the patch,
a job that didn't finish (twice a bogus VPM read setup in a hand-written
shader, once a triangle on the frame's edges, (1,1) (-1,1) (1,-1), before
fix 5) gave `Internal error: Oops` in `drm_sched_stop+0x94` from
`v3d_gpu_reset_for_timeout`, in the `v3d_reset` worker, for bin and
render jobs alike; every later job timed out (`Device timeout`) until
`reboot -f`. The reset stopped, resubmitted and restarted every queue,
including the CSD and cache-clean queues `v3d` never sets up without a
CSD (V3D < 4.1). The patch skips queues that aren't `sched.ready`, as
`v3d_sched_fini` does. Tested 2026-10-17, kernel booted over TFTP and
then from `KRN`: `CSU=0=0 kstb-v3d-gl` (hangs the binner) gets `Device
timeout`, `v3d` logs `Resetting GPU for hang` (bin, then render), no
Oops; the next `kstb-v3d-gl` and `kstb-gles-test` print `OK`. Twice in a
row on one boot.

**A cache-flush job oopsed the kernel** (fixed by
[`patches/0003-v3d-no-cache-flush-without-csd.patch`](patches/0003-v3d-no-cache-flush-without-csd.patch)):
`DRM_V3D_PARAM_SUPPORTS_CACHE_FLUSH` said 1, so Mesa set
`DRM_V3D_SUBMIT_CL_FLUSH_CACHE` for shaders with TMU writes; the clean job
went to the cache-clean queue, which doesn't exist without a CSD, and
`drm_sched_job_init` oopsed (`NULL pointer dereference at virtual address
0000000000000120`, `v3d_job_init` from `v3d_submit_cl_ioctl`), once per
draw: 21 Oopses in one dEQP group. The patch reports no cache flush
without a CSD and refuses the flag. Tested with the kernel booted over
TFTP (`kstb-linux --root`); `KRN` still has the kernel without it.

#### dEQP

VK-GL-CTS `opengl-es-cts-3.2.14.1`, cross-built on the PC (surfaceless
target, `linux/build/deqp/`), packed as `doom/build/deqp-kstb.tar`
(`deqp-gles2`, `deqp-gles3`, `deqp-egl`, data, mustpass lists) and
unpacked to `/root/deqp`. [`mesa/kstb-deqp`](mesa/kstb-deqp) runs a
module 500 cases per process (pbuffer 256×256, `rgba8888d24s8ms0`) and
resumes after a crash, a GPU hang or a board reset: one line per case in
`/root/deqp/results-<module>.txt`, `--summary` counts them. On the box
`/root/run-deqp.sh gles2` runs it with `/opt/mesa-x` and the watchdog
keep-alive.

**GLES2** (`gles2-main.txt`, 2026-10-18, Mesa 23.3.6 with fixes 1–10, the
kernel with patch 0003):

| Result | Cases |
|---|---|
| Pass | 16256 (94.7 %) |
| Fail | 761 |
| Crash | 37 |
| NotSupported | 106 |
| QualityWarning | 5 |
| total | 17165 |

No Oops; 12 GPU resets in the last 880 cases. Where the failures are:

| Group | Fail + Crash | What is known |
|---|---|---|
| `shaders.operator.*`, `shaders.matrix.*`, `shaders.random.*` | 608 + 1 | in `binary_operator.add` the fragment variants with vec3 and vec4 inputs fail, every vertex variant passes. In `add.highp_vec3_fragment` one component (`v_in1.y`, packed in `VAR1.x`) arrives as 0; it fails with ROOT's original build too. [`mesa/kstb-grid-test.c`](mesa/kstb-grid-test.c) compiles to the same shaders (checked in `V3D_DEBUG=nir`) and draws them right (92×92 indexed grid, depth/stencil, viewport offset), so some state dEQP sets is missing from it. The other groups are not looked at |
| `shaders.loops.*_dynamic_iterations` | 55 | not looked at |
| `fbo.render.*`, `fbo.completeness.*` | 52 | not looked at |
| `shaders.indexing.*`, `shaders.struct.*` | 26 + 36 | the crash looked at (`uniform_array.float_dynamic_read_vertex`): `Failed to pack instruction 31`, `Failed to compile MESA_SHADER_VERTEX`, then a segfault in `v3d_get_compiled_shader` |
| `texture.filtering.*` | 16 | not looked at |
| others | 4 | `shaders.conversions` 2, `fragment_ops.interaction` 2 |

#### GPU straight to the screen: kmsro and `kstb-drm`

For a KMS device the DRI loader opens `<kernel driver name>_dri.so`;
for a display-only driver that is Mesa's kmsro, which pairs it with the
render GPU. [`mesa/kmsro-kstb-drm.patch`](mesa/kmsro-kstb-drm.patch) adds `kstb-drm` to kmsro's
names and entry points, and `build-mesa.sh` builds `-Dgallium-drivers=v3d,kmsro`
(rebuilt on the box: `kstb-drm_dri.so` in `/opt/mesa/lib/dri`). For a
`SCANOUT` resource v3d asks `kstb-drm` for a dumb buffer big enough
(1024 × 32 bpp × n rows), imports it through PRIME and renders into it linear
(raster): stride 1920 × 2 = 3840, what the display needs. `kstb-drm`
waits for the GPU's fence before it flips (`drm_gem_plane_helper_prepare_fb`).

[`drm/kstb-kms-gles.c`](drm/kstb-kms-gles.c): GBM on `kstb-drm`'s card, an RGB565 EGL window
surface, a red/green/blue triangle spinning on dark blue, one page flip per
frame, no CPU copy.

```
gcc -O2 -o kstb-kms-gles kstb-kms-gles.c -I/usr/include/libdrm -I/opt/mesa/include \
	-L/opt/mesa/lib -lEGL -lGLESv2 -lgbm -ldrm -lm
LD_LIBRARY_PATH=/opt/mesa/lib LIBGL_DRIVERS_PATH=/opt/mesa/lib/dri ./kstb-kms-gles 20
```

Result (2026-10-18): two GBM buffers from `kstb-drm`, `stride 3840, format
RG16`; `1200 flips in 20.09 s`, the only late flip the last one (its
`glReadPixels` waits for the GPU: `late flips: 1199`); the centre pixel
`GPU 82,85,82` and in the buffer the display scans out `52aa` = 80,84,80
(the same colour in RGB565). Seen on the TV by the user: smooth and
correct. Same from `KRN`.

#### `kstb-v3d-gl`: a GL shader record without Mesa

[`mesa/kstb-v3d-gl`](mesa/kstb-v3d-gl) builds the whole job by hand (python3, raw
DRM ioctls like `kstb-v3d-test`): binning and render lists for 256×256 in
4×4 tiles, the GL shader record, and Mesa's compiled coordinate, vertex
and fragment shaders for `gl_Position = vec4(pos, 0, 1)` and a constant
colour. Packets are packed by field name with [`mesa/v3dpack.py`](mesa/v3dpack.py) from
Mesa's `v3d_packet_v33.xml`, which has to be next to it:

```
wget https://gitlab.freedesktop.org/mesa/mesa/-/raw/mesa-23.3.6/src/broadcom/cle/v3d_packet_v33.xml
./kstb-v3d-gl                  # 18432 lit pixels, OK
OLD=1 VSEQ='-0.75 -0.25 0.75 -0.25 0 0.25;-0.75 -0.75 0.75 -0.75 0 0.75' ./kstb-v3d-gl
                               # Mesa's code without fix 5: 6144, then 17664 instead of 18432
```

Knobs: `VERTS`, `VSEQ` (several jobs in one process), `CSCODE`/`VSCODE`,
`CSU`, `REC` (record fields), `FEED`/`JUNK` (the vertex fetcher supplies
the coordinate shader's outputs itself), `MAP`. Tested with it
(`docs/hardware/gpu.md`, "GL shader record"): which VPM rows the binner
reads, that writes before the thread end land and the ones in it don't.

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

- OpenGL ES: test triangles and kmscube's smooth cube only; a dma-buf texture (kmscube `-M rgba`) crashes the kernel; no conformance tests run yet. HDMI audio not yet heard.
- Display: 1080p59.94 only (no other modes, no EDID, no hot-plug, no overlay planes); formats RGB565, BGR565, ARGB8888, ABGR8888, but no XRGB8888 (the feeder always applies the fourth byte as alpha).
- Hardware RNG: none found for the host; the stock firmware asks the security processor (Nexus `NEXUS_RandomNumber_Generate`, HSM command `BCMD_cmdType_eUSER_RANDOM_NUMBER`).
- Logins and SSH: both consoles are root shells without a password.
- Starting Linux by itself at power-on: today it is `linux` typed at `kmon>`.
- KVM and KASLR are off; nothing tested them.
