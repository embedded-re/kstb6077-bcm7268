# Register sheet: every known address and what it does

One page with every hardware register used so far on the KSTB6077, and every
RAM area that matters. Details and evidence are in the doc named in each
heading. Every entry was read or written on the board unless marked
(not tested): those come from the DTB / Linux / BOLT's code.

All hardware registers are **32 bits**: access them with `ldr w`/`str w`
(AArch64) or `ldr`/`str` (AArch32). Change them with read-modify-write unless
the register says otherwise. From BOLT: `d -w <addr> 4` reads one word,
`e -w <addr> <value>` writes one.

## Do not touch

| Address | Why |
|---|---|
| `0xf041002c` | AON control: **external abort** on read (hangs the board) |
| `0xf0404030`, `0xf0404034`, `0xf0404520` | OTP fuses: read only; never write |
| `0xf064105c` | display GFD: **external abort** on read |
| `0x7db08000–0x7db0b6ff` | display lists in RAM: overwriting them **blanks the screen** until reboot |
| `0xf06fa828`, `0xf06fa884`, `0xf06fa888`, `0xf06fa898` | HDMI audio packet set-up: writing them **broke the picture** |
| `0xf0460000` (PCIe: reset by `0xf0469210` bit 1), `0xf0b02000` (USB BDC: reset by `0xf0b00234` bit 23), `0xf1200000–0xf120bfff` (V3D GPU: power island `0xf041d020`), `0xf0402800` (nothing there) | **external abort** on read while in reset / off (`peripherals.md`) |
| any address not on this sheet | unknown: may abort. Read new ones one word at a time, watchdog armed, or with the abort-safe probe (`peripherals.md`) |
| under BOLT's 32-bit `go`: anything outside `0xf0000000–0xf12fffff` | unmapped in BOLT's MMU (the GIC hung the board this way) |

## RAM (`memory-map.md`)

| Address | What |
|---|---|
| `0x00040000–0x0103ffff` | BOLT's `flash` staging buffer (only while `flash` runs) |
| `0x01000000` | where `kstb-run` loads your program (`go -64` entry) |
| `0x06400000–0x0640ffff` | PSCI monitor `smm64` (EL3): don't overwrite. `smc #0` calls it: version, CPU_ON (`cpu-cores.md`) |
| `0x06ffc000–0x09200000` | BOLT (page table `0x07000000`, code, heap, stack). **Free after `go -64`** |
| `0x10000000…` | free RAM for programs (the WAD is loaded here, `storage.md`). All free RAM: `memory-map.md`, "RAM for a bare-metal program" (~2,000 MB, tested) |
| `0x7d600000–0x7d9f47ff` | second framebuffer for double buffering (free `rmem`) |
| `0x7db08000–0x7db0b6ff` | display lists: never write |
| `0x7db0b700–0x7defffff` | **framebuffer**: pixel(x, y) = `0x7db0b700 + y × 3840 + x × 2`, RGB565 |
| `0x7df00000–0x7dffffff` | BL31 (secure firmware): don't touch |
| `0x7e000000–0x7fffffff` | SRR (secure): don't touch |

## UART0, the console: `0xf040c000` (`uart.md`)

16550, registers 4 bytes apart, 115200 8N1 set by BOLT (81 MHz clock, divisor 44).
UART1 `0xf040d000` and UART2 `0xf040e000` have the same layout (pins unknown).

| Address | Name | Read | Write |
|---|---|---|---|
| `0xf040c000` | RBR / THR | the oldest received byte (removes it) | a byte to send |
| `0xf040c004` | IER | which events interrupt (BOLT: `0`, polled) | same |
| `0xf040c008` | IIR / FCR | interrupt ID, FIFO state (`c1`) | FIFO control |
| `0xf040c00c` | LCR | frame format: `03` = 8N1; bit 7 = DLAB (divisor access) | same. Caution: Setting DLAB on UART0 garbles the console |
| `0xf040c010` | MCR | bit 4 = internal loopback (tested on UART1/2) | same |
| `0xf040c014` | **LSR** | bit 0 = byte received, bit 5 = room to send, bit 6 = all sent | — |
| `0xf040c018` | MSR | bit 4 = CTS (`10`) | — |
| `0xf04e0488` | UART clock gate | bits 4/5/6 = UART0/1/2, 1 = **off**; reads `0` | — |

## AON GPIO, LEDs and buttons: `0xf0419c80` (`gpio.md`)

Bank 0 (28 pins). Bank 1 at `0xf0419ca0`. Main GPIO (4 banks) at `0xf040a500`.

| Address | Name | What it does |
|---|---|---|
| `0xf0419c80` | ODEN | 1 = open-drain output. BOLT: `00060000` |
| `0xf0419c84` | **DATA** | read: pin levels. Write: output values. BOLT: `001340b8` |
| `0xf0419c88` | IODIR | 1 = input, 0 = output. BOLT: `0ff9ffff` |
| `0xf0419c8c` / `90` / `98` | EC / EI / LEVEL | interrupt trigger type (not tested) |
| `0xf0419c94` | MASK | interrupt enable (bit 14 set for SW1) |
| `0xf0419c9c` | STAT | changes latched even while masked; **write 1 to clear** |

| Bit of DATA | Function |
|---|---|
| 18 | LED1 **green**, 0 = on |
| 17 | LED1 **red**, 0 = on (green + red = orange) |
| 16 | LED3 **blue**, 0 = on. Input after BOLT: set ODEN, DATA, then clear IODIR |
| 14 | **SW1** front button, 0 = pressed |
| 7 | **SW4** recovery / BT-pairing button, 0 = pressed |
| 21, 26 | Wi-Fi / WLAN power (DTB) (not tested) |

## System blocks (`system-blocks.md`)

| Address | Name | What it does |
|---|---|---|
| `0xf0404000` | chip family ID | `72680010` = BCM7268 B0 |
| `0xf0404004` | chip product ID | `72680010` |
| `0xf040401c` / `20` | straps | `00000f1e` / `0`: bits 0–4 = boot device (`0x1e` = eMMC), bit 9 = SATA off / PCIe on |
| `0xf0404030` / `34`, `0xf0404520` | **OTP fuses**: read only, never write | `40` / `00a02000` / `0`; field names in `system-blocks.md` |
| `0xf041006c` | **reset history** (`RR:`) | bit 0 power-on, 6 watchdog, 9 software; BOLT clears it at boot |
| `0x07069844` | BOLT's copy of the reset history (RAM) | `d -w 0x07069844 1` at `BOLT>` = last reset reason |
| `0xf0404304` | reset source enable | write `1` first |
| `0xf0404308` | **software reset** | write `1` → the board reboots at once (`RR:00000200`) |
| `0xf040a6a8` | watchdog TIMEOUT | 27 MHz ticks: `0x30479e80` = 30 s, `0x202fbf00` = 20 s, `0x1017df80` = 10 s |
| `0xf040a6ac` | watchdog CMD | start: `ff00` then `00ff`. Stop: `ee00` then `00ee`. Read: ticks left |
| `0xf041a080` | wake timer EVENT | bit 0 = 1 when COUNTER reaches ALARM; write `1` to clear |
| `0xf041a084` | wake timer COUNTER | seconds since power-on |
| `0xf041a088` | wake timer ALARM | seconds value that sets EVENT |
| `0xf041a08c` | wake timer PRESCALER | `019bfcc0` = 27 MHz |
| `0xf041a090` | wake timer PRESCALER_VAL | sub-second countdown |
| `0xf04d1500` | temperature | bit 11 = valid, bits 10:1 = code; m°C = `410040 − code × 487` |
| `0xf0410000–0xf0410027` | AON control | readable (`0x2c` aborts) |
| `0xf0410200–0xf04105ff` | **AON SRAM**, 1 KB | survives software and watchdog resets (use the end; Linux uses the start) |

## GISB bus arbiter (`peripherals.md`)

| Address | Name | What it does |
|---|---|---|
| `0xf04007ec` | error capture address | address of the last failed (aborted) access; survives at `BOLT>` |
| `0xf04007f8` | error capture master | one bit per master: `40` = bit 6 = the CPU |
| `0xf04007f4` | error capture status | `0000083d` after a failed read |
| `0xf0400000–0x1e4`, `0xf04007e4–0x7fc` | readable | `0xf04001e8–0x7e0` abort |

## V3D GPU power (`peripherals.md`)

| Address | Name | What it does |
|---|---|---|
| `0xf041d020` | V3D power island control | write `1d00` → wait `(v & 0x74000000) == 0x34000000` (1.6 ms) = **on**; write `b00` → wait `(v & 0x72000000) == 0x42000000` = off. After BOLT: `424e0908` (off) |
| `0xf120000c` | V3D hub IDENT1 | `000e1133` = V3D 3.3 (only while powered) |

## IR receiver kbd1: `0xf0419900` (`ir.md`)

kbd2 at `0xf0419980`, kbd3 at `0xf0419a00`, same layout. Names from Nexus `BKIR_*`.

| Address | Name | What it does |
|---|---|---|
| `0xf0419900` | STATUS | bit 0 = code ready; clear by writing it back with bit 0 = 0. `55` first frame, `37` repeat |
| `0xf0419910` | **DATA0** | received code, e.g. `ea150820` = OK |
| `0xf0419914` | CMD | BOLT: `7`. bit 4 CIR on, bit 5 interrupt enable; NEC on = `37` |
| `0xf0419918` / `1c` | CIR_ADDR / CIR_DATA | timing parameter index / value (27 written for NEC) |

## Interrupts (`interrupts.md`)

| Address | Name | What it does |
|---|---|---|
| `0xf0419c00` | AON L2 STATUS | raw sources: **bit 0 IR kbd1**, bits 1–2 IR kbd2–3, **bit 3 AON GPIO**, 4 LED/keypad |
| `0xf0419c04` | AON L2 MASK_STATUS | BOLT: `7f` (all masked) |
| `0xf0419c08` / `0c` | AON L2 MASK_SET / MASK_CLEAR | from Linux, not written (not tested) |
| `0xffd01000` | GIC-400 distributor | readable from AArch64 EL2/EL3 only. `GICD_CTLR` `+0x000`, `TYPER` `+0x004` |
| `0xffd01100…` | ISENABLER*n* | enable interrupt IDs (32 per word); ID 98 = `0xffd0110c` bit 2 (not tested) |
| `0xffd01800…` | ITARGETSR | which CPU gets each ID (all `0` at hand-off) |
| `0xffd02000` | GIC CPU interface | `GICC_CTLR` `+0x000` |

GIC IDs: AON L2 (SW1, buttons) 98, UART0 102, UART1 103, UART2 104, EHCI0 122,
OHCI0 123, xHCI 124, EHCI1 126, OHCI1 127; generic timers 26/27/29/30.

## Display (`display.md`)

| Address | Name | What it does |
|---|---|---|
| `0xf0641044` | GFD source width | `780` = 1920. 960 → only the left half is sent, the rest shows the background. Keeps a direct write |
| `0xf0641048` | **GFD surface address** | where the visible picture is: `7db0b700` (read it, don't hard-code) |
| `0xf0641058` | GFD pitch | `f00` = 3840 bytes (written by the display lists) |
| `0xf0641174` | GFD height | `438` = 1080 (written by the display lists) |
| `0xf0603484` | **frame counter** | counts up once per frame, 59.94 Hz |
| `0xf0603488` + `0xf060348c` | **flip** | write a buffer address to both → it's shown from the next frame, tear-free |
| `0xf0604000` | RDC descriptor 0 list address | runs BOLT's 73-word list `0x7db09ae0` every frame. Reads back the *previous* written value. Switch: `0xf0605000`=0, address, `0xf0605000`=1 |
| `0xf0645810` | CMP0 background colour | `00 Y Cb Cr`, BOLT `0018b87b` (blue). Rewritten every frame: change it through an own list |
| `0xf0645988` | CMP0 graphics window size | `w << 16 \| h`, BOLT `07800438`. Own list only |
| `0xf064598c` | CMP0 graphics window position | `x << 16 \| y`, BOLT `0`. Own list only |

The 590 registers the display lists use all read safely; see `display.md`, "Display lists (RDC)".

## Audio, HDMI (`audio.md`)

Only after a boot whose splash container has `pcm0` (otherwise these blocks
may not be set up (not tested)).

| Address | Name | What it does |
|---|---|---|
| `0xf0ca00c0` | DMA run | `1` = play, `0` = **stop** |
| `0xf0ca0800` | ring 0 read pointer | |
| `0xf0ca0804` | ring 0 write pointer | |
| `0xf0ca0808` | ring 0 start | |
| `0xf0ca080c` | ring 0 end (= start + length − 1) | can be changed live |
| `0xf06fa0c8` | HDMI audio N | `0x040016c0` (N = 5824; keep the top bits) |
| `0xf06fa0cc`, `0xf06fa0d0` | HDMI audio CTS | `0x00022551` (140625) |
| `0xf0cb0300` | HDMI audio output port | bit 31 cleared = TV goes silent |

## USB (`usb.md`)

The USB-A port = EHCI1 (high speed, e.g. USB sticks) + OHCI1 (low/full
speed, e.g. keyboards). The internal Bluetooth adapter = EHCI0 + OHCI0.
`go` resets all controllers; the PHY stays up.

| Address | Name | What it does |
|---|---|---|
| `0xf0b00300` / `0xf0b00500` | EHCI0 / EHCI1 capabilities | `01000010`: v1.0, op regs at +0x10 |
| `+0x10` (`0xf0b00310` / `510`) | EHCI USBCMD | bit 0 = run |
| `+0x14` | EHCI USBSTS | bit 12 = halted |
| `+0x50` | EHCI CONFIGFLAG | 1 = ports routed to EHCI, 0 = to the OHCI |
| `+0x54` | EHCI PORTSC | bit 0 = connected, bit 12 = power, bit 13 = owner is the OHCI |
| `0xf0b00400` / `0xf0b00600` | OHCI0 / OHCI1 HcRevision | `110` |
| `+0x04` | HcControl | bits 7:6 = state: `0` reset, `0x80` operational |
| `+0x08` | HcCommandStatus | bit 0 = controller reset |
| `+0x18` | HcHCCA | address of a 256-byte RAM area for the controller |
| `+0x34` / `+0x40` | HcFmInterval / HcPeriodicStart | frame timing: `27782edf` / `2a2f` |
| `+0x3c` | HcFmNumber | frame counter, +1 per ms |
| `+0x50` | HcRhStatus | `10000` = power the ports |
| `+0x54` | HcRhPortStatus1 | bit 0 connected, 1 enabled, 8 powered, 9 low speed; write `100` = power on, `10` = reset |
| `0xf0b01000` | xHCI capabilities | `01000020`: v1.0, op regs at +0x20, 2 ports |
| `0xf0b01020` / `1024` | xHCI USBCMD / USBSTS | |
| `0xf0b01420` / `1430` | xHCI PORTSC 1 / 2 | (no device seen on either yet) |

## Not addresses: CPU registers (for comparison)

These live **inside the CPU**. They have names, no addresses, and no
`ldr`/`str` reaches them: use `mrs`/`msr` (AArch64). Values at EL2 after
`go -64` (`../bolt/bolt.md` §10).

| Name | Value | What it is |
|---|---|---|
| `CurrentEL` | `0x8` | exception level 2 |
| `MIDR_EL1` | `420f1000` | Broadcom Brahma-B53 core |
| `MPIDR_EL1` | `80000000` | core 0 |
| `CNTFRQ_EL0` | `019bfcc0` | generic timer frequency, 27 MHz |
| `CNTPCT_EL0` | counting | generic timer count (27 per µs) |
| `SCTLR_EL2` | `30c50830` | MMU and caches off (`30c51835` with them on) |
| `HCR_EL2` | `80000002` | hypervisor configuration |
| `VBAR_EL2` | garbage | exception vector table address: set your own |
