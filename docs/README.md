# KSTB6077 reference manual

A hardware reference for the KaonMedia KSTB6077 set-top box (Broadcom
BCM7268 B0), written from reverse engineering. Broadcom publishes no
datasheet for this chip, so every address, value and procedure here was
either tested on the board or is marked with where it came from.

The product overview, photos and the feature table are in the
[top-level README](../README.md). This page is the table of contents and
explains the conventions used in every chapter.

## Contents

### 1. Getting started

| Chapter | What it covers |
|---|---|
| [Running code on the board](booting.md) | boot chain, reaching `BOLT>`, loading programs (serial, USB, TFTP), `go` / `go -64` / EL3 entry, CPU state at entry, watchdog safety net |

### 2. System reference

| Chapter | What it covers |
|---|---|
| [Memory map](hardware/memory-map.md) | RAM layout, free RAM for programs, peripheral window, boot SRAM, what BOLT's MMU maps |
| [Register sheet](hardware/registers.md) | every register used so far on one page, and the **do-not-touch** list |
| [Peripheral blocks](hardware/peripherals.md) | every known hardware block with its address and first word; switched-off blocks; GISB error capture; the abort-safe probe |

### 3. Hardware blocks

| Chapter | Block |
|---|---|
| [CPU cores](hardware/cpu-cores.md) | 4 × Brahma-B53; starting cores 1–3 with PSCI |
| [Interrupts](hardware/interrupts.md) | GIC-400, Broadcom L2 controllers, every interrupt by ID |
| [System blocks](hardware/system-blocks.md) | temperature, wake timer, watchdog, chip ID, software reset, reset reason, straps, OTP, AON SRAM, generic timer |
| [GPIO](hardware/gpio.md) | LEDs, buttons, pin mux |
| [UARTs](hardware/uart.md) | UART0 (console), UART1, UART2 |
| [I2C](hardware/i2c.md) | five BSC controllers, bus scan |
| [IR receiver](hardware/ir.md) | NEC decoding, the Kaon remote's codes |
| [USB](hardware/usb.md) | EHCI, OHCI, xHCI; which port is which; OHCI from bare metal |
| [Ethernet](hardware/ethernet.md) | GENET v5 and the internal PHY |
| [Storage](hardware/storage.md) | SDHCI controllers, eMMC, data files for bare-metal programs |
| [Display](hardware/display.md) | HDMI framebuffer, vsync and page flipping, display lists (RDC), compositor, graphics feeder, BOLT's splash |
| [2D blitter](hardware/2d-blitter.md) | M2MC: fill, copy, scaling, palette-8 |
| [3D GPU](hardware/gpu.md) | V3D 3.3: power, reset, TFU, render and binner jobs, QPU shaders |
| [Audio](hardware/audio.md) | HDMI audio from BOLT's splash code, S/PDIF |

### 4. Firmware

| Chapter | What it covers |
|---|---|
| [BOLT bootloader](bolt/bolt.md) | identity, memory layout, page table, devices, commands by risk, network, reverse engineering BOLT's code |
| [Stock firmware](stock-firmware.md) | the untouched box: boot chain, boot reasons, BSU commands, partitions, stock kernel, Android 11 over `adb` |
| [Stock drivers as a guide](stock-drivers.md) | getting the stock Broadcom drivers and reading them |
| [Device tree files](hardware/device-tree.md) | which DTB in `boot/` is original, and how they differ |

### Appendices

| Appendix | What it is |
|---|---|
| [Evidence](evidence/README.md) | every probe program and raw capture, and the chapter that uses it |
| [Repository layout and tools](#repository-layout-and-tools) | below |
| [History: the Linux port](history/linux-port.md) | the earlier Alpine / Linux 6.6 work (historical, not maintained) |
| [Idea: second-stage bootloader](ideas/second-stage-bootloader.md) | a proposal, not built |

## Conventions

### How sure is a statement?

Every fact carries its source. Untagged statements in a section marked
*tested* were observed on the board.

| Label | Meaning |
|---|---|
| **tested** | read, written, seen or heard on the board. The chapter names the probe or command and the box |
| **inferred** | a conclusion from tested facts or general knowledge, not checked directly |
| **from the DTB** | from a device tree in [`../boot/`](../boot/) |
| **from BOLT's code** | from the disassembled BOLT image ([`bolt/bolt.md`](bolt/bolt.md) §11) |
| **from Nexus / Linux** | register names and sequences from the stock Broadcom drivers ([`stock-drivers.md`](stock-drivers.md)) or the Linux kernel. Names are used as labels; the values are from the board |
| **not tested** | named somewhere, never tried on the board |

### The two boxes

| Name | What it is |
|---|---|
| **modified box** | the box used for most tests. Its Android was replaced by the [Linux port](history/linux-port.md); `STARTUP` is unset, so it stops at `BOLT>`. A `splash` partition was re-added ([`hardware/display.md`](hardware/display.md)) |
| **stock box** | an untouched second box. It shipped with Android 8.0 and installed an Android 11 update by itself ([`stock-firmware.md`](stock-firmware.md)). BOLT is the same v1.34 on both |

### Notation

| Written | Means |
|---|---|
| `0xf040c000` | a CPU physical address, hexadecimal |
| `+0x14` | an offset from the block's base address named in the same section |
| `7db0b700` (in a "value" column) | a 32-bit register or memory value in hexadecimal, as BOLT's `d -w` prints it |
| bus address `0x2040c000` | the same register as seen by Broadcom's drivers and PSCI messages: CPU `0xfxxxxxxx` = bus `0x2xxxxxxx` ([`hardware/peripherals.md`](hardware/peripherals.md), "Bus addresses") |
| SPI *N* / ID *N* + 32 | device tree interrupt number / GIC interrupt ID |
| bit 0 | the least significant bit |
| KB, MB, GB | 1024, 1024², 1024³ bytes |
| **abort** | a synchronous external abort on that access: the block is absent, switched off or in reset |
| EL2, EL3 | AArch64 exception levels: `go -64` starts a program at EL2, `boot -64 -el3` at EL3 |

All hardware registers are 32 bits wide: access them with `ldr w` / `str w`
and change them with read-modify-write unless the chapter says otherwise.

### Safety

- **Check the do-not-touch list** in the [register sheet](hardware/registers.md)
  before reading or writing a new address. Some reads abort and hang the board.
- **Arm the watchdog** before running experimental code
  ([`booting.md`](booting.md), "Safety net").
- Under BOLT's MMU (BOLT commands and 32-bit `go`), only
  `0xf0000000–0xf12fffff` is mapped: anything else, including the GIC, hangs
  the board ([`bolt/bolt.md`](bolt/bolt.md) §3).
- Some BOLT commands are permanent (`rpmb program-key`, `setenv -p`,
  `flash`, …): see "Commands by risk" in [`bolt/bolt.md`](bolt/bolt.md) §5.

## Repository layout and tools

| Path | Contents |
|---|---|
| [`../README.md`](../README.md) | product overview and feature table |
| [`docs/`](.) | this manual |
| [`evidence/`](evidence/) | probe sources, their outputs, raw console captures ([index](evidence/README.md)) |
| [`../boot/`](../boot/) | device trees ([which is which](hardware/device-tree.md)) and the USB-stick autoboot script `sysinit.txt` |
| [`../assembly/`](../assembly/) | 32-bit bare-metal UART monitor (`boot.s`), `build.sh`, built `bootstrap.bin` / `.elf` |
| [`../c/`](../c/) | bare-metal C example (`main.c`) and its address header (`kstb.h`) |
| `stock/` | stock firmware files, downloaded by `tools/fetch-stock` (local only, not in git) |

| Tool | What it does |
|---|---|
| [`tools/build-a64`](../tools/build-a64) | builds an AArch64 program: `.s` → `.elf` + `.bin` (clang + ld.lld) |
| [`tools/build-c`](../tools/build-c) | builds a bare-metal C program: `.c` → `.elf` + `.bin` |
| [`tools/kstb-run`](../tools/kstb-run) | uploads a binary over the BOLT serial console, CRC-checks it, runs it (`go` / `go -64`) |
| [`tools/kstb-bolt`](../tools/kstb-bolt) | gets the board to `BOLT>` by sending Ctrl-C during boot (power-cycle or `--reset`) |
| [`tools/kstb-dump`](../tools/kstb-dump) | dumps board memory to a file over the BOLT console (read-only) |
| [`tools/make-splash`](../tools/make-splash) | builds a `flash0.splash` container (GZBR/zlib) from an image, optionally with a `pcm0` sound |
| [`tools/make-wad-image`](../tools/make-wad-image) | wraps a data file in a `KWAD` header for the splash partition ([`hardware/storage.md`](hardware/storage.md)) |
| [`tools/fetch-stock`](../tools/fetch-stock) | downloads the stock firmware archive into `stock/` |
| [`tools/re/`](../tools/re/) | analysis helpers: `xref.py` (string → code), `ann.py` (annotated listing), `callers.py`, `ko.py` (kernel modules), `rdc.py` (display lists), `qpu.py` (V3D QPU instructions) |
