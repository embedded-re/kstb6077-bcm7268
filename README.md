# KaonMedia KSTB6077 — Bare-Metal Reverse Engineering

**Author:** embedded-re
**Repo:** github.com/embedded-re/KAON-KSTB6077

---

## Status

The Linux port is **abandoned**. It was given up on **because of IRQ interrupts** —
interrupt handling under the vendor kernel (4.1.45) and Broadcom DTB
(`brcm,brahma-b53`, L2 intc chain) never came together, so ownership moved to
bare metal where there is full control over the CPU, the GIC, and interrupt
routing with no PSCI / DT indirection in the way.

Bare-metal assembly is now the **primary work**. Current milestone: a UART
monitor in `assembly/boot.s` running from BOLT via raw `go` jump.

## Why bare metal

- Kernel/DTS interrupt path (GIC + `brcm,l2-intc` set-up by the vendor DT boot)
  was a persistent dead end
- Program the GIC directly instead — no `enable-method`, no PSCI
- See `LINUX_PORT.md` for the abandoned Alpine migration history

## Files

| Path | Notes |
|---|---|
| `rescue.txt` | BOLT session dumps — printenv, help, dt show, boot logs. Key reference |
| `boot/Decompiled_dtb.dts` | **Original vendor DTB decompiled** (dtc) — priority reference |
| `boot/dtb.dtb` | Original vendor DTB blob — same tree as the `.dts` above |
| `boot/sysinit.txt` | BOLT autoboot script (USB stick) |
| `assembly/boot.s` | Bare-metal UART monitor (BCM7268, UART @ 0xf040c000) |
| `assembly/bootstrap.elf` / `.bin` | Built monitor images |
| `docs/` | PCB layout + Android recovery screenshots |

## Interrupt hardware (from the DTS)

| Device | Address | Notes |
|---|---|---|
| GIC (`arm,cortex-a15-gic`) | dist `0xffd01000`, cpu `0xffd02000` | root interrupt-parent, 3 cells — program directly |
| `brcm,l2-intc` | `0xf0403000` (sys), `0xf0201000`, `0xf0201a00` (hif-spi), `0xf0410640`, `0xf04d1200`, `0xf040a600` | soc-level interrupt muxes |
| GPIO intc | `0xf040a500` / `0xf0419c80` | `brcm,brcmstb-gpio` banks |
| CPUs | `enable-method "brcm,brahma-b53"` | bypassed in bare metal — GIC programmed directly |

SoC: Broadcom **BCM7268b0** (4x Cortex-A53 "B53"). System console UART
**0xf040c000** (ttyS0, 115200 8N1). RAM 2 GB.

## DTS provenance ("which one is original?")

`boot/dtb.dtb` and `boot/Decompiled_dtb.dts` are the **same device tree** — the
vendor DT. bootargs:

```
console=ttyS0,115200n8 earlycon=uart8250,mmio32,0xf040c000 root=/dev/mmcblk1p9 rootwait rootdelay=5 rw brcm_cma=64M bmem=256M@1024M selinux=0 initcall_debug
```

No `no-map`, no `bl31` node. This is the one to trust for bare-metal work.

Other DTS/DTB files seen elsewhere are **patched derivatives**, not originals:

- `dtb_working_fixed.dts` — original bootargs + `init=/bin/sh` (USB rescue variant)
- `dtb_usb_debian.dts` — `root=/dev/sda2` + `no-map`/`bl31@7df00000` memory reservations (Debian-from-USB variant)

## Boot chain (reference)

```
BOLT v1.34 (mmcblk0boot0, immutable)
  → sysinit.txt from USB FAT32 (boot/sysinit.txt)
  → load dtb.dtb + zImage, setenv DT_ADDRESS
  → go <addr> — raw jump, skipping the 2 KB image header
```

The bare-metal monitor loads the same way: BOLT `load` + `go`, then the GIC is
programmed from `assembly/boot.s`.