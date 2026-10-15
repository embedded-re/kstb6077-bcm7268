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
load -raw -rawfs -offset=0x100000 -addr=0x10000000 -max=0xdc7000 flash0.splash
load -raw -rawfs -offset=0xf00000 -addr=0x00100000 -max=0x1c0000 flash0.splash
go -64 0x00100000
```

or `tools/kstb-mon`, which sends the same three commands. Type into it with
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
doom                           start Doom; quitting from its menu returns here
ir                             live IR remote codes with key names
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
