# Running code on the board

## Boot chain

```
boot ROM → secure first-stage loaders (BFW v4.2.5, BBL v3.1.1; AVS and DDR setup)
  → BOLT v1.34 (eMMC boot partition)
  → AUTOBOOT: waitusb, then batch usbdisk0:sysinit.txt (if a USB stick is present)
  → BOLT> prompt
```

BOLT's environment changes (`setenv`) are lost on reboot unless saved with
`-p`, which writes NVRAM.

## Building the monitor

```
assembly/build.sh      →  assembly/bootstrap.bin (raw), bootstrap.elf (symbols)
```

It's linked at `0x01000000`, so it must be loaded to exactly that address.

## Development loop: upload over the serial console (recommended)

No network, no TFTP server, no USB stick. `tools/kstb-run` writes the binary
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

Copy `bootstrap.bin` to the stick as `boot.bin`. `boot/sysinit.txt` on the
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

## `go` / `boot` modes

| Command | CPU state at entry | BOLT prints |
|---|---|---|
| `go <addr>` | AArch32; ⚠️ mode unverified (see note below) | `32 bit PSCI boot...` |
| `go -64 <addr>` | **AArch64 EL2** | `64 bit PSCI boot...` |
| `boot -64 -el3 -raw -addr=<addr> <file>` | **AArch64 EL3**: your code is the secure monitor | `64 bit PSCI (@ EL3) boot...` / `Secure monitor entry @ …` |

⚠️ **32-bit `go` may enter in HYP mode with the MMU off.** The stock kernel is
started through the same `32 bit PSCI boot` path and reports `CPU: All CPU(s)
started in HYP mode` (`stock-firmware.md`). If `go` behaves the same, a 32-bit
program starts in HYP mode with its MMU off, not in SVC under BOLT's page
table. Not yet checked: print `cpsr` and HSCTLR from a 32-bit program. BOLT's
own commands (`d`, `e`) do run under BOLT's MMU (`hardware/memory-map.md`).

The DTB address is `0x07613000` in all modes. `go`/`boot` close the network
first (`-noclose` keeps it open). `-nopsci` boots without PSCI (untested). A
32-bit program inherits BOLT's page table (`hardware/memory-map.md`).

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
`images/PCB.png`). BOLT stops at `BOLT>` when autoboot finds no USB stick.
