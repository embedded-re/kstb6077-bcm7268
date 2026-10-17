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
| `tools/kstb-mon --install` | TFTP the bundle, write it to `flash0.splash +0xf00000`, read it back, start. This overwrites the golden image without its kboot header: kboot then warns about a CRC mismatch. Use kmon `slot` instead ([`../kboot/README.md`](../kboot/README.md)) |
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
send addr len                  send RAM over serial (tools/kstb-recv)
video [init]                   HDMI video: status, set up the display ourselves (see below)
audio [init|stop]              HDMI sound: status, start, silence (see below)
beep [hz [ms]]                 a test tone (decimal; default 440 Hz, 500 ms)
play [file.wav]                play a WAV from flash0.DATA, or the last load/fget/tftp get
net [up|stop|dhcp|ip a.b.c.d]  Ethernet: status, start/stop, DHCP or a fixed address (see below)
ping a.b.c.d [count]           ICMP echo; kmon also answers pings while it waits at the prompt
tftp get host:file [addr]      file from a TFTP server to RAM (default 0x01000000)
tftp put host:file [addr len]  RAM to a file on the server (default: the last load/fget)
slot [a|golden|loader|retry|clear]  images for kboot: show (CRC-checked), write (../kboot/README.md)
go [addr]                      start a program like BOLT's go -64 (default: last load)
call addr [x0] [x1]            call code as a function with the monitor's MMU on
doom                           start Doom (loads the WAD from eMMC if needed); quitting returns here
mmc                            eMMC: card status
mmc init                       reset the eMMC host and card, set them up again (8-bit, 50 MHz)
mmc extcsd | part              the card's EXT_CSD fields | the GPT partition table
mmc dump lba                   hex dump of one sector
mmc read lba count [addr]      sectors to RAM (default 0x02000000)
mmc write lba count addr       RAM to sectors: flash0.splash only (see below)
ir                             live IR remote codes with key names
irq [on|off|timer|uart|sw1|ir|watch|clear]   interrupts (see below)
cpu [on core addr [context]]   cores 1-3: state, start one at addr (PSCI, see below)
usb [init|kbd|stop|disk|read]  USB-A port: devices, keyboard, the USB stick (see below)
con [on|off|clear]             text console on the HDMI screen (see below)
fs [usb:]                      FAT32 size and free space: flash0.DATA, or the USB stick
ls [dir]                       list a directory; a path with usb: is on the USB stick
fget path [addr]               read a file to RAM (default 0x01000000); then `go` runs it
fput path [addr len]           write RAM to a file, create or replace (default: the last load/fget)
rm path / mkdir path           delete a file or empty directory / make a directory
run path                       fget a program to 0x01000000 and go
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

## Cores 1–3 (`cpu`)

`cpu` asks the EL3 monitor (kboot's or smm64) for each core's state
(AFFINITY_INFO) and which monitor it is. `cpu on 2 0x01000000 0x22` starts
core 2 at `0x01000000` with `x0` = `0x22` (kboot's monitor; smm64 passes 0).
The core starts at EL2 with its MMU and caches off, so `cpu on` cleans the
data cache first: code just loaded through kmon's cache is then in RAM. Its
writes go straight to RAM; to read them fresh from kmon, `call` any `ret`
first (it cleans and invalidates the cache). A core stops itself with
CPU_OFF (`0x84000002`, `smc #0`); `cpu` then shows it off. Details and
tests: [`../docs/hardware/cpu-cores.md`](../docs/hardware/cpu-cores.md).

## USB keyboard and hub (`usb`, [`../usb/usb.c`](../usb/usb.c))

OHCI1, the USB-A port's low/full-speed controller, polled (tested
2026-10-15, details in [`../docs/hardware/usb.md`](../docs/hardware/usb.md)).
A hub in the USB-A port works (tested 2026-10-17, up to 8 devices, hubs 2
deep): every device on it is enumerated, the first boot keyboard and the
first boot mouse are polled (tested with a Dell mouse `413c:301a`), and devices plugged into or out of the hub are found and
forgotten while kmon runs (one line each). The first USB stick (bulk-only
mass storage) can be read: blocks with `usb read`, files with a `usb:` path
([files](#files-on-flash0data-and-the-usb-stick-fat32-fat)).

```
usb            controller and port state, the devices and where they are
usb init       reset every USB host controller (USB_PM, not BOLT's state) and the port,
               enumerate the device (and behind a hub, every device), print the
               descriptors; a boot-protocol keyboard is then polled
usb kbd        print keyboard reports and the keys (US layout); any serial key stops
usb mouse      print mouse reports: buttons, movement, running sum; any key stops
usb stop       controller back to reset (also done before go/doom)
usb disk       set up the USB stick (INQUIRY, wait until ready, READ CAPACITY), print its partitions
usb read lba count [addr]   blocks of the stick to RAM (default 0x02000000), speed and CRC
```

```
kmon> usb disk
usb: mass storage device 2: SanDisk Ultra 1.00, type 00, removable
usb: mass storage: 488374272 blocks of 512 bytes (238464 MB)
partition 1: type 0c (FAT32), LBA #2048, #1048576 blocks (512 MB)
partition 2: type 83, LBA #1050624, #487323648 blocks (237951 MB)
kmon> usb read #2048 #8192
read 8192 blocks (4096 KB) LBA 2048 to 02000000 in 4417 ms (927 KB/s), CRC 8c6400c2
```

```
kmon> usb init
usb: port 00130303: device connected, low speed, enabled yes
usb: device 0461:0010, USB 1.10, class 00, ep0 max packet 8, 1 configuration(s)
...
usb: keyboard ready (interface 0, endpoint 1 IN, 8 bytes, every 10 ms)
kmon> usb kbd
  22082 ms: 00 00 0b 00 00 00 00 00  'h'
```

## On the TV with a USB keyboard (tested 2026-10-15)

kmon needs no PC once it runs. At start (and when a program returns) it:

- takes over the HDMI screen: a 114 x 32 text console ([`fbcon.c`](fbcon.c)) in
  BOLT's splash framebuffer, anti-aliased Source Code Pro 16 x 32 px
  ([`font.h`](font.h), from [`../tools/make-font`](../tools/make-font)), with a margin for TV overscan. Everything
  printed goes to the screen and the serial line.
- sets up a USB keyboard on the USB-A port or behind a hub, if one is there,
  and checks every 200 ms for one being plugged in. It types into the prompt like the serial
  line: arrows, Home/End/Delete, Tab completion, Ctrl keys; a held key repeats
  (500 ms, then every 40 ms). Keys are a US layout.

Scrollback: the last 1024 lines. **Alt+Up/Down** scrolls a line,
**Shift+PgUp/PgDn** (or Alt+PgUp/PgDn) a page; a marker shows how far back;
typing or new output returns to the bottom.

A scroll redraws every cell from the text (about 8 ms). Moving the pixels
instead takes 98 ms, because the framebuffer is uncached and reading it is
slow. The screen is updated before the serial line, so a host tool waiting
for a prompt (`kstb-send`) never sends while the screen is still busy.

## Files on `flash0.DATA` and the USB stick (FAT32, [`../fat/`](../fat/))

The eMMC's 6.3 GB `DATA` partition ([`../docs/hardware/storage.md`](../docs/hardware/storage.md)) holds
files. Paths are `/`-separated from the root, names match without regard to
case (long names from a PC too); new names must be 8.3 (`doom1.wad`,
`games/save0.dsg`). Getting a file onto the box from the PC:

```
tools/kstb-send --addr 0x02000000 file.bin     # into RAM over serial
kmon> fput file.bin                             # the last load: to flash0.DATA
kmon> run prog.bin                              # later, with no PC: read it and start it
```

Measured: 14.4 MB written in 1.2 s, read in 0.44 s. kmon's own RAM areas
(`0x00600000–0x0084ffff`) and the heap are refused as `fget` targets.

**The USB stick:** a path that starts with `usb:` is on the stick's first
FAT32 partition (MBR type `0b`/`0c`), or on the whole stick when block 0 is
itself a FAT32 boot sector. `fs`, `ls`, `fget` and `run` work on it; the
stick is set up on first use. Tested 2026-10-17 (SanDisk Ultra behind a hub,
full speed on OHCI1):

```
kmon> fs usb:
USB stick (LBA #2048-#1050623): FAT32, 130811 clusters of 4 KB, 439 MB free of 510 MB
kmon> ls usb:/
kmon> fget usb:/hi_kernel.uImage 0x03000000
/hi_kernel.uImage: 7860224 bytes at 03000000, CRC 44757325, 8300 ms
```

That file, sent to the PC with `tftp put`, has a good uImage header and data
CRC; `usb:/boot.bin` has the CRC of the same file on the PC (`0082c7e8`).
About 0.95 MB/s (USB full speed). Writing (`fput`, `rm`, `mkdir` with
`usb:`) stays inside that FAT32 partition: a block outside it is refused.
Tested 2026-10-17: `mkdir usb:/kmontest`, `fput` of 4 MB (6.5 s, about
650 KB/s), `fget` back with the same CRC `d55a7cab`, an old file unchanged,
then `rm` of both; compared with a copy taken before, both FATs were the
same, the root directory's entry slot was free again, and FSInfo's free
count was `ffffffff` (unknown; a PC counts again).

Copying a file from the stick to `DATA`, without the PC:

```
kmon> fget usb:/doom1.wad 0x02000000
kmon> fput doom1.wad                 # the last fget
```

## Video (`video`, [`video.c`](video.c))

When BOLT skipped its boot splash, the HDMI output is dark and nothing runs
the display. `video init` sets it up the way BOLT's splash does: it takes
BOLT's display script, writes its 1381 registers and starts the display
lists from kmon's own memory (`0x7d9f8000`). The script is built into kmon
([`script.S`](script.S), [`bolt_splashdata.dat`](bolt_splashdata.dat): BOLT's
RAM `0x07054318–0x07059347`, CRC `0ed66aea`, checked before use), so kmon
doesn't read BOLT's RAM for it. At start-up kmon
does this by itself when the frame counter stands still. With the splash,
it leaves the display alone. Details:
[`../docs/hardware/display.md`](../docs/hardware/display.md), "Our own display start".

```
kmon> video                 display running?, TV on the cable?, RDC descriptors, built-in script
kmon> video init            refused while the display runs ('video! init' anyway)
```

Tested 2026-10-16 with `tools/kstb-nosplash`: at start-up `video: display
running (frame counter moves)`, then the console on the TV and sound from
`beep` (both checked on the TV). Tested 2026-10-17 with the built-in script
(slot A "kmon-script-builtin"): BOLT's copy `0x07054318–0x07059347` zeroed,
then `video! init` → `display running (frame counter moves)`; a boot with
`tools/kstb-nosplash` → kmon started video and audio itself, slot A confirmed; console on the TV and `beep` heard (checked by the user). Golden seq 35 = slot A seq 34 = this build, CRC `5e2c98cf`.

**HDMI cable / TV** (tested 2026-10-16): at the prompt kmon prints
`hdmi: TV disconnected` and `hdmi: TV connected` when the cable is pulled or
plugged in (hot-plug bit `0xf06fa1a8`). Nothing has to be redone: the output
keeps running, and the console comes back on the TV by itself. Booting with
no TV works too, with and without BOLT's splash; picture and sound appear when
the cable goes in.

## Sound (`audio`, `beep`, `play`, [`audio.c`](audio.c))

HDMI audio the same way as Doom's: BOLT's boot splash starts the audio DMA
(it needs a `pcm0` in `flash0.splash`), looping over a 0.5 s buffer. Without
it (no splash, see "Video"), kmon starts the audio hardware itself with its
own buffer at `0x7da00000` ([`audio_hw.c`](audio_hw.c), tested by ear). kmon
sets HDMI N/CTS for 1080p59.94 at start-up and writes samples 60 ms ahead
of the DMA with silence behind ([`../docs/hardware/audio.md`](../docs/hardware/audio.md)). After a program
(Doom) returns, kmon takes the buffer back. `play` takes PCM WAV files,
8 or 16 bit, mono or stereo, at any rate (linear interpolation to 48 kHz);
it blocks, and any key stops it.

```
kmon> beep 440 1000
kmon> play scale48.wav              from flash0.DATA
kmon> tftp get 192.168.1.38:x.wav 0x02000000
kmon> play                          the file just fetched
```

Tested 2026-10-16, by ear on the TV: `beep 440 1000` clean; a 3 s C-major
scale, 48 kHz 16-bit stereo, clean, panning left to right; the same scale as
22.05 kHz 8-bit mono, smoother with interpolation than with the nearest
sample (some hiss from the 8 bits remains). Both files are on
`flash0.DATA` (`scale48.wav`, `scale22.wav`).

## Network (`net`, `ping`, `tftp`, [`net.c`](net.c))

The Ethernet port, with kmon's own GENET driver (polled; details and tests
in [`../docs/hardware/ethernet.md`](../docs/hardware/ethernet.md)). At start-up kmon brings it up and runs
DHCP (2 tries, at most ~8 s) when the PHY reports a link; without a cable it
prints `net: no link; skipped DHCP` and `net dhcp` works later. The boot is
confirmed to kboot before that, so DHCP doesn't count against kboot's
watchdog. It is stopped before `go`, `run` and `doom`, so its DMA doesn't
write into a program's memory, and started again with the same address
when the program returns or crashes.

Tested 2026-10-16: after a reset, `dhcp: 192.168.1.33 …` and the PC pings
the box with nothing typed; after a program that returned (`status 7`) and
after a crash (`call 0`), the PC still pinged it (2 of 2 each). Tested
2026-10-17, power-on without a cable (`RR:00000003`): slot A confirmed,
`net: GENET up, … link DOWN`, `net: no link; skipped DHCP`, kmon prompt;
cable plugged in, `net dhcp` → `dhcp: 192.168.1.33 …`. On the PC,
[`../tools/tftpd`](../tools/tftpd) serves files and receives `tftp put`.

```
kmon> net dhcp
dhcp: 192.168.1.33, mask 255.255.255.0, gateway 192.168.1.1, from 192.168.1.1
kmon> tftp get 192.168.1.38:kmon.img 0x02000000
tftp: 844800 bytes in 420 ms (1963 KB/s, 512-byte blocks)
tftp: 844800 bytes at 02000000, CRC cbc5f298
kmon> slot a 0x02000000 #844800 my-build          an update, without the serial line
kmon> tftp put 192.168.1.38:copy.bin 0x02000000 #844800
```

The server address is remembered: a later `tftp get kmon.img` uses it.
Only IPv4 addresses, no DNS. Every received file is CRC-printed; compare it
with the file on the PC.

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
[`../docs/hardware/storage.md`](../docs/hardware/storage.md), "The eMMC driver"). Its commands use
the card in the state it finds it: RCA 2, tran state, 8-bit, 50 MHz, which
kboot sets up at every boot (and BOLT before it). `mmc init` does that set-up
again from reset (host software reset, CMD0 … CMD7, two CMD6 writes; ~25 ms):
after a host or card error, the eMMC works again without a reboot
([`../docs/hardware/storage.md`](../docs/hardware/storage.md), "Our own eMMC init"). It reads at ~33 MB/s and writes at ~9 MB/s. LBAs are 512-byte
sectors from the start of the user area; `flash0.splash` is LBA
`#15239168`–`#15273594`, so `flash0.splash + offset` = `#15239168 + offset/512`.

Writes are refused outside `flash0.splash` and `flash0.DATA` (LBA
`#2242560`–`#15239167`, FAT32), and in splash's first 512 KB (the splash
container BOLT reads at boot) unless the command is `mmc!`. The driver
sends CMD6 only to BUS_WIDTH [183] and HS_TIMING [185] (refused for any
other byte), and never erase or lock commands, so it can't change the
card's boot or partition setup.

```
kmon> mmc read #15239168 #25         the splash container to 0x02000000
kmon> crc 0x02000000 0x30c4          = 3fffa0ed
```

### Updating the stored bundle from kmon (tested)

Since kboot, use `slot a` (an update to try) or `slot! golden` instead
([`../kboot/README.md`](../kboot/README.md)): they also write the header kboot checks. The raw
`mmc write` below leaves golden's header stale.

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
| `0x00100000–0x0013ffff` | the monitor: code, data, page tables (≤ 256 KB, checked by `link.ld`; ends at `0x1307b0` since the stack moved out) |
| `0x00140000–` | `doom.bin` in a `KWAD` header (from the bundle) |
| `0x00600000–0x007fffff` | DMA block, non-cacheable: USB descriptors and buffers |
| `0x00800000–0x0081c7ff` | HDMI console scrollback (1024 lines) |
| `0x00820000–0x0082ffff` | FAT32 work buffers (`FAT_BUF`) |
| `0x00830000–0x00831fff` | command line history (32 lines) |
| `0x00840000–0x0084ffff` | the monitor's stack (64 KB) |
| `0x00f00000–0x00ffffff` | the monitor's heap |
| `0x01000000–` | programs: `load`/`go` default, Doom runs here |
| `0x07054318–0x07059347` | read only: BOLT's display script (SplashData), copied at start-up |
| `0x10000000–0x10dc6fff` | the WAD (`KWAD` image), loaded by BOLT |
| `0x7d9f8000–0x7d9fffff` | kmon's display lists (`video init`) |
| `0x7da00000–0x7da2edff` | kmon's audio ring buffer, when BOLT didn't start the audio |

MMU and caches are on, with the same identity map as Doom
([`../doom/start.S`](../doom/start.S)) plus the DMA block: DRAM cached, `0x7d600000–0x7dffffff` (framebuffers,
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

`send addr len` is the other direction: `KSD1`, u32 size, u32 CRC32, then
the raw bytes, straight to UART0 (not to the HDMI console).
[`tools/kstb-recv`](../tools/kstb-recv) receives it and writes the file only when the CRC
matches; `--file path` runs `fget` first. Tested: 16,896 bytes, CRC matched
(11.1 KB/s). A 4 MB transfer lost 11,354 bytes on the serial line (they are
missing from the bridge's log too), so large files go over the network
instead: BOLT `save` ([`../docs/booting.md`](../docs/booting.md)).
