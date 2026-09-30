# KaonMedia KSTB6077

Reverse-engineering notes and bare-metal ARM code for the KaonMedia KSTB6077,
an Android TV set-top box built on the Broadcom BCM7268 SoC.

## Board

| | |
|---|---|
| Product | KaonMedia KSTB6077 (Android TV 8.0 set-top box) |
| Board name | `KM_SH368AT` (PCB silkscreen: "DT-DVB-T Android TV rev1.0") |
| SoC | Broadcom **BCM7268 B0**, chip ID `72680010` |
| CPU | 4× Broadcom Brahma-B53 (Cortex-A53, **ARMv8-A**), 1656 MHz, 1 MB L2 |
| RAM | 2 GB DDR4 at 1856 MHz, at physical address `0x00000000` |
| Storage | 7.28 GiB eMMC: user area, two 4 MB boot partitions, 4 MB RPMB |
| Firmware | BOLT v1.34 bootloader, BSP 4.2.5, ARM Trusted Firmware BL31, PSCI v0.2 |

## What the board has

| Function | Hardware | Where |
|---|---|---|
| Serial console | UART0, 16550-compatible, 115200 8N1, at `0xf040c000`; 5-pin header on the PCB | `docs/booting.md` |
| More UARTs | UART1 `0xf040d000`, UART2 `0xf040e000` | `docs/hardware/memory-map.md` |
| Ethernet | GENET v5 at `0xf0480000`, internal BCM7268 PHY (ID `0xae025091`), 100 Mbit/s | `docs/bolt/bolt.md` |
| Wi-Fi | Broadcom BCM4335 on PCIe (`14e4:aa31`); power switched by AON GPIO 21 and 26 | `docs/hardware/gpio.md` |
| Bluetooth | Broadcom USB adapter `0a5c:2045` | |
| USB | 5 host buses (xHCI, EHCI, OHCI); one USB-A port | |
| Video | HDMI, driven by the Broadcom display pipeline (no open driver) | |
| TV tuner | DVB-T tuner with RF input | |
| Power LED (LED1) | green = AON GPIO 18, red = AON GPIO 17; active-low; both on = orange | `docs/hardware/gpio.md` |
| Blue LED (LED3) | AON GPIO 16, active-low | `docs/hardware/gpio.md` |
| Standby button (SW1) | AON GPIO 14, active-low | `docs/hardware/gpio.md` |
| Recovery button (SW4) | AON GPIO 7, active-low | `docs/hardware/gpio.md` |
| Power switch (SW2) | hard power switch | |
| IR receiver (IR1) | receiver only; the SoC's IR block is not enabled by BOLT | `docs/hardware/gpio.md` |
| Temperature sensor | AVS TMON at `0xf04d1500`, °C = (410040 − code × 487) / 1000 | `docs/hardware/system-blocks.md` |
| Seconds counter | wake timer at `0xf041a080`, 27 MHz clock | `docs/hardware/system-blocks.md` |
| Watchdog | `0xf040a6a8`, 27 MHz ticks | `docs/hardware/system-blocks.md` |
| Software reset | SUN_TOP_CTRL `0xf0404304` / `0xf0404308` | `docs/hardware/system-blocks.md` |
| Reset-surviving memory | 1 KB AON SRAM at `0xf0410200` | `docs/hardware/system-blocks.md` |
| Interrupt controller | ARM GICv2 at `0xffd01000`, fed by Broadcom L2 controllers | `docs/hardware/interrupts.md` |

## CPU modes

- BOLT runs in 32-bit mode (AArch32, SVC) with its MMU and caches on.
- `go` starts a program in AArch32. `go -64` starts it in **AArch64 at EL2**,
  and `boot -64 -el3` starts it in **AArch64 at EL3**, as the secure monitor.
- Under BOLT's MMU, peripherals are reachable only in `0xf0000000–0xf12fffff`.
  The GIC (`0xffd…`) is unmapped there, and accessing it hangs the board.

Details: `docs/booting.md`, `docs/hardware/memory-map.md`.

## Repository

| Path | Contents |
|---|---|
| `assembly/` | bare-metal UART monitor (`boot.s`), `build.sh`, built `bootstrap.bin`/`.elf` |
| `boot/original_dtb.dts` | the original vendor device tree |
| `boot/dtb.dtb`, `boot/Decompiled_dtb.dts` | patched device tree from the Linux port |
| `boot/sysinit.txt` | BOLT autoboot script for the USB stick |
| `docs/booting.md` | how to load and run code: USB stick, TFTP, 32/64-bit, watchdog safety net |
| `docs/hardware/` | memory map, GPIO, interrupts, system blocks, device-tree provenance |
| `docs/bolt/bolt.md` | the BOLT bootloader: memory layout, page table, devices, commands by risk |
| `docs/bolt/raw/` | raw BOLT console captures, including `rescue.txt` |
| `docs/ideas/second-stage-bootloader.md` | proposal: a shim between BOLT and Linux |
| `docs/history/linux-port.md` | the earlier Alpine / Linux 6.6 port |
| `docs/images/` | PCB photo, Android recovery screenshot |

## Quick start

```
assembly/build.sh
# copy assembly/bootstrap.bin to a FAT32 USB stick as boot.bin, then at BOLT>:
load -loader=raw -addr=0x01000000 usbdisk0:boot.bin
go 0x01000000
```
