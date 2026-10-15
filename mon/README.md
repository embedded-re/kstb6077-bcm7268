# kmon: a memory monitor for the KSTB6077

A small monitor that runs at EL2 (AArch64) after BOLT's `go -64`. It reads and
writes memory and registers by address or by name, survives aborts and
crashes, receives programs over the serial line and starts them. Doom is
stored with it and starts with one command; quitting Doom returns to the
prompt.

Tested on the modified box on 2026-10-15 unless marked otherwise.

## Starting it

The monitor and `doom.bin` are stored together (the "bundle") in
`flash0.splash` at `+0xf00000`, after the WAD ([`../docs/hardware/storage.md`](../docs/hardware/storage.md)).
From `BOLT>`:

```
load -raw -rawfs -offset=0xf00000 -addr=0x00100000 -max=0x1c0000 flash0.splash
go -64 0x00100000
```

or `tools/kstb-mon`, which also has BOLT load the WAD first. Without that,
`doom` reads the WAD from the eMMC itself (14 MB in about 0.4 s, tested). Type into it with
`kstb -i` (raw mode).

| Tool | What it does |
|---|---|
| `tools/kstb-mon` | start from flash (the three lines above) |
| `tools/kstb-mon --tftp` | start a new build over TFTP, without flashing |
| `tools/kstb-mon --install` | TFTP the bundle, write it to `flash0.splash +0xf00000`, read it back, start |
| `tools/kstb-send [--addr A] [--go] file` | send a file to `kmon>` (`load`), optionally start it |

`make -C mon` builds `mon/build/mon.bin` and the bundle `mon/build/kmon.img`
(`mon.bin` padded to 256 KB, then `doom.bin` in a `KWAD` header). For
`--tftp`/`--install`, `kmon.img` has to be in the TFTP server's root.

## Commands

```
help [cmd]                     list
info                           chip, CPU, temperature, uptime, reset reason, what is loaded
regs [filter]                  known names (about 65, from registers.md)
r[.b|.h|.w|.q] addr            read one value; known registers are named and some decoded
w[.b|.h|.w|.q] addr value      write, then read back
md[.b|.h|.w|.q] [addr] [count] dump with ASCII; Enter on an empty line continues
mw[.b|.h|.w|.q] addr value [n] write a value n times
setb / clrb / togb addr bit... set / clear / toggle bits (32-bit read-modify-write)
watch[.b|.h|.w] addr [ms]      print the value whenever it changes, any key stops
fill addr len byte             cp dst src len          cmp a b len
crc addr len                   CRC32, same as BOLT's crc and zlib.crc32
find[.b|.h|.w|.q] addr len value
calc expr                      e.g. calc fb+#3840
load [addr]                    receive a file over serial (tools/kstb-send), default 0x01000000
go [addr]                      start a program like BOLT's go -64 (default: last load)
call addr [x0] [x1]            call code as a function with the monitor's MMU on
doom                           start Doom (loads the WAD from eMMC if needed); quitting returns here
mmc                            eMMC: card status
mmc extcsd | part              the card's EXT_CSD fields | the GPT partition table
mmc dump lba                   hex dump of one sector
mmc read lba count [addr]      sectors to RAM (default 0x02000000)
mmc write lba count addr       RAM to sectors: flash0.splash only (see below)
ir                             live IR remote codes with key names
irq [on|off|timer|uart|sw1|ir|watch|clear]   interrupts (see below)
led green|red|orange|blue|off  front LEDs
wdt [seconds|off]              watchdog
history                        reset    software reset back to BOLT
```

Numbers are hex (`f0404000`, `0x10`) or `#decimal`, and register names work
anywhere a number does: `r temp`, `md fb`, `w led 1340b8`, `md fb+#3840`. A
name wins over hex: `fb` is the framebuffer, `0xfb` the number. Sizes
`.b .h .w .q` = 1, 2, 4, 8 bytes; the default is `.w` (registers are 32-bit).
The line editor has arrows, Home/End, Ctrl-A/E/K/U, Ctrl-C, Up/Down
history and Tab completion of commands and names.

## Safety

- **Aborts are caught** (tested). Every access to an address you typed goes through
  `safe_read`/`safe_write` in `start.S`, which work like the abort-safe probe
  ([`../docs/hardware/peripherals.md`](../docs/hardware/peripherals.md)): a data abort skips the access, and the
  command prints the ESR instead of hanging the board:
  ```
  kmon> r 0x80000000
  80000000: abort (ESR 96000005, data abort, translation fault: not mapped)
  kmon> r 0xf0402800
  f0402800: abort (ESR 96000210, data abort)
  ```
- **Crashes return to the prompt** (tested with `call 0`). Any other exception
  prints its type, ESR, ELR, FAR, SPSR and all registers, then the monitor
  restarts its prompt. The same happens when a program started with `go`
  crashes, unless it has installed its own vectors.
- **"Do not touch" addresses are refused** (tested): the list from
  [`../docs/hardware/registers.md`](../docs/hardware/registers.md) (OTP fuses, the display lists, the HDMI
  registers that break the picture, BL31/SRR, PSCI, the monitor itself). Add
  `!` to the command to do it anyway: `w! …`, `md! …`.
- The watchdog is not armed by the monitor. `wdt 60` arms it, `wdt off`
  stops it.

## Interrupts (`irq`, [`irq.c`](irq.c))

Interrupts at EL2 through the GIC-400, one step at a time (tested
2026-10-15, details in [`../docs/hardware/interrupts.md`](../docs/hardware/interrupts.md), "Taking interrupts at
EL2"). Off at start-up. Everything is switched off again (sources, IMO,
DAIF.I) before `go` and after a crash, both tested, and before `doom` (same
call, not tested there).

```
irq                    GIC, CPU and per-source state: enabled, pending, active, count
irq on [noimo]         open GICC_PMR, set HCR_EL2.IMO, unmask IRQs (noimo: leave IMO at 0)
irq off                every source off, IRQs masked, HCR_EL2 restored
irq timer ms|off       EL2 physical timer, ID 26 (ms in decimal)
irq uart on|off        UART0 receive, ID 102: the console reads a ring buffer, the CPU waits in WFI
irq sw1 on|off         SW1 on both edges: AON GPIO 14 -> AON L2 bit 3 -> ID 98
irq ir on|off          remote: kbd1 -> AON L2 bit 0 -> ID 98
irq watch              print SW1 and IR events, and the timer count once a second
irq clear              zero the counters
```

A source can be enabled before `irq on`: it then shows as pending in the GIC
but isn't taken. `irq uart on` is refused until the timer has been taken
(otherwise WFI might never wake), and the console falls back to polling if
a received byte waits 100 ms without an interrupt.

The IRQ vector ([`start.S`](start.S)) saves x0-x18, x29, x30, ELR and SPSR and calls
`irq_handler`; `irq.c` is built with `-mgeneral-regs-only` so the handler
never touches FP/SIMD registers.

## Trying a new build without flashing (`mon-ram.bin`)

`make -C mon` also builds `mon/build/mon-ram.bin`: [`reload.S`](reload.S), a copy stub,
followed by `mon.bin`. From a running kmon:

```
tools/kstb-send --go mon/build/mon-ram.bin
```

`go` turns the MMU off and runs the stub at `0x01000000`. It copies the new
monitor over the old one at `0x00100000` and starts it fresh (tested: 71 KB
in about 6 s). The Doom image at `0x00140000` stays where it is, and nothing
is written to flash: a reset brings back the stored build.

## eMMC (`mmc`, [`mmc.c`](mmc.c))

kmon has its own eMMC driver (tested, details and measurements in
[`../docs/hardware/storage.md`](../docs/hardware/storage.md), "The eMMC driver"). It uses the card as
BOLT left it (RCA 2, tran state, 8-bit, 50 MHz), so it needs no
initialisation. It reads at ~33 MB/s and writes at ~9 MB/s. LBAs are 512-byte
sectors from the start of the user area; `flash0.splash` is LBA
`#15239168`–`#15273594`, so `flash0.splash + offset` = `#15239168 + offset/512`.

Writes are refused outside `flash0.splash`, and in its first 512 KB (the
splash container BOLT reads at boot) unless the command is `mmc!`. The driver
never sends CMD6, erase or lock commands, so it can't change the card's
boot or partition setup.

```
kmon> mmc read #15239168 #25         the splash container to 0x02000000
kmon> crc 0x02000000 0x30c4          = 3fffa0ed
```

### Updating the stored bundle from kmon (tested)

No BOLT and no TFTP needed: send the new bundle over serial, write it to
`flash0.splash +0xf00000` (= LBA `#15269888`), check the read-back.

```
make -C mon
tools/kstb-send --addr 0x02000000 mon/build/kmon.img      # ~73 s for 820 KB
kmon> mmc write #15269888 #1640 0x02000000                 # 839680 / 512 = 1640 sectors
kmon> mmc read #15269888 #1640 0x04000000
kmon> crc 0x04000000 0xcd000                               # = the file's CRC32
```

Measured: write 1,640 sectors in 26 ms, read back in 28 ms, CRC matched; the
next start from flash ran the new build. Don't type into `kstb -i` during a
`kstb-send`: the bridge mixes it into the data. A stray Ctrl-C (`03`) did
exactly that once, and the CRC check caught it.

## Memory map

| Range | Use |
|---|---|
| `0x00100000–0x0013ffff` | the monitor: code, data, 64 KB stack, page tables (≤ 256 KB, checked by `link.ld`) |
| `0x00140000–` | `doom.bin` in a `KWAD` header (from the bundle) |
| `0x00f00000–0x00ffffff` | the monitor's heap |
| `0x01000000–` | programs: `load`/`go` default, Doom runs here |
| `0x10000000–0x10dc6fff` | the WAD (`KWAD` image), loaded by BOLT |

MMU and caches are on, with the same identity map as Doom
([`../doom/start.S`](../doom/start.S)): DRAM cached, `0x7d600000–0x7dffffff` (framebuffers,
audio ring) non-cacheable, `0x80000000–0xbfffffff` unmapped, `0xc0000000+`
Device. `md` of RAM therefore reads through the cache.

## Programs: `go` and the way back

`go addr` cleans the data cache, turns the MMU and caches off and jumps with
the CPU in the same state as BOLT's `go -64`. Programs built for BOLT
(`tools/build-a64`, `tools/build-c`) therefore run unchanged. On top of that:

```
x1 = 0x4b4d4f4e ('KMON')     started by the monitor
x2 = resume address          jump there with x0 = status to return to kmon>
```

The monitor's resume entry accepts any MMU/cache state: it cleans the caches,
turns the MMU off and sets itself up again. Doom uses this (tested): its
`start.S` keeps x1/x2, and `exit()` (Quit Game) silences its sound and jumps
back. `doom` copies a fresh `doom.bin` from the bundle every time, so Doom can
be started again (tested twice in a row).

## Serial upload (`load`, `tools/kstb-send`)

`load addr` waits for `KLD1`, a u32 size and a u32 CRC32 (little-endian),
then the bytes; it stores them and compares the CRC. Measured: 64 KB in
5.7 s (~11.5 KB/s at 115200 baud; BOLT's `e -w` upload is ~1.3 KB/s); the CRC
matched (tested, and checked again with `crc`). Ctrl-C cancels while it
waits for the header. It refuses targets in the monitor, its heap or outside
RAM.
