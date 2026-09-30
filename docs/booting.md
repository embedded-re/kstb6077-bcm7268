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
| `go <addr>` | AArch32 SVC, BOLT's MMU and caches on, IRQ/FIQ masked | `32 bit PSCI boot...` |
| `go -64 <addr>` | **AArch64 EL2** | `64 bit PSCI boot...` |
| `boot -64 -el3 -raw -addr=<addr> <file>` | **AArch64 EL3**: your code is the secure monitor | `64 bit PSCI (@ EL3) boot...` / `Secure monitor entry @ …` |

The DTB address is `0x07613000` in all modes. `go`/`boot` close the network
first (`-noclose` keeps it open). `-nopsci` boots without PSCI (untested). A
32-bit program inherits BOLT's page table (`hardware/memory-map.md`). An
AArch64 test program reached the UART at its physical address.

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
