# KaonMedia KSTB6077 — Full Reverse Engineering & Linux Migration
**Author:** embedded-re  
**Repo:** github.com/embedded-re  
**Status:** ✅ Complete — Alpine Linux 3.23 running headless on internal eMMC  
**Last updated:** 2026-04-05

---

## Overview

This document covers the full reverse engineering and OS replacement of a KaonMedia KSTB6077 Android TV set-top box — from initial hardware analysis through Android userspace enumeration, BOLT bootloader exploitation, and final headless Alpine Linux deployment on the internal eMMC. Every dead end, wrong assumption, and fix is documented.

**Goal:** Replace Android TV with a minimal headless Alpine Linux environment. No display stack. SSH + Ethernet only. Raspberry Pi-style server from a discarded cable box.

---

## 1. Hardware

| Field | Value |
|---|---|
| Model | KaonMedia KSTB6077 |
| SoC | Broadcom BCM7268 (4x Cortex-A53 "B53", 1656 MHz) |
| Userspace arch | ARMv7l (32-bit Android on 64-bit capable SoC) |
| RAM | 2048 MB DDR @ 1856 MHz |
| Storage | eMMC 7.28 GiB (DG4008, mmcblk0, 14 GPT partitions) |
| Ethernet | Broadcom GENET v5 @ 0xf0480000 — MAC: 90:F8:91:E7:00:0A |
| WiFi/BT | Broadcom integrated combo — BT MAC: 90:F8:91:E7:00:0C |
| UART | 115200 8N1, /dev/ttyS0 — **bidirectional in BOLT, RX-only in Android** |
| USB | XHCI + EHCI + OHCI (USB 2.0 + 3.0) — mass storage confirmed working |
| HDMI | Present — nxserver crashes without display connected |
| Board | KM_SH368AT |
| Serial | BS1006373C001535 |
| OEM Key | ATV00000019ST_TMCZ (Slovak/Czech T-Mobile region) |

---

## 2. Original Software Stack

| Field | Value |
|---|---|
| OS | Android TV 8.0 Oreo |
| Kernel | Linux 4.1.45-1-15pre |
| Kernel build | gcc 4.8.5 (Broadcom stbgcc-4.8-1.6), 2020-04-14 |
| SELinux | Enforcing — hardcoded on user/release-keys builds |
| Bootloader | BOLT v1.34 — Verified Boot: **ORANGE** |

---

## 3. eMMC Partition Map

| Partition | Size | Notes |
|---|---|---|
| flash0.macadr (p1) | 512 B | MAC address storage |
| flash0.nvram (p2) | 64 KB | NVRAM |
| flash0.bsu (p3) | 942 KB | BOLT sidecar app |
| flash0.misc (p4) | 1 MB | Boot control block (BCB) |
| flash0.hwcfg (p5) | 1 MB | Hardware config — **do not touch** |
| flash0.factory_settings (p6) | 16 MB | Factory data — **do not touch** |
| flash0.splash (p7) | 12 MB | Boot splash image |
| flash0.metadata (p8) | 8 MB | A/B metadata |
| flash0.cache (p9) | 1024 MB | Android cache |
| flash0.recovery (p10) | 64 MB | **Boot trampoline kernel** |
| flash0.boot (p11) | 64 MB | Android boot image |
| flash0.system (p12) | 1528 MB | Android system (ext4, ro) |
| flash0.vendor (p13) | 224 MB | Android vendor HALs (ext4, ro) |
| flash0.userdata (p14) | 4353 MB | **Alpine Linux rootfs (final target)** |
| mmcblk0boot0 | 4 MB | BOLT bootloader — **immutable** |
| mmcblk0boot1 | 4 MB | BOLT backup |
| mmcblk0rpmb | 4 MB | Replay Protected Memory Block |

---

## 4. Android Attack Surface — Dead Ends

All Android userspace paths were systematically attempted and exhausted before pivoting to BOLT.

### 4.1 ADB

- Connected at 192.168.1.28:5555, persistent
- UID: uid=2000(shell) — no root
- SELinux context: u:r:shell:s0, enforcing
- DirtyCOW (CVE-2016-5195): SELinux blocked all three required madvise syscalls
- CVE-2019-2215 (binder UAF): introduced in kernel 4.4+, irrelevant on 4.1.45
- No setuid binaries found (`find -perm -4000` empty)
- `androidboot.selinux=permissive` in cmdline: ignored — hardcoded enforcing on user builds
- **Verdict: dead end.**

### 4.2 Network Services

| Port | Owner | Result |
|---|---|---|
| 10101 | UID 10001 / root | Returns `F` on ASCII — binary handshake required, not pursued |
| 55577 | Broadcom nxserver | Open, silent — Nexus Remote Debug, low-level media stack |

### 4.3 Kaon HAL Service

The vendor runs a root-privileged HIDL service (`kaon.hardware.kaonsvc@1.0-service`) with 39 documented methods including GPIO control, LED control, MAC/serial write, DRM key write, and a dangerous `enableSecureLock()` marked irreversible. Two undocumented methods (`getRegister`/`setRegister`) allow direct hardware register access. The service uses a custom Netlink family ("Lucas") for kernel↔HAL communication. Binary is unreadable from ADB shell (SELinux), hwbinder-only (not in regular service manager). Documented for future reference but not exploitable from uid=2000.

---

## 5. BOLT Bootloader

### 5.1 Access

BOLT v1.34 is accessible via UART (115200 8N1) when interrupted at boot, or triggered via "Reboot to bootloader" from Android. Verified Boot is **ORANGE** — unsigned images accepted, cmdline injection allowed. No authentication, no restrictions.

**Critical:** `setenv` is volatile — all environment variables are lost on reboot. There is no `saveenv` in this build.

### 5.2 Key Variables

| Variable | Value |
|---|---|
| STARTUP | `boot -elf -noclose -bsu flash0.bsu; android boot -rawfs` |
| DT_ADDRESS | 0x7614000 (DTB location in RAM) |
| MEMORYSIZE | 2048 |
| ANDROID_RECOVERY_IMG | flash0.recovery |

### 5.3 Useful Commands

| Command | Notes |
|---|---|
| `load -loader=raw -addr=X flashY` | Load raw partition into RAM |
| `go addr` | Raw jump to address — **key for kernel bypass** |
| `d addr size` | Hex dump to UART — guaranteed exfil |
| `dt show` | Dump full device tree as DTS |
| `batch usbdisk0:file` | Execute script from USB FAT32 |
| `setenv bootargs "..."` | Set kernel cmdline (interactive — quotes work) |

### 5.4 BOLT Batch Parser Bug — Quoting

**Critical discovery.** When `setenv bootargs "..."` is used inside a `sysinit.txt` batch file, BOLT's batch parser strips quotes and splits on whitespace, assigning only the first token to `bootargs`. The kernel receives only `console=ttyS0,115200` and panics immediately with `VFS: Cannot open root device "(null)"`.

In interactive mode, BOLT's parser handles the quoted string correctly. The fix was to pass bootargs via DTB `/chosen` `bootargs` property instead of relying on `setenv` in batch files — eliminating the quoting issue entirely.

---

## 6. Kernel Execution — The Surgical Jump Method

`android boot` parses the Android boot image header and uses its **embedded** cmdline, ignoring any external `bootargs`. The `go` command bypasses `android boot` entirely, jumping directly to the kernel entry point at load address + 0x800 (skipping the 2KB Android boot image header), and the kernel picks up `bootargs` from the BOLT environment or the DTB `/chosen` node.

```
load -loader=raw -addr=0x02208000 flash0.recovery
# 0x02208800 = load address + Android header size (0x800)
go 0x02208800
```

This works because `flash0.recovery` contains the vendor recovery kernel which has full USB stack, eMMC, and Ethernet drivers compiled in.

---

## 7. DTB Strategy — Why We Had to Patch on PC

### 7.1 The Problem

BOLT provides a `dt add prop` command for modifying the device tree, and `setenv bootargs` for setting the kernel cmdline. Both were unusable for persistent automation:

- `dt add prop` returns `-8` (locked DTB) — BOLT on this build does not allow in-place DTB modification
- `setenv bootargs` in batch files loses everything after the first space due to the quoting bug described above

A further complication: **once the Android userdata partition (`flash0.userdata`) was reformatted for Alpine, BOLT's flash abstraction layer lost its ability to load the DTB via the normal `android boot` path.** The `android boot` command's internal DTB loading relied on the partition layout being intact as Android left it. With the partition zeroed and reformatted as ext4, this load path was broken. BOLT had zero memory available to reconstruct or patch the DTB in-place.

### 7.2 The Solution

Dump the DTB from RAM while it was still loaded by BOLT, patch it on the PC, and serve it back from the USB stick on every boot.

**Step 1 — Dump DTB from BOLT RAM:**
BOLT reports `DT_ADDRESS = 0x7614000`. Dump via TFTP was broken (DNS resolution bug). Instead, the DTB was dumped by loading the recovery kernel, letting Linux boot, then reading `/proc/device-tree` from the running system and reconstructing the blob via `dtc`.

**Step 2 — Patch on PC using fdtput:**
```bash
# Inject bootargs into /chosen node
fdtput -ts dtb_emmc.dtb /chosen bootargs \
  "console=ttyS0,115200 root=/dev/mmcblk0p14 rootwait rw selinux=0"

# Clear ext4 feature flags incompatible with kernel 4.1
# (metadata_csum and 64bit — see section 8)
```

**Step 3 — Load from USB on every boot:**
```
# sysinit.txt on USB FAT32 sda1:
load -loader=raw -addr=0x7700000 usbdisk0:dtb_emmc.dtb
setenv DT_ADDRESS 0x7700000
load -loader=raw -addr=0x02208000 flash0.recovery
dt on
go 0x02208800
```

BOLT loads the pre-patched DTB from the USB stick into RAM, sets `DT_ADDRESS` to point at it, then jumps to the kernel. The kernel reads bootargs from the DTB `/chosen` node. No quoting issues, no flash dependency, fully reproducible on every boot.

### 7.3 USB Stick Contents

| File | Purpose |
|---|---|
| `sysinit.txt` | BOLT autoboot script |
| `dtb_emmc.dtb` | Patched vendor DTB — bootargs + ext4 feature flags cleared |

### 7.4 Rescue / Fallback DTB

`dtb_patched.dtb` — same DTB but with `root=/dev/sda2 init=/bin/sh` — kept on the laptop. Used for side-loading maintenance work (mounting eMMC from USB Alpine to edit files without booting the eMMC system). To activate: replace `dtb_emmc.dtb` on the USB stick with this file.

---

## 8. ext4 Compatibility — metadata_csum

Alpine Linux's `mkfs.ext4` defaults to enabling `metadata_csum` and `64bit` features. Linux kernel 4.1.45 does not support these features and cannot mount such a filesystem.

**Symptom:** Kernel log shows `EXT4-fs: couldn't mount as ext3 due to feature incompatibilities` followed by mount failure.

**Fix — format without incompatible features:**
```bash
mkfs.ext4 -O ^metadata_csum,^64bit /dev/mmcblk0p14
```

The `EXT4-fs: couldn't mount as ext3/ext2` lines that appear in every boot log are harmless — the kernel probes ext3 and ext2 compatibility first, fails, then mounts successfully as ext4.

---

## 9. Alpine Linux Installation

### 9.1 Procedure

1. Boot USB Alpine (`dtb_patched.dtb`, `root=/dev/sda2`) to get a live shell with eMMC access
2. Format p14: `mkfs.ext4 -O ^metadata_csum,^64bit /dev/mmcblk0p14`
3. Mount and extract Alpine minirootfs armv7:
```bash
mount /dev/mmcblk0p14 /mnt/p14
tar xf alpine-minirootfs-3.23-armv7.tar.gz -C /mnt/p14
```
4. Install required packages (chroot or post-boot via apk):
   `util-linux`, `lsblk`, `nano`, `openssh`, `busybox-extras`, `python3`, `chrony`, `btop`, `tailscale`
5. Remove `/etc/securetty` (or rename to `.bak`) to allow root serial login
6. Set root password
7. Patch DTB with eMMC bootargs, copy to USB stick
8. Boot from eMMC

### 9.2 Init System — OpenRC Failure

OpenRC was initially installed and configured. It consistently caused boot hangs on this kernel (4.1.45). Root cause: OpenRC's boot runlevel blocked on networking (DHCP) before getty could spawn, and the sysinit runlevel had ordering issues with the 4.1 kernel's cgroup/proc initialization. Multiple inittab configurations were attempted; all caused hangs at different stages.

**Resolution:** OpenRC removed from boot path entirely. Replaced with a minimal busybox init + custom startup script.

### 9.3 Final inittab

```
::sysinit:/etc/startup.sh
::respawn:/sbin/getty -L 115200 ttyS0 vt100
::ctrlaltdel:/sbin/reboot
```

### 9.4 Final startup.sh

```sh
#!/bin/sh
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mkdir -p /dev/pts
mount -t devpts devpts /dev/pts 2>/dev/null || true
ip link set eth0 up
ip addr add 192.168.1.28/24 dev eth0
ip route add default via 192.168.1.1
sleep 3
ntpd -q -n -p pool.ntp.org
nohup tailscaled >/dev/null 2>&1 &
/usr/sbin/sshd
hostname -F /etc/hostname
```

Notes:
- `ntpd` from busybox handles large clock offsets (1970 → 2026) reliably. `chronyd -q` times out on offsets this large.
- `sshd` without `-D` daemonizes itself cleanly and returns, allowing startup.sh to exit and getty to spawn.
- `tailscaled` backgrounded with nohup — survives startup.sh exit.
- `/dev/pts` must be mounted before sshd for PTY allocation to work.

---

## 10. Final System State

### Boot Chain

```
Power on
  → BOLT v1.34 (from mmcblk0boot0, immutable)
  → reads sysinit.txt from USB FAT32 (sda1)
  → loads dtb_emmc.dtb from USB into RAM @ 0x7700000
  → sets DT_ADDRESS=0x7700000
  → loads flash0.recovery (vendor recovery kernel, 32MB) into RAM @ 0x02208000
  → jumps to 0x02208800 (skipping 2KB Android boot image header)
  → Linux 4.1.45 boots with patched DTB
  → mounts mmcblk0p14 as root (ext4)
  → busybox init reads /etc/inittab
  → startup.sh: mounts proc/sys/devpts, configures eth0, syncs NTP, starts sshd+tailscale
  → getty spawns on ttyS0
  → Alpine Linux 3.23 login prompt
```

### What Works

| Feature | Status |
|---|---|
| Fully automated boot (no manual BOLT commands) | ✅ |
| Serial console login (ttyS0, 115200) | ✅ |
| Ethernet (DHCP or static 192.168.1.28) | ✅ |
| SSH (OpenSSH, root login, password auth) | ✅ |
| NTP clock sync | ✅ |
| Tailscale VPN | ✅ |
| lsblk / full eMMC partition visibility | ✅ |
| apk package manager | ✅ |
| All 14 eMMC partitions intact | ✅ |
| USB fallback boot (dtb_patched.dtb on laptop) | ✅ |

### Credentials

- User: `root`
- Login: serial (ttyS0) or SSH (192.168.1.28:22)
- `/etc/securetty` moved to `/etc/securetty.bak`

---

## 11. Security Posture

| Layer | Status |
|---|---|
| BOLT | Fully owned — no auth, Verified Boot ORANGE |
| Secure Boot (BFW/SSBL) | Active — protects BOLT chain only |
| Android SELinux | Irrelevant — Android replaced |
| DM-Verity | Irrelevant — Android replaced |
| RPMB | Status unknown — not investigated |
| TrustZone | Present — `tz` subcommands available in BOLT, not yet explored |

---

## 12. Files

| File | Notes |
|---|---|
| `dtb_emmc.dtb` | Patched DTB for eMMC boot — on USB stick and `~/Downloads/` |
| `dtb_patched.dtb` | Rescue DTB for USB Alpine boot — on laptop only |
| `sysinit.txt` | BOLT autoboot script — on USB stick |
| `dt_bolt.txt` | Full raw DTB dump from BOLT `dt show` |
| `boot.txt` | UART capture — full BOLT + Android boot log |
| `kaon.hardware.kaonsvc@1.0.so` | 165 KB HIDL proxy library — key HAL RE artifact |

---

## 13. Wrong Assumptions Log

| Assumption | Reality |
|---|---|
| `setenv bootargs "..."` works in sysinit.txt | Batch parser strips quotes, only first token assigned — use DTB `/chosen` instead |
| BOLT `dt add prop` can patch DTB in-place | Returns -8, locked — must patch externally |
| `flash0.boot` is the right partition to load | Recovery kernel is in `flash0.recovery` — different offset |
| OpenRC will work on kernel 4.1.45 | Hangs on boot — use busybox init + startup script |
| `mkfs.ext4` defaults are compatible with 4.1 | `metadata_csum` and `64bit` features must be disabled |
| chrony handles large clock offsets | Times out on 1970→2026 offset — use busybox ntpd |
| sshd needs `-D` flag to stay running | Without `-D`, sshd daemonizes itself and is more stable in this init setup |
| BOLT's flash abstraction survives partition reformatting | After wiping flash0.userdata, `android boot`'s internal DTB load path breaks — must load DTB explicitly from USB |
