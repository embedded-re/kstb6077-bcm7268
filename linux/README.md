# Linux on the KSTB6077 (arm64, mainline 6.18)

A mainline arm64 Linux kernel with a busybox initramfs, started from kmon.
Unlike the old 32-bit port ([`../docs/history/linux-port.md`](../docs/history/linux-port.md)), it runs
64-bit on top of our own firmware: kboot's EL3 monitor answers PSCI, and
the device tree is written by hand from the tested chapters in `docs/`.

Tested on the modified box on 2026-10-17 unless marked otherwise.

| | |
|---|---|
| Kernel | Linux 6.18.57 (LTS), `ARCH=arm64`, built with clang 23 / ld.lld (`LLVM=1`), no gcc |
| Config | `allnoconfig` + [`kstb.config`](kstb.config): everything built in, no modules; Image 7.9 MB |
| Device tree | [`kstb6077.dts`](kstb6077.dts) (no BCM7268 tree exists upstream) |
| Userspace | Alpine 3.24.2 aarch64 minirootfs (busybox, musl) as initramfs, 4 MB; [`rootfs/`](rootfs/) adds `inittab` and `kstb-rc` |
| Boot | from `kmon>`: four files over TFTP, then `go` to [`boot.S`](boot.S) ([`../tools/kstb-linux`](../tools/kstb-linux)) |
| Time to init | `Run /sbin/init` at 0.44 s (serial only) and 0.93 s (all drivers below) after the kernel starts |

## Build and boot

```
mkdir -p linux/build/dl && cd linux/build/dl
curl -LO https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-6.18.57.tar.xz
curl -LO https://dl-cdn.alpinelinux.org/alpine/latest-stable/releases/aarch64/alpine-minirootfs-3.24.2-aarch64.tar.gz
cd .. && tar xf dl/linux-6.18.57.tar.xz && cd ../..

make -C linux tftp                          # ~3.5 min the first time; copies linux-* into doom/build
tools/kstb-linux --watchdog 150 --listen 20 # board at kmon>, network up
```

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
| `0x05e00000` | `kstb6077.dtb` |
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
| HDMI console | `chosen/framebuffer@7db0b700` (`simple-framebuffer`, 1920 × 1080, `r5g6b5`, stride 3840) | `fb0: simplefb registered!`, `Console: switching to colour frame buffer device 240x67`; the boot log and a shell on `tty1` show on the TV, typed into with the USB keyboard (checked by the user) |

Linux reports `GPT:partition_entry_array_crc32 values don't match`: the
backup GPT at the end of the eMMC still describes the old layout
([`../docs/hardware/display.md`](../docs/hardware/display.md)). The partitions come from the primary
table (p10 = `splash` is there).

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

Cause: `allnoconfig` turns off `CONFIG_USB_EHCI_TT_NEWSCHED` (default `y`).
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
- HDMI audio, the IR receiver, the GPU (`brcm,7268-v3d` exists in mainline, not tried), Wi-Fi (PCIe), Bluetooth firmware.
- A root file system on the eMMC: everything is in the initramfs, nothing persists.
- Starting Linux without the PC (from `DATA` or a kboot slot).
- KVM and KASLR are off; nothing tested them.
