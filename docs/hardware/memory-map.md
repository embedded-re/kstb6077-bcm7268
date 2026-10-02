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
| `0x02208000` | zImage load address used by `boot/sysinit.txt` |
| `0x06400000–0x0640ffff` | **PSCI** secure monitor (`smm64`), reserved |
| `0x06ffc000–0x09200000` | **BOLT**: FSBL info `0x06ffc000`, page table `0x07000000`, code `0x070080f0`, data, bss, heap `0x07100000–0x09100000`, stack `0x09100000–0x09200000` |
| `0x07613000` | DTB that BOLT passes on `go` (`DT_ADDRESS`, size `0xa53e`) |
| `0x07700000` | DTB load address used by `boot/sysinit.txt` |
| `0x7df00000–0x7dffffff` | **BL31** (ARM Trusted Firmware, EL3), reserved, secure |
| `0x7e000000–0x7fffffff` | **SRR**, 32 MB reserved, secure |

Available to programs (BOLT `rmem`): `0x00000000–0x06400000` and
`0x06410000–0x7df00000`, minus BOLT's own region while BOLT is alive.

## Peripherals

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
| `0xf0460000` | PCIe (Wi-Fi BCM4335) | |
| `0xf0480000` | GENET v5 Ethernet; MDIO at `0xf0480800` | |
| `0xf04d1500` | AVS temperature sensor | `system-blocks.md` |
| `0xf04e0488`, `0xf04e051c–0524` | UART clock gate and clock muxes | `uart.md` |
| `0xf0b00200…` | USB PHY, EHCI/OHCI/xHCI/BDC | |
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
