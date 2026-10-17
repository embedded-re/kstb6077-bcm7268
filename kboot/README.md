# kboot: the kmon loader

BOLT's `STARTUP` runs kboot, and kboot chooses which kmon image to start:
a **golden** image, which updates don't overwrite, or **slot A**, where
updates go. A slot-A image that doesn't start properly is tried 3 times and
then golden starts by itself, with no PC and no serial line needed.

kboot runs at EL2 (AArch64) after `go -64 0x03000000`, with the MMU off. It
uses kmon's eMMC driver ([`../mon/mmc.c`](../mon/mmc.c)) and no C library. `make` builds
`build/kboot.bin` (22 KB). Since 2026-10-16 kboot resets the eMMC host and
card itself and sets them up again (`mmc_reset_card()`, up to 3 tries, then
it stops) instead of using the card as BOLT left it:
[`../docs/hardware/storage.md`](../docs/hardware/storage.md), "Our own eMMC init". The layout and the boot state are in [`slot.h`](slot.h),
which kmon uses too.

Tested on the modified box on 2026-10-15/16 unless marked otherwise.

**EL3:** started with `boot -64 -el3` instead of `go -64`, kboot first
replaces BOLT's EL3 monitor (smm64) with its own ([`el3.S`](el3.S)), then
drops to EL2 and carries on the same way. Details and tests:
[`../docs/booting.md`](../docs/booting.md), "Our own EL3". `kboot.bin` is kept in two places:
`flash0.splash +0x80000` (the `go -64` path) and `flash0.DATA:kboot.bin`
(the `boot -64 -el3` path, since `boot` has no `-offset`). `slot! loader`
writes both and reads both back (tested).

The EL3 monitor also starts and stops cores 1–3 (PSCI CPU_ON, CPU_OFF,
AFFINITY_INFO, since 2026-10-16:
[`../docs/hardware/cpu-cores.md`](../docs/hardware/cpu-cores.md) "Our own CPU_ON and CPU_OFF").

Current images (2026-10-16): golden seq 33 and slot A seq 32, both
`kmon-hdmi-hpd` (845,312 bytes, CRC `02847314`; own copy of the display
script, HDMI hot-plug). Before that, `kmon-usb-reset` (CRC `e8f60bf8`; own USB
controller reset, [`../docs/hardware/usb.md`](../docs/hardware/usb.md) "Our own USB start"), and `kmon-mmc-init`
(844,800 bytes, CRC `73315587`); `kboot.bin` 21,704 bytes,
CRC `a3fce739`, in both places. Tested after `slot! loader`: a software
reset → STARTUP → kboot at EL3 → `kboot: eMMC reset and set up in 25 ms` →
slot A → confirmed; kmon's `cpu` showed `kboot's (4b423303)` and `ls` read
`flash0.DATA`. Before that the same kboot.bin ran from RAM (kmon `go
0x03000000`, the EL2 entry): card reset in 34 ms, slot A read and checked
in 568 ms (the same as on BOLT's card set-up), kmon confirmed.

## Choosing an image

| Condition | Starts | Watchdog |
|---|---|---|
| SW1 or SW4 held (**not tested**) | golden | off |
| slot A has a valid header, its CRC matches, fewer than 3 unconfirmed tries | slot A, tries + 1 | 30 s |
| otherwise (no slot A, CRC mismatch, 3 tries used) | golden | off |

kmon confirms a slot-A start once it reaches its prompt (`boot_confirm` in
[`../mon/mon.c`](../mon/mon.c)). It stops the watchdog, sets the tries back to 0 and
prints `boot: slot A, try 1, confirmed; watchdog stopped`. A slot-A image
that hangs or crashes before then is reset by the watchdog, and BOLT runs
STARTUP, and so kboot, again.

kboot starts golden even when golden's CRC doesn't match its header,
because it is the last resort; it prints a warning. Golden without a header
is loaded unchecked (917,504 bytes). kboot sets the power LED orange while
it loads, green when it starts an image, and red if it stops (eMMC not
ready after 3 own resets, golden unreadable); the colours haven't been checked by eye.

## Layout in `flash0.splash`

| Offset | Contents |
|---|---|
| `+0x080000` | `kboot.bin`, raw, at most 64 KB |
| `+0xeffe00` | golden header (one sector) |
| `+0xf00000` | golden image: a kmon bundle (`mon/build/kmon.img`), at most `0xe0000` bytes. The same place the earlier STARTUP loaded, so that STARTUP still works |
| `+0xfe0000` | slot A header (one sector) |
| `+0xfe0200` | slot A image, at most `0xef400` bytes (979,968) |

Header (first 64 bytes of its sector, the rest zero): `KSLT`, version 1,
image size, image CRC32 (zlib), a sequence number (+1 with every write), a
40-byte name, and the CRC32 of the 60 bytes before it.

Boot state: 4 words at the end of AON SRAM, `0xf04105f0` (magic `KBST`,
tries, which image and why, check word). AON SRAM keeps them across the
watchdog resets. After a power-on, kboot started slot A at try 1, as
expected; whether AON SRAM itself is cleared by a power-off is still not
tested.

## Autostart

Since 2026-10-16, EL3 first, EL2 if `flash0.DATA:kboot.bin` can't be loaded
(tested both ways, [`../docs/booting.md`](../docs/booting.md)):

```
setenv -p STARTUP "boot -64 -el3 -raw -fatfs -addr=0x03000000 flash0.DATA:kboot.bin; load -raw -rawfs -offset=0x80000 -addr=0x03000000 -max=0x10000 flash0.splash; go -64 0x03000000"
```

EL2 only (2026-10-15 to 16):

```
setenv -p STARTUP "load -raw -rawfs -offset=0x80000 -addr=0x03000000 -max=0x10000 flash0.splash; go -64 0x03000000"
``` Back to starting golden directly, without kboot:

```
setenv -p STARTUP "load -raw -rawfs -offset=0xf00000 -addr=0x00100000 -max=0x1c0000 flash0.splash; go -64 0x00100000"
```

## Writing images (kmon `slot`)

```
kmon> slot                                  show loader, golden, slot A (each CRC-checked), boot state
kmon> slot a 0x02000000 #844800 my-build    write slot A from RAM (default: the last load/fget); tries = 0
kmon> slot! golden 0x02000000 #844800 name  write golden (needs !)
kmon> slot! loader 0x04000000 #18440        write kboot.bin to splash +0x80000 and flash0.DATA:kboot.bin,
                                            each read back and CRC-checked (needs !)
kmon> slot retry                            tries = 0: kboot tries slot A again
kmon> slot clear                            remove slot A's header: kboot starts golden
```

`slot` writes in this order: it removes the header, writes the image, reads
it back and compares the CRC, then writes the header. A write cut short
therefore leaves no valid header, and kboot starts golden.

Trying a new kmon build, over the network (tested 2026-10-16):

```
make -C mon && cp mon/build/kmon.img doom/build/          # into the TFTP root
kmon> net dhcp
kmon> tftp get 192.168.1.38:kmon.img 0x02000000           # 0.4 s
kmon> slot a 0x02000000 #844800 my-build
kmon> reset                                               # kboot starts slot A
```

Over serial instead: `tools/kstb-send --addr 0x02000000 mon/build/kmon.img`
(~73 s; the serial line has dropped bytes, the CRC check catches it).

If the build is bad, the box comes back on golden after 3 watchdog resets
(about 2 minutes). Once a build is good, `slot! golden` makes it the new
fallback.

## Current images

Since 2026-10-16 golden and slot A both hold the network build (`kmon-net-dhcp`,
844,800 bytes, CRC `49c3109b`): if slot A fails, the box comes back on
golden and is still reachable over the network. Tested: slot A removed,
reset → `golden (no valid image in slot A)`, CRC OK, DHCP, ping from the PC.

## Tests

| Test | Result |
|---|---|
| kboot from RAM, nothing written | `golden (no valid image in slot A)`, golden loaded unchecked, kmon started |
| slot A written (844,800 bytes), kboot from RAM | slot A read and CRC-checked in 562 ms, watchdog armed, kmon confirmed; the watchdog count stayed at 810,000,000 (stopped) |
| one sector of slot A overwritten with zeros | CRC `b0d0acb8` instead of `2bb21e6d`: `golden (slot A failed its check)` |
| golden, slot A and loader written with `slot` | each read back, CRC matched |
| kboot loaded from flash at `BOLT>` with the STARTUP lines | slot A, confirmed |
| STARTUP set, software reset | `Executing STARTUP...` → kboot → slot A → confirmed |
| slot A = a 4-byte image that loops (`b .`) | tries 1, 2, 3, each ended by a watchdog reset (`RR:00000040`), then `golden (slot A did not confirm 3 times)`; kmon printed the same reason |
| power-on (`RR:00000003`) | STARTUP → kboot → slot A, try 1, confirmed |
| power-on without an Ethernet cable (2026-10-17) | the same; kmon skips DHCP (`net: no link`), `net dhcp` works once the cable is in |
| SW1/SW4 held at boot | **not tested** |
| own eMMC reset at every boot (`kboot.bin` CRC `a3fce739`), from RAM and from STARTUP | 34 ms (RAM, after kmon had used the card) and 25 ms (after BOLT); slot A CRC OK, confirmed |

## Recovery when kboot itself is broken

- Ctrl-C at power-on still gives `BOLT>` (`tools/kstb-bolt`). From there,
  start golden by hand or set STARTUP back (above).
- A USB stick with `sysinit.txt` runs before STARTUP ([`../docs/booting.md`](../docs/booting.md)).
