# History: the Linux 6.6 port and Debian migration

This is a historical record of the project's first phase: getting a Linux
userspace onto the KSTB6077, first Alpine on the vendor kernel, then a
mainline Linux 6.6 kernel, written by embedded-re. It was last updated on
2026-06-07 and is kept as it was then. It is not maintained. The current, tested description of the
hardware is in the other chapters.

## What later work corrected

Some statements below were the best knowledge at the time and have since
been shown wrong on the board:

| Stated here | Now known | Where |
|---|---|---|
| BOLT only boots 32-bit | `go -64` starts AArch64 at EL2, `boot -64 -el3` at EL3 | [`../booting.md`](../booting.md) |
| BOLT does not initialise HDMI, so there is no framebuffer to inherit | BOLT's boot splash sets up HDMI and leaves a 1920 × 1080 RGB565 framebuffer, but only if `flash0.splash` is valid. This port had repurposed that partition | [`../hardware/display.md`](../hardware/display.md) |
| Wi-Fi is a BCM4335 | BCM43570 on PCIe (`14e4:aa31`, chip `0xaa32`) | [`../stock-firmware.md`](../stock-firmware.md) |
| 2048 MB DDR4 | 2 GB LPDDR4 (one Samsung K4F6E3S4HM-MGCJ) | [`../../README.md`](../../README.md) |
| `setenv` is volatile, there is no `saveenv` | `setenv -p` writes NVRAM | [`../bolt/bolt.md`](../bolt/bolt.md) §5 |
| RPMB status unknown | the RPMB key was never programmed | [`../bolt/bolt.md`](../bolt/bolt.md) §8 |

## Overview

The work covered hardware analysis, a survey of the Android userspace, BOLT
bootloader access, an Alpine Linux install and a port to mainline Linux 6.6
with Debian 12 as the target userspace.

- **First goal:** headless Alpine Linux on the internal eMMC. Reached in April
  2026.
- **Second goal:** mainline Linux 6.6 with Debian 12 armhf: a full
  single-board computer, later with a GUI through a USB display adapter. In
  progress when this record stopped: Linux 6.6.0 was running, the move from
  Alpine to Debian 12 armhf was under way.

## 1. Hardware (as recorded then)

| Field | Value |
|---|---|
| Model | KaonMedia KSTB6077 |
| SoC | Broadcom BCM7268 B0 (4 × Cortex-A53 "B53", 1656 MHz) |
| Userspace architecture | ARMv7l (32-bit) |
| RAM | 2048 MB at 1856 MHz |
| Storage | eMMC 7.28 GiB (`DG4008`, `mmcblk0`) |
| Ethernet | Broadcom GENET v5 at `0xf0480000`, MAC `90:F8:91:E7:00:0A` |
| Wi-Fi | Broadcom on PCIe (PCI ID `14e4:aa31`), `brcmfmac` |
| Bluetooth | BCM2045A0 on USB (`0a5c:2045`), `btusb` |
| UART | 115200 8N1, `ttyS0` at `0xf040c000`, works both ways under BOLT |
| USB | xHCI + 2 × EHCI + 2 × OHCI (USB 2.0 and 3.0) |
| HDMI | present; Broadcom BVN/VEC display pipeline, no mainline driver |
| Board | `KM_SH368AT` |
| Serial number | `BS1006373C001535` |
| OEM key | `ATV00000019ST_TMCZ` (Slovak/Czech T-Mobile region) |

## 2. Original software

| Field | Value |
|---|---|
| OS | Android TV 8.0 Oreo |
| Kernel | Linux 4.1.45-1-15pre (Broadcom stbgcc-4.8-1.6) |
| SELinux | enforcing (fixed on user/release-keys builds) |
| Bootloader | BOLT v1.34, Verified Boot **ORANGE** (unsigned images accepted) |

## 3. eMMC partitions

### Original Android layout (14 partitions)

| Partition | Size | Notes |
|---|---|---|
| `flash0.macadr` (p1) | 512 B | MAC addresses |
| `flash0.nvram` (p2) | 64 KB | NVRAM: board config, serial, BT MAC |
| `flash0.bsu` (p3) | 942 KB | BOLT sidecar ELF |
| `flash0.misc` (p4) | 1 MB | boot control block |
| `flash0.hwcfg` (p5) | 1 MB | hardware config (cramfs): **do not touch** |
| `flash0.factory_settings` (p6) | 16 MB | factory data: **do not touch** |
| `flash0.splash` (p7) | 12 MB | boot splash |
| `flash0.metadata` (p8) | 8 MB | A/B metadata |
| `flash0.cache` (p9) | 1024 MB | Android cache |
| `flash0.recovery` (p10) | 64 MB | **recovery kernel**, used as the boot trampoline |
| `flash0.boot` (p11) | 64 MB | Android boot image |
| `flash0.system` (p12) | 1528 MB | Android system (ext4) |
| `flash0.vendor` (p13) | 224 MB | Android vendor HALs |
| `flash0.userdata` (p14) | 4353 MB | **reused for the Alpine root filesystem** |

### Layout after the migration (9 partitions)

| Partition | Size | Contents |
|---|---|---|
| p1 | 512 B | MAC addresses |
| p2 | 64 KB | NVRAM |
| p3 | 942 KB | BOLT BSU |
| p4 | 1 MB | misc |
| p5 | 1 MB | hwcfg |
| p6 | 64 MB | **kernel slot** (`flash0.KRN`) |
| p7 | 4 MB | **DTB slot** |
| p8 | 2 GB | swap |
| p9 | 5.2 GB | **root filesystem** |

The `splash` partition was later added back by shrinking p9
([`../hardware/display.md`](../hardware/display.md), "Adding a splash
partition").

## 4. Android: paths that did not lead to root

Every Android userspace path was tried before moving to BOLT.

### 4.1 ADB

- `uid=2000(shell)`, no root.
- DirtyCOW blocked by SELinux.
- CVE-2019-2215 does not apply (needs kernel 4.4 or later).
- No setuid binaries.

### 4.2 Network services

| Port | Owner | Result |
|---|---|---|
| 10101 | UID 10001 / root | binary handshake, not pursued |
| 55577 | Broadcom `nxserver` | Nexus remote debug, not exploitable |

### 4.3 Kaon HAL service

A root HIDL service with 39 methods, including GPIO, LED, MAC/serial write,
DRM key write and `enableSecureLock()` (irreversible). Two undocumented
methods, `getRegister`/`setRegister`, give direct register access. Not
reachable from `uid=2000` (SELinux, hwbinder only).

## 5. BOLT, as used then

### 5.1 Access

BOLT v1.34 is reachable over the UART (115200 8N1): interrupt the boot, or
choose "Reboot to bootloader" in Android recovery. Verified Boot ORANGE:
unsigned images are accepted.

### 5.2 Variables

| Variable | Value |
|---|---|
| `STARTUP` | `boot -elf -noclose -bsu flash0.bsu; android boot -rawfs` |
| `DT_ADDRESS` | `0x7614000` (original) / `0x7700000` (the port's load address) |
| `MEMORYSIZE` | `2048` |

### 5.3 Commands used

| Command | Notes |
|---|---|
| `load -loader=raw -addr=X device:file` | load a raw file into RAM |
| `load -loader=zimg -addr=X device:file` | load and decompress a zImage |
| `go addr` | jump to an address: used to start the kernel |
| `dt on` | enable DTB changes |
| `dt bolt` | activate the loaded DTB |
| `d addr size` | hex dump to the UART |
| `batch usbdisk0:file` | run a script from a FAT32 USB stick |

### 5.4 BOLT problems met

- **Quoting:** `setenv bootargs "..."` in a batch file strips the quotes and
  keeps only the first word. Fix: put the kernel arguments in the DTB's
  `/chosen` node.
- **DTB lock:** `dt add prop` returns −8 on this build, so the DTB can't be
  changed in place. It was patched on the PC instead.
- **Partitions:** after the Android partitions were reformatted, `android
  boot`'s own DTB loading broke. The DTB was then loaded with `load`.
- **Start address:** a plain zImage starts at `go 0x02208000`; an Android
  boot image at `go 0x02208800` (past its 2 KB header).

## 6. Phase 1: Alpine Linux on the vendor kernel 4.1 (finished April 2026)

### 6.1 Approach

The vendor recovery kernel (`flash0.recovery`, Android boot image format)
served as the boot trampoline, started with `go 0x02208800`. An Alpine
minirootfs went onto the reused eMMC partition.

### 6.2 Findings

- `mkfs.ext4` defaults (`metadata_csum`, `64bit`) don't work with kernel 4.1:
  format with `-O ^metadata_csum,^64bit`.
- OpenRC hangs on 4.1.45: replaced with busybox init and a `startup.sh`.
- `chronyd` times out on the 1970 → 2026 clock jump: busybox `ntpd` instead.
- `sshd` without `-D` daemonises cleanly.
- `/dev/pts` must be mounted before `sshd` starts, for PTY allocation.

### 6.3 Boot chain

```
BOLT v1.34
  → sysinit.txt from a FAT32 USB stick
  → loads dtb_emmc.dtb from USB at 0x7700000
  → loads flash0.recovery (vendor kernel) at 0x02208000
  → go 0x02208800
  → Linux 4.1.45 → Alpine Linux 3.23 on eMMC p14
```

### 6.4 Wrong assumptions

| Assumption | Reality |
|---|---|
| `setenv bootargs "..."` works in a batch file | strips quotes: use DTB `/chosen` |
| `dt add prop` works | returns −8: locked |
| `flash0.boot` has the right kernel | the recovery kernel is in `flash0.recovery` |
| OpenRC works on 4.1.45 | hangs: use busybox init |
| `mkfs.ext4` defaults are fine | `metadata_csum` breaks kernel 4.1 |
| chrony handles large clock offsets | times out: use busybox `ntpd` |
| BOLT's partition handling survives reformatting | breaks: load the DTB from USB |

## 7. Phase 2: mainline Linux 6.6 (in progress when this record stopped)

### 7.1 Motivation

Alpine 3.23's apk-tools 3.x crashed on armv7 with an illegal instruction,
and the vendor kernel 4.1.45 is end-of-life. The goal was a mainline kernel
with a long-term supported distribution.

### 7.2 Kernel build

| | |
|---|---|
| Source | Linux 6.6 LTS |
| Architecture | 32-bit ARM, `multi_v7_defconfig` base |
| Cross-compiler | `arm-linux-gnueabihf-gcc` 13.3.1 (Arm GNU Toolchain) |
| Output | `arch/arm/boot/zImage`, about 13 MB, magic `0x016f2818` |

Options added to `multi_v7_defconfig`:

```
CONFIG_ARCH_BRCMSTB=y        # BCM7268 platform
CONFIG_SERIAL_8250_BCM7271=y # UART driver
CONFIG_MMC_SDHCI_BRCMSTB=y   # eMMC
CONFIG_BCMGENET=y            # GENET v5 Ethernet
CONFIG_USB_BRCMSTB=y         # USB host controllers
CONFIG_PHY_BRCM_USB=y        # USB PHY
CONFIG_PCIE_BRCMSTB=y        # PCIe
CONFIG_GPIO_BRCMSTB=y        # GPIO
CONFIG_BRCMSTB_THERMAL=y     # thermal
CONFIG_BCM7038_WDT=y         # watchdog
CONFIG_ARM_BRCMSTB_AVS_CPUFREQ=y  # CPU frequency
CONFIG_BRCMFMAC=y            # Wi-Fi
CONFIG_WIREGUARD=y           # Tailscale
CONFIG_TUN=y                 # TUN/TAP
# systemd, cgroups, namespaces
CONFIG_CGROUPS=y
CONFIG_NAMESPACES=y
CONFIG_MEMCG=y
CONFIG_NF_NAT=y
```

### 7.3 Device tree changes

Mainline Linux has no BCM7268 device tree. The vendor DTB was taken from
the eMMC, decompiled and patched in two rounds
([`../hardware/device-tree.md`](../hardware/device-tree.md)).

**Round 1: GIC interrupt types.** The vendor DTB gives every peripheral
`IRQ_TYPE_NONE (0x00)`, which the Linux 6.6 GIC driver rejects. Every
`<0x00 0xNN 0x00>` was replaced with `<0x00 0xNN 0x04>` (level high).
GENET's interrupts use a 6-cell list and were missed by that replacement, so
they were fixed by hand:

```
interrupts = <0x00 0x61 0x00 0x00 0x62 0x00>
         → <0x00 0x61 0x04 0x00 0x62 0x04>
```

**Round 2: clocks.** The vendor DTB's clock nodes are
`compatible = "brcm,brcmstb-sw-clk"`, which has no mainline driver. Every
device that depends on a clock stayed in `-EPROBE_DEFER`: both SDHCI
controllers (eMMC), all USB host controllers and GENET. A Python script
replaced all 19 such nodes with `fixed-clock` nodes at 125 MHz and kept
their phandles.

Also changed:
- `sdhci@f0200100` (the SD slot) disabled, so the eMMC becomes `mmcblk0`.
- `no-map` reserved memory added for BL31 (`0x7df00000`), SRR (`0x7e000000`)
  and bmem (`0x40000000`), which stopped `VM_FAULT_OOM` memory corruption.
- All kernel arguments moved to the DTB's `/chosen` node.

### 7.4 HWCAP check

```
AT_HWCAP:  0x003fb0d6   VFP, NEON, AES, PMULL, SHA1
AT_HWCAP2: 0x0000001f   SHA2, AES, PMULL, SHA1, CRC32
```

The kernel reports the crypto extensions correctly. The apk3 crash is an
Alpine armv7 packaging problem, not a kernel bug.

### 7.5 First boot of Linux 6.6.0

```
[    7.900] EXT4-fs (mmcblk0p9): mounted filesystem r/w
[    7.913] VFS: Mounted root (ext4 filesystem)
[    7.948] Run /sbin/init
   OpenRC 0.63 is starting up Linux 6.6.0 (armv7l)
```

### 7.6 Boot chain

```
BOLT v1.34 (mmcblk0boot0)
  → reads sysinit.txt from a FAT32 USB stick
  → load -loader=raw  -addr=0x07700000 usbdisk0:dtb_usb_debian.dtb
  → load -loader=zimg -addr=0x02208000 usbdisk0:zImage
  → dt on
  → dt bolt
  → go 0x02208000
  → Linux 6.6.0
  → root on sda2 (USB) or mmcblk0p9 (eMMC)
  → systemd
  → getty on ttyS0, sshd
```

### 7.7 Status

| Component | Status | Notes |
|---|---|---|
| Linux 6.6.0 | running | armv7l, SMP, 4 cores |
| All 4 CPUs | works | PSCI v0.2 |
| eMMC (`mmcblk0p9`) | works | ext4 read/write, ADMA |
| GENET Ethernet | works | 100 Mbit/s full duplex |
| Serial console | works | `ttyS0`, 115200 |
| SSH | works | |
| Thermal | works | AVS TMON |
| Watchdog | works | BCM7038 |
| AVS CPU frequency | works | |
| PCIe | works | Wi-Fi chip detected |
| TUN/WireGuard | works | kernel support present |
| USB host | partial | deferred probe: PHY clock problem, not solved |
| Wi-Fi | partial | driver present, firmware not installed |
| Bluetooth | partial | `btusb` present, firmware needed |
| HDMI | no | Broadcom Nexus pipeline only, no mainline driver |

### 7.8 Known problems

- **USB host deferred probe:** EHCI, OHCI and xHCI stay in `-EPROBE_DEFER`.
  The USB PHY's clocks were converted to `fixed-clock` too, but the probe
  still fails, probably on another dependency (a reset controller or
  regulator).
- **apk-tools 3.x crash:** Alpine 3.23's apk3 crashes on armv7 with an
  illegal instruction. An Alpine packaging bug (`AT_HWCAP2` is correct).
  Workaround: the static apk2 binary. Long-term: Debian 12 armhf.
- **GENET interrupt warnings:** `HW irq 129/130 has invalid type`, even
  after the DTB fix. Ethernet works, but its interrupt set-up is not fully
  right.

## 8. Phase 3: Debian 12 armhf (in progress when this record stopped)

### 8.1 Why Debian

- the largest armhf package repository
- apt/dpkg have no armv7 problems
- five years of long-term support
- systemd
- the best support for a later USB display adapter (DisplayLink)

### 8.2 Target

- Distribution: Debian 12 (bookworm) armhf, systemd.
- Boot: the same BOLT chain, kernel from the USB stick's FAT32 partition.
- Root: ext4 on the USB stick's `sda2` for testing, then the eMMC's
  `mmcblk0p9`.

### 8.3 USB stick layout

```
sda (USB stick)
├── sda1  FAT32  512 MB  BOLT files: sysinit.txt, dtb_usb_debian.dtb, zImage
└── sda2  ext4   rest    Debian 12 armhf root filesystem
```

### 8.4 debootstrap

```bash
# first stage
sudo debootstrap --arch=armhf --foreign bookworm \
    /mnt/debian_root http://deb.debian.org/debian

# second stage: needs qemu-arm-static and binfmt registered
sudo cp /usr/bin/qemu-arm-static /mnt/debian_root/usr/bin/
sudo chroot /mnt/debian_root /debootstrap/debootstrap --second-stage
```

The second stage was still pending (a qemu binfmt registration problem).

## 9. Files of that phase

Only `sysinit.txt` and the DTB variants in `boot/` are in this repository.
The rest stayed local.

| File | Notes |
|---|---|
| `sysinit.txt` | BOLT autoboot script on the USB stick ([`../../boot/sysinit.txt`](../../boot/sysinit.txt)) |
| `dtb_usb_debian.dtb` | patched DTB for the USB Debian boot |
| `dtb_working_fixed.dts` | decompiled and patched DTS source |
| `fix_dtb_v2.py` | Python script that patched the DTB |
| `zImage` | Linux 6.6.0 kernel for the BCM7268 |
| `bsu.bin` | BOLT sidecar ELF from the eMMC |
| `kstb6077_mmcblk0_full.img` | full eMMC backup |
| `kstb6077_mmcblk0boot0_BOLT.img` | BOLT bootloader backup |
| `stringyfiedNexus.ko.txt` | strings of the Nexus kernel module |
| `KSTB6077_assets.zip` | Android init scripts, Wi-Fi config, SAGE binaries |

## 10. Security layers

| Layer | Status then |
|---|---|
| BOLT | fully accessible: no authentication, Verified Boot ORANGE |
| Secure boot (BFW/SSBL) | active; protects only the chain up to BOLT, can't be bypassed |
| Android SELinux, dm-verity | irrelevant once Android was replaced |
| RPMB | not investigated (since then: key never programmed) |
| TrustZone | present (`tz` commands in BOLT), not explored |

## 11. Wrong assumptions in phase 2

| Assumption | Reality |
|---|---|
| the apk3 crash is a kernel HWCAP bug | `AT_HWCAP2 = 0x1f` is correct: an Alpine armv7 packaging bug |
| simplefb can take over BOLT's framebuffer | none was found then (the splash partition was gone; see the table at the top) |
| one `sed` fixes every GIC interrupt type | GENET's 6-cell list was missed |
| the USB deferred probe is only a clock problem | `fixed-clock` fixed SDHCI; the USB PHY has another dependency |
| the eMMC stays `mmcblk0` when both SDHCI controllers probe | `mmc0` takes `mmcblk0`; the eMMC was `mmcblk1` until the first SDHCI was disabled |
| `VM_FAULT_OOM` meant low memory | missing `no-map` on the BL31/SRR regions corrupted memory |
| `make zImage` after config changes rebuilds everything | `make clean` is needed after `make mrproper` |

## 12. Board identifiers

| Field | Value |
|---|---|
| Board serial | `BS1006373C001535` |
| MAC (eth0) | `90:F8:91:E7:00:0A` |
| BT MAC | `90:F8:91:E7:00:0C` |
| IP (then) | `192.168.1.84/24` |
| Root UUID | `917ddad4-dc4a-4a2b-88dc-336bab935bee` |
