# KaonMedia KSTB6077 — Linux 6.6 Kernel Port & Debian Migration

**Author:** embedded-re  
**Repo:** github.com/embedded-re/KAON-KSTB6077  
**Status:** 🔧 In Progress — Linux 6.6.0 running, migrating from Alpine to Debian 12 armhf  
**Last updated:** 2026-06-07

---

## Overview

This document covers the full reverse engineering and OS replacement of a KaonMedia KSTB6077 Android TV set-top box — from initial hardware analysis through Android userspace enumeration, BOLT bootloader exploitation, Alpine Linux deployment, and an ongoing port to mainline Linux 6.6 with Debian 12 as the target userspace.

**Original goal:** Headless Alpine Linux on internal eMMC. ✅ Achieved April 2026.  
**Current goal:** Mainline Linux 6.6 kernel with Debian 12 armhf — full SBC with future GUI capability via USB display adapter.

---

## 1. Hardware

| Field | Value |
|---|---|
| Model | KaonMedia KSTB6077 |
| SoC | Broadcom BCM7268 B0 (4× Cortex-A53 "B53", 1656 MHz) |
| Userspace arch | ARMv7l (32-bit — SoC is ARMv8 capable but BOLT only boots 32-bit) |
| RAM | 2048 MB DDR4 @ 1856 MHz |
| Storage | eMMC 7.28 GiB (DG4008, mmcblk0) |
| Ethernet | Broadcom GENET v5 @ 0xf0480000 — MAC: 90:F8:91:E7:00:0A |
| WiFi | Broadcom BCM4335 via PCIe (PCI ID 14e4:aa31) — brcmfmac |
| Bluetooth | BCM2045A0 via USB (0a5c:2045) — btusb |
| UART | 115200 8N1, ttyS0 @ 0xf040c000 — bidirectional in BOLT |
| USB | xHCI + 2× EHCI + 2× OHCI (USB 2.0 + 3.0) |
| HDMI | Present — BCM7268 proprietary BVN/VEC pipeline, no mainline driver |
| Board | KM_SH368AT |
| Serial | BS1006373C001535 |
| OEM Key | ATV00000019ST_TMCZ (Slovak/Czech T-Mobile region) |

---

## 2. Original Software Stack

| Field | Value |
|---|---|
| OS | Android TV 8.0 Oreo |
| Kernel | Linux 4.1.45-1-15pre (Broadcom stbgcc-4.8-1.6) |
| SELinux | Enforcing — hardcoded on user/release-keys builds |
| Bootloader | BOLT v1.34 — Verified Boot: **ORANGE** (unsigned images accepted) |

---

## 3. eMMC Partition Map

### Original Android layout (14 partitions)

| Partition | Size | Notes |
|---|---|---|
| flash0.macadr (p1) | 512 B | MAC address storage |
| flash0.nvram (p2) | 64 KB | NVRAM — board config, serial, BT MAC |
| flash0.bsu (p3) | 942 KB | BOLT sidecar ELF |
| flash0.misc (p4) | 1 MB | Boot control block |
| flash0.hwcfg (p5) | 1 MB | Hardware config cramfs — **do not touch** |
| flash0.factory_settings (p6) | 16 MB | Factory data — **do not touch** |
| flash0.splash (p7) | 12 MB | Boot splash |
| flash0.metadata (p8) | 8 MB | A/B metadata |
| flash0.cache (p9) | 1024 MB | Android cache |
| flash0.recovery (p10) | 64 MB | **Recovery kernel** — used as boot trampoline |
| flash0.boot (p11) | 64 MB | Android boot image |
| flash0.system (p12) | 1528 MB | Android system ext4 |
| flash0.vendor (p13) | 224 MB | Android vendor HALs |
| flash0.userdata (p14) | 4353 MB | **Repurposed for Alpine rootfs** |

### Current layout (9 partitions — post-migration)

| Partition | Size | Contents |
|---|---|---|
| p1 | 512 B | MAC addresses |
| p2 | 64 KB | NVRAM |
| p3 | 942 KB | BOLT BSU |
| p4 | 1 MB | misc |
| p5 | 1 MB | hwcfg |
| p6 | 64 MB | **Kernel slot (flash0.KRN)** |
| p7 | 4 MB | **DTB slot** |
| p8 | 2 GB | swap |
| p9 | 5.2 GB | **Root filesystem** |

---

## 4. Android Attack Surface — Dead Ends

All Android userspace paths exhausted before pivoting to BOLT.

### 4.1 ADB
- UID: uid=2000(shell), no root
- DirtyCOW blocked by SELinux
- CVE-2019-2215 not applicable (requires kernel 4.4+)
- No setuid binaries
- **Verdict: dead end**

### 4.2 Network Services
| Port | Owner | Result |
|---|---|---|
| 10101 | UID 10001 / root | Binary handshake, not pursued |
| 55577 | Broadcom nxserver | Nexus Remote Debug — not exploitable |

### 4.3 Kaon HAL Service
Root-privileged HIDL service with 39 methods including GPIO, LED, MAC/serial write, DRM key write, and `enableSecureLock()` (irreversible). Two undocumented `getRegister`/`setRegister` methods allow direct register access. Not exploitable from uid=2000 (SELinux, hwbinder-only).

---

## 5. BOLT Bootloader

### 5.1 Access
BOLT v1.34 accessible via UART (115200 8N1) — interrupt at boot or trigger via "Reboot to bootloader". Verified Boot ORANGE — unsigned images accepted, no restrictions.

**Critical:** `setenv` is volatile. No `saveenv`. All env vars lost on reboot.

### 5.2 Key Variables
| Variable | Value |
|---|---|
| STARTUP | `boot -elf -noclose -bsu flash0.bsu; android boot -rawfs` |
| DT_ADDRESS | 0x7614000 (original) / 0x7700000 (our load address) |
| MEMORYSIZE | 2048 |

### 5.3 Useful Commands
| Command | Notes |
|---|---|
| `load -loader=raw -addr=X device:file` | Load raw file into RAM |
| `load -loader=zimg -addr=X device:file` | Load and decompress zImage |
| `go addr` | Jump to address — key for kernel boot |
| `dt on` | Enable DTB modification |
| `dt bolt` | Activate loaded DTB |
| `d addr size` | Hex dump to UART |
| `batch usbdisk0:file` | Execute script from USB FAT32 |

### 5.4 Critical BOLT Bugs & Constraints

**Quoting bug:** `setenv bootargs "..."` in batch files strips quotes, assigns only first token. Fix: embed bootargs in DTB `/chosen` node.

**DTB lock:** `dt add prop` returns -8 on this build — no in-place DTB modification. Must patch DTB externally on PC.

**Flash abstraction:** After reformatting Android partitions, `android boot`'s internal DTB load path breaks. Must load DTB explicitly via `load` command.

**go address:** Plain zImage has no header → `go 0x02208000`. Android boot image format → `go 0x02208800` (skips 2KB header).

---

## 6. Phase 1 — Alpine Linux on Kernel 4.1 (Completed April 2026)

### 6.1 Approach
Used vendor recovery kernel (`flash0.recovery`, Android boot image format) as boot trampoline. Loaded via BOLT `go 0x02208800`. Alpine minirootfs deployed to repurposed eMMC partition.

### 6.2 Key Discoveries
- `mkfs.ext4` defaults (`metadata_csum`, `64bit`) incompatible with kernel 4.1 → format with `-O ^metadata_csum,^64bit`
- OpenRC hangs on 4.1.45 → replaced with busybox init + custom `startup.sh`
- `chronyd` times out on 1970→2026 clock offset → use busybox `ntpd`
- `sshd` without `-D` daemonizes cleanly
- `/dev/pts` must be mounted before sshd for PTY allocation

### 6.3 Final Boot Chain (Phase 1)
```
BOLT v1.34
  → sysinit.txt from USB FAT32
  → loads dtb_emmc.dtb from USB @ 0x7700000
  → loads flash0.recovery (vendor kernel) @ 0x02208000
  → go 0x02208800
  → Linux 4.1.45 → Alpine Linux 3.23 on eMMC p14
```

### 6.4 Wrong Assumptions Log (Phase 1)
| Assumption | Reality |
|---|---|
| `setenv bootargs "..."` works in batch | Strips quotes — use DTB /chosen |
| `dt add prop` works | Returns -8, locked |
| `flash0.boot` has the right kernel | Recovery kernel is in `flash0.recovery` |
| OpenRC works on 4.1.45 | Hangs — use busybox init |
| mkfs.ext4 defaults are fine | metadata_csum breaks kernel 4.1 |
| chrony handles large clock offsets | Times out — use busybox ntpd |
| BOLT flash abstraction survives partition reformatting | Breaks after reformatting — load DTB from USB |

---

## 7. Phase 2 — Mainline Linux 6.6 Kernel Port (In Progress)

### 7.1 Motivation
Alpine Linux 3.23 ships apk-tools 3.x which crashes on armv7 with illegal instruction. The vendor kernel 4.1.45 is EOL and missing modern features. Goal: mainline kernel with long-term supported distro.

### 7.2 Kernel Build

**Source:** Linux 6.6 LTS  
**Architecture:** ARM 32-bit (`multi_v7_defconfig` base)  
**Cross-compiler:** arm-linux-gnueabihf-gcc 13.3.1 (Arm GNU Toolchain)  
**Output:** `arch/arm/boot/zImage`, ~13 MB, magic `0x016f2818` ✅

**Key config additions over multi_v7_defconfig:**
```
CONFIG_ARCH_BRCMSTB=y        # BCM7268 platform
CONFIG_SERIAL_8250_BCM7271=y # UART driver
CONFIG_MMC_SDHCI_BRCMSTB=y  # eMMC
CONFIG_BCMGENET=y            # GENET v5 Ethernet
CONFIG_USB_BRCMSTB=y         # USB host controllers
CONFIG_PHY_BRCM_USB=y        # USB PHY
CONFIG_PCIE_BRCMSTB=y        # PCIe
CONFIG_GPIO_BRCMSTB=y        # GPIO
CONFIG_BRCMSTB_THERMAL=y     # Thermal
CONFIG_BCM7038_WDT=y         # Watchdog
CONFIG_ARM_BRCMSTB_AVS_CPUFREQ=y  # CPU freq
CONFIG_BRCMFMAC=y            # BCM4335 WiFi
CONFIG_WIREGUARD=y           # Tailscale
CONFIG_TUN=y                 # TUN/TAP
# Full systemd/cgroups/namespaces support
CONFIG_CGROUPS=y
CONFIG_NAMESPACES=y
CONFIG_MEMCG=y
CONFIG_NF_NAT=y
```

### 7.3 DTB Surgery — Two Rounds

**No mainline BCM7268 DTB exists.** Vendor DTB extracted from eMMC, decompiled, and patched.

#### Round 1 — GIC Interrupt Types
Vendor DTB used `IRQ_TYPE_NONE (0x00)` for all peripherals. Linux 6.6 GIC driver rejects this.

Fix: bulk sed replacement of all `<0x00 0xNN 0x00>` → `<0x00 0xNN 0x04>` (IRQ_TYPE_LEVEL_HIGH).

GENET eth0 uses a 6-cell interrupt format and was missed — fixed separately:
```
interrupts = <0x00 0x61 0x00 0x00 0x62 0x00>
         → <0x00 0x61 0x04 0x00 0x62 0x04>
```

#### Round 2 — Clock Framework (brcm,brcmstb-sw-clk)
Vendor DTB used `compatible = "brcm,brcmstb-sw-clk"` nodes — **no driver in mainline**.

Result: all peripherals with clock dependencies entered `-EPROBE_DEFER` permanently:
- Both SDHCI controllers (eMMC)
- All USB host controllers
- GENET Ethernet

Fix: Python script replaced all 19 `brcm,brcmstb-sw-clk` nodes with `fixed-clock` nodes at 125 MHz, preserving phandle values.

**Also fixed:**
- Disabled unused `sdhci@f0200100` (SD slot) so eMMC enumerates as `mmcblk0`
- Added `no-map` reserved memory for BL31 (0x7df00000), SRR (0x7e000000), bmem (0x40000000) to prevent VM_FAULT_OOM corruption
- All bootargs moved to DTB `/chosen` node

### 7.4 HWCAP Analysis
```
AT_HWCAP:  0x003fb0d6   VFP, NEON, AES, PMULL, SHA1 — correct
AT_HWCAP2: 0x0000001f   SHA2, AES, PMULL, SHA1, CRC32 — correct
```
Kernel correctly advertises crypto hwcaps. apk3 crashes on armv7 due to Alpine packaging issue (compiled with AArch64 assumptions), not a kernel bug.

### 7.5 First Successful Boot — Linux 6.6.0
```
[    7.900] EXT4-fs (mmcblk0p9): mounted filesystem r/w
[    7.913] VFS: Mounted root (ext4 filesystem)
[    7.948] Run /sbin/init
   OpenRC 0.63 is starting up Linux 6.6.0 (armv7l)
```

### 7.6 Current Boot Chain
```
BOLT v1.34 (mmcblk0boot0, immutable)
  → reads sysinit.txt from USB FAT32
  → load -loader=raw  -addr=0x07700000 usbdisk0:dtb_usb_debian.dtb
  → load -loader=zimg -addr=0x02208000 usbdisk0:zImage
  → dt on
  → dt bolt
  → go 0x02208000
  → Linux 6.6.0 boots
  → mounts sda2 (USB) or mmcblk0p9 (eMMC) as root
  → systemd starts
  → getty on ttyS0 + sshd
```

### 7.7 Status Matrix

| Component | Status | Notes |
|---|---|---|
| Linux 6.6.0 | ✅ Running | armv7l, SMP 4 cores |
| All 4 CPUs | ✅ | PSCI v0.2 |
| eMMC (mmcblk0p9) | ✅ | ext4 r/w, ADMA |
| GENET Ethernet | ✅ | 100Mbps/Full |
| Serial console | ✅ | ttyS0 115200 |
| SSH | ✅ | |
| Thermal | ✅ | AVS TMON |
| Watchdog | ✅ | BCM7038 |
| AVS CPUfreq | ✅ | |
| PCIe | ✅ | BCM4335 detected |
| TUN/WireGuard | ✅ | Kernel support present |
| USB host | ⚠️ | Deferred probe — PHY clock issue under investigation |
| WiFi (BCM4335) | ⚠️ | Driver present, firmware not installed |
| Bluetooth | ⚠️ | btusb present, firmware needed |
| HDMI | ❌ | Proprietary NEXUS pipeline only, no mainline driver |

### 7.8 Known Issues

**USB host deferred probe:** EHCI/OHCI/xHCI controllers still in `-EPROBE_DEFER`. Root cause under investigation — USB PHY clock references converted to fixed-clock but probe still fails. Likely secondary dependency (reset controller or regulator) not satisfied.

**apk-tools 3.x crash:** Alpine 3.23's apk3 binary crashes on armv7 with illegal instruction. Confirmed to be an Alpine packaging bug (not a kernel issue — AT_HWCAP2 is correctly populated). Mitigation: apk2 static binary. Long-term solution: migrate to Debian 12 armhf.

**GENET IRQ warnings:** `HW irq 129/130 has invalid type` — fixed in DTB but Ethernet still functions. Interrupt handling not fully correct.

---

## 8. Phase 3 — Debian 12 armhf Migration (In Progress)

### 8.1 Motivation
- Debian 12 has the largest armhf package repository
- apt/dpkg has no armv7 issues
- 5-year LTS support horizon  
- systemd — well understood
- Best hardware compatibility for future USB display adapter (DisplayLink)

### 8.2 Target Architecture
- Distro: Debian 12 (bookworm) armhf
- Init: systemd
- Boot: same BOLT chain, kernel from USB FAT32 partition
- Root: ext4 on USB sda2 (testing) → eMMC mmcblk0p9 (final)

### 8.3 USB Stick Layout
```
sda (USB stick)
├── sda1  FAT32  512MB   BOLT files: sysinit.txt, dtb_usb_debian.dtb, zImage
└── sda2  ext4   rest    Debian 12 armhf rootfs
```

### 8.4 Debootstrap Process
```bash
# Bootstrap Debian 12 armhf
sudo debootstrap --arch=armhf --foreign bookworm \
    /mnt/debian_root http://deb.debian.org/debian

# Requires qemu-arm-static + binfmt registered for second stage
sudo cp /usr/bin/qemu-arm-static /mnt/debian_root/usr/bin/
sudo chroot /mnt/debian_root /debootstrap/debootstrap --second-stage
```

**Status:** Second stage pending — qemu binfmt registration issue being resolved.

---

## 9. Files

| File | Notes |
|---|---|
| `sysinit.txt` | BOLT autoboot script — on USB stick |
| `dtb_usb_debian.dtb` | Patched DTB for USB Debian boot |
| `dtb_working_fixed.dts` | Decompiled + patched DTS source |
| `fix_dtb_v2.py` | Python script for DTB patching |
| `zImage` | Linux 6.6.0 kernel for BCM7268 |
| `bsu.bin` | BOLT sidecar ELF extracted from eMMC |
| `kstb6077_mmcblk0_full.img` | Full eMMC backup |
| `kstb6077_mmcblk0boot0_BOLT.img` | BOLT bootloader backup |
| `stringyfiedNexus.ko.txt` | Nexus kernel module strings — HAL RE artifact |
| `KSTB6077_assets.zip` | Android init scripts, WiFi config, SAGE binaries |

---

## 10. Security Posture

| Layer | Status |
|---|---|
| BOLT | Fully owned — no auth, Verified Boot ORANGE |
| Secure Boot (BFW/SSBL) | Active — protects BOLT chain only, cannot bypass |
| Android SELinux | Irrelevant — Android replaced |
| DM-Verity | Irrelevant — Android replaced |
| RPMB | Status unknown — not investigated |
| TrustZone | Present — `tz` subcommands in BOLT, not explored |

---

## 11. Wrong Assumptions Log (Phase 2)

| Assumption | Reality |
|---|---|
| apk3 crash is a kernel HWCAP bug | AT_HWCAP2=0x1f is correct — Alpine armv7 apk3 packaging bug |
| simplefb can inherit BOLT framebuffer | BOLT does not initialize HDMI — no framebuffer to inherit |
| sed fixes all GIC IRQ types | GENET uses 6-cell interrupt format, missed by bulk sed |
| USB deferred probe is only a clock issue | Fixed-clock resolves SDHCI — USB PHY has additional dependency |
| eMMC stays mmcblk0 after both SDHCI probe | mmc0 claims mmcblk0 — eMMC became mmcblk1 until first SDHCI disabled |
| VM_FAULT_OOM was low memory | Missing no-map on BL31/SRR regions caused memory corruption |
| `make zImage` after config changes rebuilds | Need `make clean` first after `make mrproper` |

---

## 12. Board Identifiers

| Field | Value |
|---|---|
| Board serial | BS1006373C001535 |
| MAC (eth0) | 90:F8:91:E7:00:0A |
| BT MAC | 90:F8:91:E7:00:0C |
| IP (current) | 192.168.1.84/24 |
| Root UUID | 917ddad4-dc4a-4a2b-88dc-336bab935bee |
