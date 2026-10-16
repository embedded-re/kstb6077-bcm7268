# Running code on the board

How the board boots, how to stop it at the `BOLT>` prompt, and the ways to
load and run your own program: over the serial console, from a USB stick or
over TFTP. Ends with the CPU state your program starts in and a watchdog
safety net.

## Boot chain

```
boot ROM → secure first-stage loaders (BFW v4.2.5, BBL v3.1.1; AVS and DDR setup)
  → BOLT v1.34 (eMMC boot partition)
  → AUTOBOOT: waitusb, then batch usbdisk0:sysinit.txt (if a USB stick is present)
  → BOLT> prompt
```

### Secure boot is on (boot log, every power-on)

The secure stages print their fuse reads before BOLT's banner:

```
MemsysFW: OTP Decrypt 0x00000000 … OTP Secure Boot 0x00000001
Memsys verify key 2 ok
Verify/Decrypt Memsys OK!
Sdram Scramble OTP = 0x00000001
BFW key3 OK, BFW load OK
BFW v4.2.5
OTP Sec Boot 0x00000001
SSBL verify key 1 OK
Verify SSBL OK!                 ← SSBL = BOLT, checked before it runs
```

So the boot ROM's chain checks BOLT's signature, and DRAM scrambling is on.
A changed or unsigned BOLT in the boot partition would not run: BOLT is the
first code that can't be replaced, and everything after it is ours (kboot,
kmon, and since kboot's EL3 entry, the EL3 monitor too).

The stock firmware archive has a file `otp_result_file` whose lines read
`0-DataSection_Read`, `0-PublicKey0Index`, `0-MarketId`, `0-DramScramble`,
`0-Enable_Secure_Boot`, `0-AsymmetricCrLockEnable`. Its README reads that
as "Secure Boot: DISABLED". The boot log above contradicts that; the file
looks like a list of factory provisioning steps, each with result 0
(inferred, not tested).

BOLT's environment changes (`setenv`) are lost on reboot unless saved with
`-p`, which writes NVRAM.

## Getting to `BOLT>` (Ctrl-C)

BOLT cancels its autostart when it receives Ctrl-C right after its banner:
`Automatic startup canceled via Ctrl-C`. This works on any box, including a
stock one with `STARTUP` set:

```
tools/kstb-bolt              # then power-cycle the board: lands at BOLT>
tools/kstb-bolt --reset      # board already at BOLT>: software reset, back at BOLT>
```

The tool sends Ctrl-C continuously, so timing doesn't matter. Tested on the
stock box: from a power-cycle (`RR:00000003`) and with `--reset`
(`RR:00000200`, about 5.5 s back to `BOLT>`). Ctrl-C only cancels the
autoboot / `STARTUP` step: BOLT's boot splash (and so HDMI) still runs before
the prompt ([`hardware/display.md`](hardware/display.md)).

### One boot without the splash (Ctrl-S)

In the same check, Ctrl-S makes BOLT print `Splash screen disabled via
Ctrl-S` and skip its boot splash, for this boot only: nothing is stored, the
next boot has the splash again. Autostart still runs. Other keys in the same
loop (from BOLT's code, not tried): Ctrl-E (`Ethernet disabled`, sets env
`ETH_OFF`), Ctrl-U (`USB set to manual mode`, sets env `USBMODE`), Tab
(`System initialization forced`). Don't send the two that set env variables.

```
tools/kstb-nosplash --reset      # from kmon>: reset, Ctrl-S during the boot
tools/kstb-nosplash              # or: power-cycle the board now
```

Tested 2026-10-16 (three boots): Ctrl-S sent continuously from BOLT's `GO!`
line on is always seen. Sending a few after `PSCI: v0.2` with one `kstb` call
each was too late. kmon then sets up the display and the sound itself
([`../mon/README.md`](../mon/README.md), "Video").

## Building the monitor

```
assembly/build.sh      →  assembly/bootstrap.bin (raw), bootstrap.elf (symbols)
```

It's linked at `0x01000000`, so it must be loaded to exactly that address.

## Development loop: upload over the serial console (recommended)

No network, no TFTP server, no USB stick. [`tools/kstb-run`](../tools/kstb-run) writes the binary
into RAM through BOLT's `e -w` command (32 words = 128 bytes per command),
checks every write, compares BOLT's `crc` with the file's CRC32, and then
runs it:

```
tools/build-a64 prog.s                   # AArch64: prog.s -> prog.elf + prog.bin (clang + ld.lld)
tools/kstb-run --a64 prog.bin            # upload, verify, go -64, print output for 10 s
tools/kstb-run --a64 --watchdog 20 --wait-bolt prog.bin
                                         # ...arm the watchdog first, wait until back at BOLT>
tools/kstb-run --no-go prog.bin          # upload + verify only
tools/kstb-run prog.bin                  # 32-bit: plain go
```

C instead of assembly: `tools/build-c prog.c` builds `prog.bin` the same way
(clang + ld.lld, no C library). [`c/main.c`](../c/main.c) is a small example
that prints the chip ID and temperature and cycles the LEDs; run it with
`tools/kstb-run --a64 --watchdog 60 --listen 30 --wait-bolt c/main.bin`.

Measured: 849 bytes in 0.6 s, 16 KB in 11.9 s (about 1.4 KB/s), with the CRC
matching every time. A full cycle (upload, run, watchdog reboot, back at
`BOLT>`) takes about 27 s, mostly the reboot.

Requirements and limits:
- The board must be at `BOLT>`.
- It needs the local `kstb-bridge` service, which owns the serial port and
  exposes `/tmp/kstb.sock` (override with `KSTB_SOCK`). It isn't part of this
  repo.
- It supports `go` and `go -64` only. EL3 (`boot -64 -el3`) needs BOLT to
  load the file itself, from TFTP or USB.
- After `go`, the tool prints output for `--listen` seconds and exits. Use
  `kstb -i` to interact with a running program.

## Loading from a USB stick (FAT32)

Copy [`bootstrap.bin`](../assembly/bootstrap.bin) to the stick as `boot.bin`. [`boot/sysinit.txt`](../boot/sysinit.txt) on the
stick runs automatically at boot. By hand:

```
load -loader=raw -addr=0x01000000 usbdisk0:boot.bin
go 0x01000000
```

## Loading over Ethernet (TFTP)

The board gets an address by DHCP. Network settings are lost on reboot.

```
ifconfig eth0 -auto
load -tftp -raw -addr=0x01000000 <PC IP>:bootstrap.bin
crc -offset=0x1000000 -size=<file size>      ← compare with the file's CRC32
go 0x01000000
```

PC side (TFTP needs root for UDP port 69):

```
sudo dnsmasq --no-daemon --port=0 --enable-tftp --user=<you> \
  --tftp-root=<folder> --listen-address=<PC IP> --bind-interfaces
```

With dnsmasq, a request can return the **previous transfer's file**: BOLT's
requests all come from the same UDP port. Always verify with `crc`; the
"bytes read" count BOLT prints also reveals a stale file. A TFTP server that
handles each request separately (e.g. tftp-hpa) should avoid this, but that
is untested.

### Saving RAM to the PC (`save`)

BOLT's `save <PC IP>:<file> <addr> <len>` sends RAM to the PC over TFTP.
dnsmasq can't receive files, so [`tools/tftpd`](../tools/tftpd) does both directions: it
serves files from `--root` (like dnsmasq above) and stores received files in
`--save`, printing each one's size and CRC32. Stop dnsmasq first, since both
need UDP port 69. tftpd binds the port as root, then drops to the user who
ran sudo.

```
sudo tools/tftpd --ip <PC IP> --root doom/build --save ~/kstb-backup     # PC

ifconfig eth0 -auto                                                       # BOLT>
load -raw -rawfs -max=0x400000 -addr=0x20000000 flash1
save <PC IP>:boot0.bin 0x20000000 0x400000
```

Tested 2026-10-15: 4 MB and 3 MB saves arrived with the same CRC32 as kmon's
`crc` of the same RAM. Use this rather than the serial line for large
files: a 4 MB `send` from kmon over serial lost 11,354 bytes, already
missing in the bridge's log, so before the PC software.

## `go` / `boot` modes

| Command | CPU state at entry | BOLT prints |
|---|---|---|
| `go <addr>` | AArch32; mode unverified (see note below) | `32 bit PSCI boot...` |
| `go -64 <addr>` | **AArch64 EL2** | `64 bit PSCI boot...` |
| `boot -64 -el3 -raw -addr=<addr> <file>` | **AArch64 EL3**: your code is the secure monitor | `64 bit PSCI (@ EL3) boot...` / `Secure monitor entry @ …` |

Caution: **32-bit `go` may enter in HYP mode with the MMU off.** The stock kernel is
started through the same `32 bit PSCI boot` path and reports `CPU: All CPU(s)
started in HYP mode` ([`stock-firmware.md`](stock-firmware.md)). If `go` behaves the same, a 32-bit
program starts in HYP mode with its MMU off, not in SVC under BOLT's page
table. Not yet checked: print `cpsr` and HSCTLR from a 32-bit program. BOLT's
own commands (`d`, `e`) do run under BOLT's MMU ([`hardware/memory-map.md`](hardware/memory-map.md)).

The DTB address is `0x07613000` in all modes. `go`/`boot` close the network
first (`-noclose` keeps it open). `-nopsci` boots without PSCI (untested). A
32-bit program inherits BOLT's page table ([`hardware/memory-map.md`](hardware/memory-map.md)).

### State of a 64-bit program at entry (read by a probe on core 0)

| Register | `go -64` (EL2) | `boot -64 -el3` (EL3) |
|---|---|---|
| SCTLR | `SCTLR_EL2 = 30c50830`: **MMU off, D-cache and I-cache off** (reset value) | `SCTLR_EL3 = 00c52838`: **MMU off, caches off** |
| VBAR | `VBAR_EL2 = fff7feffb2f7ffe0`: **garbage**, no vector table | `VBAR_EL3 = 06400000`: still points at BOLT's PSCI monitor (`smm64`) |
| other | `HCR_EL2 = 80000002` (RW = 1: EL1 would be AArch64) | `SCR_EL3 = 131` (NS = 1, RW = 0) |
| MPIDR_EL1 | `80000000`: core 0 | same |

Consequences:
- All addresses are physical, and every data access is uncached Device
  memory until you enable the MMU. Peripherals, including the GIC, are
  reachable directly.
- **Install your own vector table (`VBAR_ELn`) before anything can fault.**
  At EL2 any exception jumps to a garbage address. At EL3 it would enter
  BOLT's PSCI monitor code.
- There is no stack yet: set `sp` before calling code that pushes.

## Our own EL3 (kboot, tested 2026-10-16)

`boot -64 -el3` starts a program at EL3 through smm64 (`PSCI: Secure
monitor entry @ …`). kboot ([`../kboot/README.md`](../kboot/README.md)) started this way
replaces smm64: it copies its own small EL3 monitor to smm64's region
`0x06400000`, points VBAR_EL3 at it, sets up EL3 and EL2 as smm64 does for
`go -64`, and drops to EL2. Everything after that runs as before.

What smm64 does before it starts an AArch64 program at EL2, read from its
code (64 KB at `0x06400000`, copied with kmon `tftp put`, disassembled;
addresses are smm64's) and repeated by kboot's [`el3.S`](../kboot/el3.S):

| Where in smm64 | Register | Value |
|---|---|---|
| `0x064028b0` (core set-up) | ACTLR_EL3, ACTLR_EL2, ACTLR_EL1 | bit 1 set (lower ELs may access CPUECTLR) |
| | CPUECTLR_EL1 (`S3_1_C15_C2_1`) | bit 6 SMPEN set |
| `0x06400c98` | CNTFRQ_EL0 / CNTVOFF_EL2 | from `0xf0412020` / 0 |
| `0x06402b10` (GIC) | GICD_CTLR (core 0), IGROUPR0–7, GICC_CTLR, GICC_PMR | `3`, all `ffffffff` (group 1), `3`, `0x80`; base from CBAR |
| `0x0640251c` | SCR_EL3 | `0x531`: NS, HCE, RW (lower ELs AArch64) |
| `0x064026f0` | CPACR_EL1, HCR_EL2, SCTLR_EL2 | `0x300000`, `0x80000000`, `30c50830` |
| | CPTR_EL2, CPTR_EL3, HSTR_EL2, MDCR_EL2, MDSCR_EL1 | `& 7feffbff`, `0`, `0`, `0x1f`, `0` |
| `0x064024a4` | CNTHCTL_EL2; CNTHP/CNTP/CNTV_CTL; VPIDR/VMPIDR_EL2 | `\|= 3`; enable 1, mask 0; = MIDR/MPIDR |
| | SPSR_EL3, ELR_EL3 | `0x3c9` (EL2h, interrupts masked), the program's entry |

kboot's monitor answers SMC from EL2: PSCI_VERSION → `2`, CPU_ON, CPU_OFF
and AFFINITY_INFO (cores 1–3, since 2026-10-16, details and tests in
[`hardware/cpu-cores.md`](hardware/cpu-cores.md) "Our own CPU_ON and
CPU_OFF"), SYSTEM_RESET → software reset, `0x8600ff00` → `4b423303` ('KB3'
version 3; version 1 without CPU_ON answered `4b423301`, version 2 without
CPU_OFF `4b423302`; smm64 answers `ffffffff`), anything else → `-1` (not
supported). It keeps `x4`–`x30`. kmon's `cpu` shows which monitor runs.

| Where in our monitor | |
|---|---|
| `0x06400000` | vectors and code (about 3 KB) |
| `0x06400914` | `el3_warm`: where cores 1–3 start |
| `0x0640e000 + 32n` | core *n*: entry, context id, state, lock |
| below `0x06410000 - 1 KB·n` | core *n*'s EL3 stack |

| Test | Result |
|---|---|
| `boot -64 -el3 -raw -tftp -addr=0x03000000 <PC>:kboot.bin` | `kboot: kmon loader (EL3: own monitor …)`, slot A, kmon confirmed, DHCP |
| `boot -64 -el3 -raw -fatfs -addr=0x03000000 flash0.DATA:kboot.bin` (no network in BOLT) | the same |
| SMC from kmon (`call` of `smc #0; ret`) | `0x8600ff00` → `4b423301`; PSCI_VERSION → `2`; CPU_ON → `-1`. On the smm64 path: `2` and `ffffffff` |
| CPU_ON, CPU_OFF with kboot started by `boot -64 -el3 -raw -tftp` (2026-10-16) | `0x8600ff00` → `4b423303`; cores 1–3 started, stopped, started again, [`hardware/cpu-cores.md`](hardware/cpu-cores.md) |
| EL2 timer interrupt (kmon `irq timer 10`) | 412 interrupts in ~4 s |
| SYSTEM_RESET by SMC | software reset (`RR:00000200`), the box came back by itself |
| kboot through `go -64` (EL2) after the change | unchanged |

BOLT reads files from `flash0.DATA` itself (`load`/`boot -fatfs
flash0.DATA:file`, tested with a CRC). In a `;` list, BOLT runs the next
command after one fails (tested: `File not found`, then the next command
ran), so a STARTUP can try the EL3 path and fall back to the EL2 one.

## Safety net for experiments

Arm the watchdog before `go`. If the code hangs, the board reboots itself
back to BOLT (the boot log shows `RR:00000040`):

```
e -w 0xf040a6a8 202fbf00      20 s
e -w 0xf040a6ac 0000ff00
e -w 0xf040a6ac 000000ff
go ...
```

## Serial console

UART0, 115200 8N1, on the 5-pin "UART shell" header (see
[`images/PCB_Front.jpg`](images/PCB_Front.jpg)). Since 2026-10-15 the modified box starts kmon by
itself (`STARTUP`, below); Ctrl-C (above) still stops at `BOLT>`.

## Autostart of kmon (modified box)

Since 2026-10-16 `STARTUP` starts **kboot at EL3** from `flash0.DATA`, so
kboot's own EL3 monitor replaces smm64 ("Our own EL3" above). If that file
can't be loaded, the next two commands start kboot at EL2 from
`flash0.splash` as before:

```
setenv -p STARTUP "boot -64 -el3 -raw -fatfs -addr=0x03000000 flash0.DATA:kboot.bin; load -raw -rawfs -offset=0x80000 -addr=0x03000000 -max=0x10000 flash0.splash; go -64 0x03000000"
```

Tested: a software reset → `boot -64 -el3` → kboot (EL3) → slot A → kmon,
and kmon's SMC identifier call answered `4b423301` (kboot's monitor).
With `kboot.bin` deleted from `flash0.DATA`: `Could not load kboot.bin:
File not found`, then the EL2 path started kboot and kmon as before; after
`slot! loader` wrote the file again, the next reset took the EL3 path.
Not covered by the fallback: a `kboot.bin` that loads but is damaged would
run at EL3 and could hang; `slot! loader` reads the file back to prevent
that, and Ctrl-C at boot still reaches `BOLT>`.

From 2026-10-15 to 2026-10-16 `STARTUP` ran kboot at EL2 only
([`../kboot/README.md`](../kboot/README.md)):

```
setenv -p STARTUP "load -raw -rawfs -offset=0x80000 -addr=0x03000000 -max=0x10000 flash0.splash; go -64 0x03000000"
```

Tested after a software reset, a power-on and three watchdog resets. Before
that, `STARTUP` started kmon directly (still works, golden is in the same
place):

```
setenv -p STARTUP "load -raw -rawfs -offset=0xf00000 -addr=0x00100000 -max=0x1c0000 flash0.splash; go -64 0x00100000"
```

Tested 2026-10-15: after a software reset with no USB stick, BOLT printed
`USB device matching <USB Disk> not found!`, `Executing STARTUP...`, loaded
1,835,008 bytes and kmon came up on the TV and the serial line. It still
works after the `DATA` partition change. `tools/kstb-bolt` (Ctrl-C at boot)
still reaches `BOLT>`. With a USB stick carrying `sysinit.txt`, AUTOBOOT runs
that first. To undo: `unsetenv -p STARTUP`.
