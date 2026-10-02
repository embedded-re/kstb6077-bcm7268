# Memory map

Physical address map of the KSTB6077 (BCM7268 B0). Sources: BOLT `info` and
`rmem`, BOLT's page table, the original DTB (`boot/original_dtb.dts`), and
the boot log. See `../bolt/bolt.md` for BOLT's page table, decoded entry by
entry.

## RAM (2 GB DDR4 at address 0)

| Range | Contents |
|---|---|
| `0x00000000–0x00000fff` | reserved, no DMA (DTB `reserved-nodma`). Unmapped in BOLT's MMU (NULL guard) |
| `0x01000000–~0x01005200` | bare-metal monitor: code, data, bss, 16 KB stack (`assembly/`) |
| `0x00040000–0x0103ffff` | BOLT's `flash` staging buffer (used only while `flash` runs; `storage.md`) |
| `0x02208000` | zImage load address used by `boot/sysinit.txt` |
| `0x06400000–0x0640ffff` | **PSCI** secure monitor (`smm64`), reserved |
| `0x06ffc000–0x09200000` | **BOLT**: FSBL info `0x06ffc000`, page table `0x07000000`, code `0x070080f0`, data, bss, heap `0x07100000–0x09100000`, stack `0x09100000–0x09200000` |
| `0x07613000` | DTB that BOLT passes on `go` (`DT_ADDRESS`, size `0xa53e`) |
| `0x07700000` | DTB load address used by `boot/sysinit.txt` |
| `0x7db08000–0x7defffff` | `splash0` (stock BOLT `rmem`): display lists `0x7db08000–0x7db0b6ff`, **framebuffer from `0x7db0b700`** (`display.md`) |
| `0x7df00000–0x7dffffff` | **BL31** (ARM Trusted Firmware, EL3), reserved, secure |
| `0x7e000000–0x7fffffff` | **SRR**, 32 MB reserved, secure |

Available to programs (BOLT `rmem`): `0x00000000–0x06400000` and
`0x06410000–0x7df00000`, minus BOLT's own region while BOLT is alive.

## RAM for a bare-metal program after `go -64` (tested 2026-10-02)

Probes and outputs: `../bolt/raw/ram/`. All at EL2 on the modified box.

| Range | Size | After `go -64` |
|---|---|---|
| `0x00000000–0x00000fff` | 4 KB | leave alone: DTB `reserved-nodma` |
| `0x00001000–0x000fffff` | 1 MB | **free**, tested (`ram_gaps_probe.s`) |
| `0x00100000–0x00ffffff` | 15 MB | **free**, tested |
| `0x01000000–0x011fffff` | 2 MB | your program (loaded at `0x01000000` by `kstb-run`) |
| `0x01200000–0x063fffff` | 82 MB | **free**, tested |
| `0x06400000–0x0640ffff` | 64 KB | **PSCI monitor: keep** (needed for `smc`, e.g. starting other cores) |
| `0x06500000–0x06efffff` | 10 MB | **free**, tested |
| `0x06f00000–0x091fffff` | 35 MB | BOLT, **free after `go`**: overwritten completely (page table, code, heap, stack, the DTB at `0x07613000`) and PSCI still answered |
| `0x09200000–0x7d9fffff` | 1,864 MB | **free**, tested (includes the second framebuffer `0x7d600000`) |
| `0x7da00000–0x7db07fff` | 1 MB | **free** without `pcm0`, tested (picture unchanged). With `pcm0`, BOLT puts the audio buffer at `0x7dada100–0x7db08eff` |
| `0x7db08000–0x7db0b6ff` | 14 KB | display lists: **never write** |
| `0x7db0b700–0x7defffff` | 4 MB | the framebuffer |
| `0x7df00000–0x7fffffff` | 33 MB | BL31 + SRR, secure: never read or write |

About **2,008 MB is usable**, all tested: 1,971 MB (`ram_test.s`) + BOLT's 35 MB (`bolt_reuse_probe.s`) + 2 MB (`ram_gaps_probe.s`).

- **RAM test** (`ram_test.s`): every free word above (1,971 MB) written with
  `address ^ 0x5a5a5a5aa5a5a5a5`, checked, then the same inverted.
  **0 errors.** Non-cacheable speed: write 670 MB/s (2.9 s per pass), read
  70 MB/s (28.0 s). Reads need the cache to be fast (`display.md`, "Video speed").
- **Nothing writes RAM by itself after `go -64`** (`ram_scan.s`): two
  read-only passes over all 2 GB, 10 s apart. The only changed 1 MB block was
  the probe's own (`0x01100000`). The display was running (it only reads);
  audio wasn't started (no `pcm0`). With `pcm0`, the audio DMA reads its
  ring buffer; it doesn't write RAM (inferred: it's an output).
- **RAM is not cleared at boot.** After a power-on, almost every 1 MB block
  had no zero word at all: random contents. The low 16 MB (`0x00001000`…)
  hold random words, nothing structured. Clear what you use (`.bss`, buffers).
- **PSCI** (`bolt_reuse_probe.s`): `smc #0` with `x0 = 0x84000000` returns
  `2` = PSCI v0.2, before and after BOLT's RAM was overwritten.
  `PSCI_FEATURES` returns `-1`: it only exists from PSCI 1.0, so that says
  nothing about `CPU_ON` (not tested yet).

## Peripherals

Register by register: `registers.md`. Every known block, with its ID/first
word read from the board: `peripherals.md`.

All peripheral registers are 32 bits wide, on 4-byte-aligned addresses.

| Base | Block | Doc |
|---|---|---|
| `0xf0200000` / `0xf0200200` | SDHCI 0 (SD slot) / SDHCI 1 (eMMC) host registers; config blocks at `+0x100` | |
| `0xf0403000` | system L2 interrupt controller | `interrupts.md` |
| `0xf0404000` | SUN_TOP_CTRL: chip ID, straps, software reset, pin mux | `system-blocks.md` |
| `0xf0408000` / `0xf0409000` | PWM blocks | |
| `0xf040a500` | main GPIO (4 banks) | `gpio.md` |
| `0xf040a6a8` | watchdog | `system-blocks.md` |
| `0xf040c000` | **UART0: console**, 16550, 115200 8N1 | `uart.md` |
| `0xf040d000` / `0xf040e000` | UART1 / UART2, 16550 | `uart.md` |
| `0xf0410000` | AON control (`0x00–0x27` readable; **`0x2c` gives an external abort**); AON SRAM `0xf0410200–0xf04105ff` | `system-blocks.md` |
| `0xf0410700` | AON pin mux | `gpio.md` |
| `0xf0418000` | MSPI (SPI controller) | |
| `0xf0419c00` | AON L2 interrupt controller (`upg_main_aon`) | `interrupts.md` |
| `0xf0419c80` | AON GPIO (2 banks) | `gpio.md` |
| `0xf041a080` | wake timer (seconds counter, 27 MHz) | `system-blocks.md` |
| `0xf0460000` | PCIe (Wi-Fi BCM43570) | |
| `0xf0480000` | GENET v5 Ethernet; MDIO at `0xf0480800` | |
| `0xf04d1500` | AVS temperature sensor | `system-blocks.md` |
| `0xf04e0488`, `0xf04e051c–0524` | UART clock gate and clock muxes | `uart.md` |
| `0xf0604000` | display RDC (register DMA) list pointers | `display.md` |
| `0xf0641000` | display graphics feeder (GFD): width `+0x44`, **surface address `+0x48`**; `+0x5c` aborts | `display.md` |
| `0xf0b00200…` | USB control/PHY `0x200`, EHCI0 `0x300`, OHCI0 `0x400`, EHCI1 `0x500`, OHCI1 `0x600`, xHCI `0x1000`, BDC `0x2000` | `usb.md` |
| `0xffd01000` / `0xffd02000` | GIC distributor / CPU interface | `interrupts.md` |
| `0xffe00000` | boot SRAM (128 KB) | |

## What is reachable under BOLT's MMU

BOLT runs with the MMU on, and programs started with `go` (32-bit) inherit
its page table at `0x07000000`:

| Range | Mapping |
|---|---|
| `0x00001000–0x7fffffff` | Normal memory, write-back cached |
| `0xf0000000–0xf12fffff` | Device memory: the **only** mapped peripheral window |
| `0xffe00000–0xffefffff` | boot SRAM (4 KB pages) |
| everything else, **including the GIC (`0xffd…`)** | unmapped: an access aborts and hangs the board |

Before reading a new address with BOLT's `d`, check that it lies inside a
mapped range. Code started with `go -64` or `boot -64 -el3` runs at a fresh
exception level and reached the UART at its physical address
(`../booting.md`).
