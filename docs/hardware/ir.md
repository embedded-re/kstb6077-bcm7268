# IR receiver

`IR1` on the PCB is an IR receiver. Its signal goes to the SoC's IR receiver
block (Broadcom "KBD"), channel **kbd1** at `0xf0419900`. Set up as an NEC
decoder, the block turns every press on the original Kaon remote into a 32-bit
code, and the codes match the stock Android key map.

Tested on the modified box on 2026-10-03, from `go -64` (EL2, MMU off).
Probes and outputs are in `../bolt/raw/ir/`.

BOLT doesn't enable the block, so after BOLT a remote press changes nothing
(`gpio.md`). Nothing on the box reacts to the remote by itself: the decoding
only happens once software turns it on.

## Where the register names come from

The register layout and the set-up sequence come from the stock Nexus driver
(`stock/release/modules/nexus.ko`, functions `BKIR_*`, see
`../stock-drivers.md`). The register names below are the roles those functions
give the registers, not Broadcom's names. The stock firmware picks the mode in
`stock/release/vendor_build.prop`:

```
ro.ir_remote.mode=CirNec
ro.ir_remote.map=kaon_301rcu_6077
```

In Nexus, `CirNec` is IR device 16, which uses the timing table `necParam`.

## Channels

| Channel | Base | AON L2 bit (`0xf0419c00`) |
|---|---|---|
| kbd1 | `0xf0419900` | 0 |
| kbd2 | `0xf0419980` | 1 |
| kbd3 | `0xf0419a00` | 2 |

The remote is received on **kbd1** (tested). kbd2 and kbd3 were read but not
enabled.

## Registers (per channel)

| Offset | Name (from Nexus) | After BOLT, kbd1 | Tested use |
|---|---|---|---|
| `+0x00` | STATUS | `00000000` | bit 0 = code ready. Cleared by writing STATUS back with bit 0 = 0 |
| `+0x08` | FILTER1 | `00000000` | written 0 |
| `+0x0c` | DATA1 | `00000000` | stays 0 for NEC (32-bit frames) |
| `+0x10` | DATA0 | `00000000` | the received 32-bit code |
| `+0x14` | CMD | `00000007` | bit 4 = CIR (consumer IR) decoder on, bit 5 = interrupt enable |
| `+0x18` | CIR_ADDR | `00000000` | index of a timing parameter (0–27) |
| `+0x1c` | CIR_DATA | `00000814` | value of the parameter selected by CIR_ADDR |
| `+0x20` / `24` / `28` | data filter | `ffffffff` | read only |
| `+0x2c` / `30` / `34` | data filter | `00000000` | read only |

All offsets above read without an abort on all three channels, as do the PM
AON config word `0xf0419880` (`00000000`) and the AON L2 `0xf0419c00–0c`
(`ir_read_probe_output.txt`). `+0x04` and anything past `+0x34` were not read
(Nexus doesn't name them).

## Turning it on (NEC)

This is the order Nexus uses (`BKIR_OpenChannel`, `BKIR_P_EnableInt`,
`BKIR_EnableIrDevice`, `BKIR_P_ConfigCir`), on kbd1:

```
STATUS   = 0
CMD     |= 0x20          (interrupt enable)
CMD     |= 0x10          (CIR on)          -> CMD reads 00000037
FILTER1  = 0
for each (i, v): CIR_ADDR = i, CIR_DATA = v
```

The 27 NEC timing parameters (index: value; index 24 is not written):

```
 0:0ff   1:000   2:012   3:384   4:384   5:1c2   6:0e1   7:000   8:000
 9:000  10:000  11:80f  12:c32  13:000  14:071  15:071  16:10d  17:032
18:830  19:031  20:016  21:006  22:258  23:118  25:000  26:000  27:000
```

They are computed from `necParam` the way `BKIR_P_ConfigCir` packs it. The
table counts in 10 µs ticks: `0x384` = 9 ms (NEC leader), `0x1c2` = 4.5 ms
(space), `0xe1` = 2.25 ms (repeat space). That reading of the units is an
inference from the NEC standard, not measured.

To read codes, poll STATUS bit 0, read DATA0, then write STATUS back with
bit 0 cleared.

To undo, set CMD = `7` and write the saved parameter values back (the probe
reads them first through CIR_ADDR / CIR_DATA). After that, kbd1 read STATUS
`00000014`, CMD `00000007`, CIR_DATA `00000814` and L2 STATUS `0`.

## What a press looks like (tested)

One line per frame from `ir_nec_probe.s` (L2 STATUS, kbd1 STATUS, DATA1,
DATA0), key `1` pressed and held briefly:

```
00000001 00000055 00000000 e31c0820     first frame
00000001 00000037 00000000 e31c0820     repeat
00000001 00000037 00000000 e31c0820     repeat
```

- The first frame of a press has STATUS `0x55`, repeats while the key is held
  have STATUS `0x37` (bits 1 and 5 set, bit 6 clear). The repeat frames
  carry the same code in DATA0. In `BKIR_P_HandleInterrupt_Isr`, bit 6 and
  bit 5 are stored as two "preamble" flags, and for NEC bit 5 is used as the
  repeat flag.
- AON L2 STATUS bit 0 (kbd1) is set while a code is waiting, with the L2
  still masked (`7f`). The interrupt path from the IR block to the L2 works;
  the GIC side is not tested.

## Codes of the Kaon remote (tested)

Every key was pressed (54 presses, 41 different codes). Every code has the
form `~cmd cmd 08 20`: custom code `0x0820`, then a command byte and its
inverse. All 41 are in the stock key map
`stock/release/irkeymap/kaon_301rcu_6077.ikm`, with these names:

| Code | Key | Code | Key | Code | Key |
|---|---|---|---|---|---|
| `ff000820` | POWER | `ed120820` | UP | `e31c0820` | 1 |
| `fa050820` | TV | `ec130820` | DOWN | `e21d0820` | 2 |
| `fb040820` | MUTE | `eb140820` | LEFT | `e11e0820` | 3 |
| `ef100820` | BACK | `e9160820` | RIGHT | `bf400820` | 4 |
| `ee110820` | MENU | `ea150820` | OK (SELECT) | `be410820` | 5 |
| `a55a0820` | HOMEPAGE | `fd020820` | VOLUMEUP | `bd420820` | 6 |
| `e7180820` | RED | `fc030820` | VOLUMEDOWN | `bb440820` | 7 |
| `e6190820` | GREEN | `f9060820` | CHANNELUP | `ba450820` | 8 |
| `e51a0820` | YELLOW | `f8070820` | CHANNELDOWN | `b9460820` | 9 |
| `e41b0820` | BLUE | `b34c0820` | RECORD | `b8470820` | 0 |
| `f30c0820` | EPG | `bc430820` | PLAYPAUSE | `a8570820` | LAST |
| `af500820` | INFO | `b24d0820` | STOP | `f10e0820` | SEARCH |
| `f00f0820` | SUBTITLE | `b54a0820` | FASTFORWARD | `e01f0820` | SLEEP |
| `f40b0820` | SOUND | `b6490820` | REWIND | `b44b0820` | NEXTSONG |

`8` (`ba450820`) was received in the first run, not the all-keys run.

Key map entries with custom code `0x0820` that were not received: `f20d0820`
TUNER, `b14e0820` PLAY, `b7480820` PREVIOUSSONG. Either this remote doesn't
have those keys, or they weren't pressed. The key map also has codes with
custom code `0x2016` and `0xff00`, which belong to other remotes.

## The remote under stock Android 11 (tested 2026-10-04)

Read with `getevent -lt /dev/input/event4` over `adb` (the input device
`NexusIrHandlerTMCZ`; `../stock-firmware.md`). Nexus decodes the IR itself
and sends only Linux key codes: **no raw scan code** (`MSC_SCAN`), so this
run gives key names, not IR codes. Its key layout
(`/vendor/usr/keylayout/NexusIrHandlerTMCZ.kl`) is not readable without root.

Keys received (41): `KEY_0`–`KEY_9`, `UP`, `DOWN`, `LEFT`, `RIGHT`,
`SELECT`, `MENU`, `BACK`, `HOMEPAGE`, `EPG`, `INFO`, `SOUND`, `SUBTITLE`,
`TV`, `MUTE`, `VOLUMEUP`, `VOLUMEDOWN`, `CHANNELUP`, `CHANNELDOWN`,
`RECORD`, `STOP`, `PLAY`, `FASTFORWARD`, `REWIND`, `PREVIOUSSONG`,
`NEXTSONG`, `SLOW`, `RED`, `GREEN`, `YELLOW`, `BLUE`, `POWER`.

Compared with the key map names in the table above: `PLAY`,
`PREVIOUSSONG` and `SLOW` appear here, while `PLAYPAUSE`, `SEARCH`, `SLEEP`
and `LAST` did not. Which physical buttons these are wasn't recorded, so
whether Android 11 names the same codes differently is open.

Timing and behaviour:

| What | Seen |
|---|---|
| events per press | one `DOWN` and one `UP`; no `REPEAT` events (Android repeats keys itself) |
| quick tap (one IR frame) | `UP` 75 ms after `DOWN` |
| normal press (one repeat frame) | `UP` 168 ms after `DOWN`; `kbd1` fired 2 times |
| held 2.78 s | one `DOWN`, one `UP`; `kbd1` fired 26 times, one interrupt per IR frame, a repeat every ~108 ms |
| `RED` | **always** sends `KEY_WAKEUP` (DOWN, UP 22–40 µs later) first, then `KEY_RED`, awake or in standby. No other key does |
| `POWER` | standby, and wakes the box from standby |

## Not tested

- kbd2 and kbd3: not enabled; it isn't known whether anything is wired to
  them.
- The IR interrupt through the GIC (unmask L2 bit 0, then the AON L2 SPI).
- The data filters (`+0x20`–`+0x34`) and FILTER1.
- Other protocols (Nexus has timing tables for RC5, Sony, RC6 and others).
- Whether reading CIR_DATA returns the parameter value: it was read only
  before the NEC values were written and after the restore, never in between.
- Repeat timing on bare metal (no timestamps were taken there; under
  Android, see above).
