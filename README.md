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
| `boot/original_dtb.dts` | **Original vendor DTB as dumped by BOLT `dt show`** (extracted from `rescue.txt`) — the one to trust |
| `boot/dtb.dtb` / `Decompiled_dtb.dts` | **Patched** variant (injected bootargs, renamed USB nodes) — NOT the original |
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

The canonical device tree is the one BOLT was running when `rescue.txt` was
captured — dumped live via `dt show` (BOLT: `DT_SIZE a53e` = 42,258 B). It is
reproduced exactly in `boot/original_dtb.dts`. Its `chosen` node is vendor-stock:

```
chosen {
    stdout-path = "/rdb/serial@f040c000:115200";
};
```

No `bootargs`, no `no-map`, no `bl31` node. This is the tree to trust for
bare-metal work.

`boot/dtb.dtb` (`Decompiled_dtb.dts`) is a **later, patched variant**, not the
original — differences vs `original_dtb.dts`:

- `chosen` has an injected kernel cmdline (`root=/dev/mmcblk1p9 ... initcall_debug`)
- phantom root nodes `0x80000000{}`, `0x00000000{}`, `reg{}`, `memory{}`
- USB nodes renamed: `usb@`→`usb-phy@`, `ehci@`→`ehci_v2@`, `ohci@`→`ohci_v2@`, `bdc@`→`bdc_v2@`
- blob size differs (0x9af7 vs the resident 0xa53e)

Other patched derivatives seen elsewhere, also not originals:

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