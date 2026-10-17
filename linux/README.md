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

Alpine's busybox has no `tftp` applet; the 10 KB overlay came from a
short-lived `python3 -m http.server` on the PC (SHA-256 checked on both
sides). `UUID 435cda17-99fa-492f-ba47-d04cc34286ea`.

Tested with `kstb-linux --root`: `EXT4-fs (mmcblk0p8): mounted filesystem …
r/w`, `VFS: Mounted root (ext4 filesystem)` at 1.1 s, DHCP, the clock from
NTP at boot (`kstb-rc`), `apk update` and `apk add nano` from the Internet;
973 MB, 893 MB free. A file written in `/root` and `nano` were still there
after `reboot` (busybox init: `umount -a -r`, PSCI reset, kmon, boot again),
and ext4 mounted without a journal recovery.

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
- HDMI audio, the IR receiver, the GPU (`brcm,7268-v3d` exists in mainline, not tried), Wi-Fi (PCIe), Bluetooth firmware.
- Logins and SSH: both consoles are root shells without a password.
- Starting Linux by itself at power-on: today it is `linux` typed at `kmon>`.
- KVM and KASLR are off; nothing tested them.
