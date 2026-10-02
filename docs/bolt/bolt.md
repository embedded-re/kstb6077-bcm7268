# BOLT v1.34 on the KSTB6077: reference

Mapped over the serial console on 2026-09-30. §1–8 used **read-only** commands
only (`help`, `info`, `show *`, `printenv`, `rmem`, `gisb`, `rts`, `d`,
`mii read`, `psci` version query, `rpmb counter`). Nothing was written to
memory, the environment or flash. §9 adds the network, TFTP and 64-bit tests
(RAM loads, watchdog, `go`/`boot`; still no flash or NVRAM writes). Raw captures are in `raw/`.

✅ = observed on this board. ⚠️ = inference or general knowledge.

---

## 1. Identity

| Field | Value | Source |
|---|---|---|
| Version | BOLT v1.34, "LOCAL BUILD" 2018-11-29, `arm-linux-gcc (Broadcom stbgcc-4.8-1.6) 4.8.5` | ✅ boot banner |
| BSP (security firmware) | `4.2.5` (boot log `BFW v4.2.5`) | ✅ `info` |
| SHMOO (DDR tuning) | `5.6.1.0` | ✅ `info` |
| Board | `KM_SH368AT` (one board in the FSBL table, AVS board defaults) | ✅ `boards` |
| UI level | `LEVEL 3; MIN 0` | ✅ `info` |
| RTS | `BOX MODE: 1` | ✅ `rts` (meaning unknown ⚠️) |
| GISB bus timeout | `162000` | ✅ `gisb` |

Compiled-in drivers (`info`): loaders ELF, RAW, SREC, ZIMG; FAT/FAT32; a
network stack (Ethernet, TCP); USB (disk, Ethernet, serial, HID); NAND flash
support; splash 512 KB from `flash0.splash`.

---

## 2. Memory: BOLT's own layout (✅ `info`)

| Region | Range | Size |
|---|---|---|
| **Total used by BOLT** | `0x06FFC000–0x09200000` | 34 MB |
| FSBL info area | `0x06FFC000–0x06FFC04C` | 76 B |
| **Page table (TTBR0)** | `0x07000000` (16 KB L1 + L2 tables at `0x07004000…`) | |
| Code (text) | `0x070080F0–0x0703F000` | 220 KB |
| Initialised data | `0x0703F000–0x07069208` | 169 KB |
| BSS | `0x07069208–0x0706B248` | 8 KB |
| Heap | `0x07100000–0x09100000` | 32 MB (5.5 MB used) |
| Stack | `0x09100000–0x09200000` | 1 MB |

### Reserved memory (✅ `rmem`)

| Name | Base | Size | Notes |
|---|---|---|---|
| `PSCI` | `0x06400000` | 64 KB | the `smm64` PSCI monitor (boot log `INSTALL smm64@06400000`) |
| `BL31` | `0x7DF00000` | 1 MB | ARM Trusted Firmware EL3 runtime (⚠️ by name; secure memory, **don't read or write it**) |
| `SRR` | `0x7E000000` | 32 MB | secure reserved region (⚠️ by name) |

Available to programs: `0x00000000–0x06400000` and `0x06410000` + `0x77af0000`
(up to `0x7DF00000`). BOLT's own 34 MB sits inside the second range, so
treat `0x06FFC000–0x09200000` as occupied while BOLT is alive.

---

## 3. The MMU page table (✅ read from `0x07000000`)

BOLT uses the **ARMv7 short-descriptor format**: 4096 × 4-byte L1 entries,
each covering 1 MB. The entry for address `A` is at `0x07000000 + (A >> 20) * 4`.
The entry layouts below match that format exactly (⚠️ TTBCR itself not yet
read).

| Megabytes | Entry example | Decoded |
|---|---|---|
| `0x000` | `07004001` | **L2 page table** at `0x07004000`. Its first 4 KB page is `00000000` = **unmapped** (a NULL-pointer guard), and the rest are 4 KB normal pages |
| `0x001–0x7FF` (all 2 GB RAM, except the two below) | `0011141e` | section, **Normal memory, write-back cached** (TEX=001 C=1 B=1), shareable, AP=01, **XN=1** |
| `0x064` (PSCI) | `06410406` | section, **Device** (TEX=000 C=0 B=1), shareable |
| `0x070` (BOLT) | `07004801` | L2 page table at `0x07004800` (finer-grained BOLT mappings) |
| `0x800–0xEFF` | `00000000` | unmapped (no RAM above 2 GB) |
| **`0xF00–0xF12`** | `f0010416` | section, **Device**, shareable, XN. **These are the only mapped peripheral megabytes: `0xF0000000–0xF12FFFFF`** |
| `0xF13–0xFFC` | `00000000` | unmapped |
| **`0xFFD` (GIC)** | **`00000000`** | **unmapped**: exactly why `d -w 0xffd01000` aborted |
| `0xFFE` (boot SRAM) | `07004401` | L2 table at `0x07004400`, pages `ffe0x45f` |
| `0xFFF` | `00000000` | unmapped (so high vectors at `0xffff0000` are not mapped ⚠️) |

Notes:
- RAM entries have **XN=1** (execute-never), yet your monitor executes from
  `0x01000000`. That means domain 0 is set to *Manager* in DACR, which
  ignores permission bits (⚠️ inferred; confirm by reading DACR with
  `mrc p15, 0, r0, c3, c0, 0` from a 32-bit program).
- Any peripheral **outside `0xF0000000–0xF12FFFFF`** is unreachable under
  BOLT's MMU. Before probing a new DTB address with `d`, check that it falls
  in this window.
- To reach the GIC: write a device section entry at `0x07003FF4`
  (MB `0xFFD`), for example `ffd10416` (same attributes as the peripheral
  window: `f0010416` = base `0xf00`, Device, shareable, XN), then invalidate the TLB, **from your own code**. ⚠️ Untested;
  writing BOLT's page table from BOLT's `e` command is also possible, but
  the TLB may still hold the old entry.

---

## 4. Devices BOLT knows about (✅ `show devices`)

| BOLT device | What | Range / address |
|---|---|---|
| `uart0` | 16550 DUART channel 0 | `0xf040c000` |
| `mem0` | memory | |
| `flash0` | eMMC user area | 7458 MB |
| `flash0.macadr` | MAC addresses | `0x4400–0x4600` (512 B) |
| `flash0.nvram` | BOLT persistent environment | `0x4600–0x14600` (64 KB) |
| `flash0.bsu` | BOLT sidecar app | `0x14600–0xFFE00` (942 KB) |
| `flash0.misc` | | 1 MB |
| `flash0.hwcfg` | hardware config (**don't touch**) | 1 MB |
| `flash0.KRN` | kernel slot | 64 MB |
| `flash0.DTB` | DTB slot | 4 MB |
| `flash0.SWAP` | swap | 2048 MB |
| `flash0.ROOT` | root filesystem | 5322 MB (shrunk 2026-10-02 to make room for `splash`) |
| `flash0.splash` | boot-splash container (added 2026-10-02, `../hardware/display.md`) | 17 MB |
| `flash1` / `flash2` | eMMC boot partitions 1/2 (BOLT lives here) | 4 MB each |
| `flash3` | eMMC RPMB | 4 MB |
| `eth0` | GENET internal Ethernet | `0xf0480000` |
| `mdio0` | GENET MDIO bus | `0xf0480800` |

These partition names reflect the current (post-Android) layout, matching
`../history/linux-port.md`.

---

## 5. Commands by risk

### Safe: read-only (used for this map)
`help`, `info`, `boards`, `show devices`, `show heap`, `show usb`,
`printenv`, `rmem` (no args), `gisb` (no args), `rts` (no args),
`d` (**only on mapped addresses**, see §3; even mapped blocks can have offsets that external-abort, e.g. AON control `0xf041002c`, so dump new blocks in small pieces), `u` (disassemble), `crc`, `sha`,
`dir`, `mii read`, `psci -r0=0x84000000` (version), `time`, `testenv`, `t`
(compare memory), `dt show`, `dt sane`.

### Volatile: changes RAM or runtime state, lost on reboot
`e`, `f`, `load`, `go`, `boot`, `batch`, `setenv` (without `-p`),
`dt on/off/bolt/add/del`, `ifconfig`, `sleep`, `loop`, `memtest`/`memwrap`
(write RAM patterns: don't run them over BOLT, PSCI or BL31),
`uncache` (changes MMU and caches; `-nommu` turns the MMU off).

### ⚠️ Persistent or irreversible: don't run without a plan
| Command | Why |
|---|---|
| `rpmb program-key` | **one-time programmable**: burns the eMMC RPMB key forever |
| `setenv -p` / `-ro`, `unsetenv`, `incenv` | write the NVRAM partition (`-ro` can never be undone) |
| `setsn`, `macprog` | rewrite serial/MAC storage |
| `flash`, `erase` | write or erase eMMC, including BOLT's own boot partitions. `flash -noerase -mem=<addr> -memsize=<n> mem0 <dev>` writes exactly *n* bytes from RAM to the start of `<dev>` (tested). `-offset` is signed 32-bit: raw `flash0` offsets of 2 GB or more fail or wrap |
| `tz mon`, `tz boot` | load and run code in the secure world |

---

## 6. Useful capabilities discovered

- **64-bit boot works** ✅ (tested 2026-09-30, §9). `go -64` starts an
  AArch64 program at **EL2**. `boot -64 -el3` starts it at **EL3**, as the
  secure monitor (the highest privilege level). The default without flags is
  32-bit (AArch32).
- **TFTP loading works** ✅ (§9) after `ifconfig eth0 -auto`, with one
  caveat: with dnsmasq as the server, a new request can get the **previous
  transfer's file** back. Always verify with `crc`.
- **`u addr [len]`**: a built-in disassembler, handy to check what's in
  memory before a `go`.
- **Scripting**: `loop "cmd" -count=N`, `t` (compare memory with `-eq/-gt/-lt/-and`),
  `testenv`, `time "cmd"`, and `batch` files (like `sysinit.txt`).
- **`load -raw -splash -tftp <pc>:<file>.bmp`** brings up HDMI with a 1920 × 1080
  BMP and leaves a live RGB565 framebuffer ✅ (`../hardware/display.md`).
- **Ctrl-C during boot** cancels autostart (`Automatic startup canceled via
  Ctrl-C`) ✅; `../../tools/kstb-bolt` automates it.
- **`uncache -nommu`** turns BOLT's MMU off. ⚠️ Might make the GIC readable
  from `d`. Risky: BOLT itself may misbehave without caches or its
  mappings. Untested.
- **`tz console on uart1|uart2`**: confirms UART1 (`0xf040d000`) and UART2
  (`0xf040e000`) are real, usable UARTs. Which pins they're on is unknown.
- **`rmem`** can reserve memory and export it to the DTB (`-dt`), for a later
  Linux boot.

---

## 7. Secure world / PSCI

| Probe | Result |
|---|---|
| `psci -r0=0x84000000` (PSCI_VERSION) | `0x2` → **PSCI v0.2** ✅ |
| `psci -r0=0x8400000a` (PSCI_FEATURES) | `0xffffffff` = NOT_SUPPORTED. Expected: FEATURES was only added in PSCI 1.0 ✅ |
| `tz dt show` | `TZ not initialized`. No TrustZone OS is loaded under BOLT ✅ |
| `rmem` | `BL31` (1 MB @ `0x7DF00000`) and `SRR` (32 MB @ `0x7E000000`) reserved ✅ |

Picture (⚠️ inferred from names and the boot log): ARM Trusted Firmware BL31
runs at EL3 (AArch64, `smm64`); BOLT and your code run below it in AArch32;
PSCI calls (`smc`) go up to it. ✅ Confirmed that EL3 exists in AArch64
and that BOLT will hand it to your code: `boot -64 -el3` printed
`PSCI: Secure monitor entry @ 0000000001000000` (§9).

✅ Read from EL3 (§10): `VBAR_EL3 = 0x06400000`, so the EL3 exception vectors
are in the `PSCI` region: **`smm64` at `0x06400000` is the EL3 monitor that
handles PSCI calls.** `SCR_EL3 = 0x131` (NS = 1, RW = 0): BOLT's world is
**non-secure AArch32**.

---

## 8. Other hardware facts reported by BOLT

| Fact | Value | Source |
|---|---|---|
| DDR | `16Gx32 phy:32`, 1856 MHz, `80000000 @ 00000000` (2 GB at address 0) | ✅ `info` |
| SDIO | controller 0 = SD slot, controller 1 = eMMC | ✅ `info` |
| Ethernet PHY | ID `0xae02_5091` = **BCM7268 internal PHY rev 1** (Linux `brcmphy.h`: `PHY_ID_BCM7268 0xae025090`) | ✅ `mii read mdio0 1 2/3` + upstream header |
| PHY state | no cable: BMSR `0x7809` (link down). With a cable: BMSR `0x7829` (autoneg complete), `100 Mbps Full-Duplex` | ✅ `mii`, `ifconfig` |
| USB | 5 root hubs (buses 0–4); BT `0a5c:2045` on bus 2 | ✅ `show usb` |
| eMMC RPMB | `RPMB response error. result: 0x7`. In the eMMC spec, result 7 = "authentication key not yet programmed" (⚠️ spec-based). So the RPMB key was **never programmed**; `rpmb program-key` would set it permanently. | ✅ `rpmb counter flash3` |
| AVS | STB 0.962 V, CPU 0.945 V, 44.8 °C | ✅ `info` |

---

## 9. Network, TFTP and 64-bit tests (2026-09-30, Ethernet connected)

### Network ✅
```
ifconfig eth0 -auto
100 Mbps Full-Duplex
Device eth0:  hwaddr 90-F8-91-E7-00-0A, ipaddr 192.168.1.33, mask 255.255.255.0
        gateway 192.168.1.1, nameserver 192.168.1.1
```
Ping works both ways (board ↔ PC `192.168.1.38`). Network settings are lost
on reboot, so re-run `ifconfig eth0 -auto` after each boot. `go`/`boot`
print `Closing network 'eth0'` before jumping (use `-noclose` to keep it).

### TFTP ✅, with a caveat
PC side (needs root for UDP port 69), serving one folder:
```
sudo dnsmasq --no-daemon --port=0 --enable-tftp --user=arch \
  --tftp-root=<folder> --listen-address=<PC IP> --bind-interfaces
```
Board side:
```
load -tftp -raw -addr=0x01000000 <PC IP>:bootstrap.bin
crc -offset=0x1000000 -size=<file size>     ← compare with the PC's CRC32
```
- The first load was byte-perfect: 264 bytes, CRC `0xa5961154` on both sides.
- ❌ **Stale-file problem.** Later loads returned the **previous transfer's
  file**, whatever name was asked for. It survived `ifconfig -off/-auto` and
  even a board reboot: the first request after a reboot got the file from
  before the reboot. curl on the PC received the correct files from the same
  dnsmasq, so the problem is in the BOLT ↔ dnsmasq exchange. ⚠️ Likely
  cause: BOLT sends every request from the same UDP source port, and dnsmasq
  treats the new request as a retransmit of its still-open old transfer.
  After a few minutes' pause, a load worked again.
- **Workarounds** (⚠️ untested): use a TFTP server that handles each request
  separately (e.g. tftp-hpa's `in.tftpd`), or pause between loads. **Always
  check `crc`** before `go`.
- The "bytes read" count BOLT prints is the size of the file actually
  received, which makes a stale file easy to spot.

### 64-bit (AArch64) ✅
A 24-instruction AArch64 probe (prints `A64 OK, EL<n>` from `CurrentEL`, then
spins; built with `clang --target=aarch64-none-elf` + `ld.lld -Ttext=0x01000000`)
was loaded by TFTP. The watchdog was armed for 20 s first
(`e -w 0xf040a6a8 202fbf00`, then `ff00`, `00ff` to `+0x4`), so the board
rebooted itself afterwards (`RR:00000040`).

| Command | BOLT printed | Probe printed |
|---|---|---|
| `go -64 0x01000000` | `64 bit PSCI boot...` / `PSCI: DTB @ 0000000007613000, Linux entry @ 0000000001000000` | **`A64 OK, EL2`** |
| `boot -64 -el3 -raw -tftp -addr=0x01000000 <PC>:a64.bin` | `64 bit PSCI (@ EL3) boot...` / `PSCI: Secure monitor entry @ 0000000001000000` | **`A64 OK, EL3`** |

What this means:
- The Cortex-A53 cores run AArch64 on this board. 32-bit is just BOLT's
  default for `go`.
- `go -64` enters at **EL2** (hypervisor level), the standard entry point for
  a 64-bit Linux kernel.
- `boot -64 -el3` makes **your code the secure monitor**. At EL3 you control
  everything, including the GIC's secure/non-secure interrupt groups (see
  `../ideas/second-stage-bootloader.md`). Note that BOLT's own PSCI and BL31
  services are then not in charge: your code replaces them, so later calls
  like CPU_ON would have to be handled by you (⚠️ inferred).
- The probe ran with no MMU setup of its own and reached the UART at its
  physical address (⚠️ the MMU is presumably off on entry to a fresh
  exception level).

Probe source (not part of the monitor):
```asm
// AArch64 probe: prints "A64 OK, EL<n>" on the UART, then spins.
.global _start
_start:
    mov     x1, #0xc000
    movk    x1, #0xf040, lsl #16      // x1 = 0xf040c000 (UART)
    adr     x2, msg
1:  ldrb    w0, [x2], #1
    cbz     w0, 3f
2:  ldr     w3, [x1, #0x14]           // LSR
    tbz     w3, #5, 2b                // wait THRE
    str     w0, [x1]
    b       1b
3:  mrs     x4, CurrentEL             // EL in bits [3:2]
    ubfx    x4, x4, #2, #2
    add     w0, w4, #'0'
4:  ldr     w3, [x1, #0x14]
    tbz     w3, #5, 4b
    str     w0, [x1]
    adr     x2, tail
5:  ldrb    w0, [x2], #1
    cbz     w0, 7f
6:  ldr     w3, [x1, #0x14]
    tbz     w3, #5, 6b
    str     w0, [x1]
    b       5b
7:  wfe
    b       7b
msg:  .asciz "\r\nA64 OK, EL"
tail: .asciz "\r\n"
```

---

## 10. CPU and GIC state probe (AArch64, EL2 and EL3)

An AArch64 probe printed CPU system registers and GIC registers, run once
with `go -64` (EL2) and twice with `boot -64 -el3` (EL3), watchdog armed.
The source is in `raw/gic64b_probe.s` and the full output in
`raw/gic_probe_el2_el3.txt`. No access aborted.

| | EL2 | EL3 |
|---|---|---|
| CurrentEL | 2 | 3 |
| MIDR_EL1 | `420f1000`: implementer `0x42` (Broadcom), part `0x100` (Brahma-B53). BOLT's banner `B53 [420f1000]` is MIDR | same |
| MPIDR_EL1 | `80000000`: core 0 | same |
| CNTFRQ_EL0 | `019bfcc0`: **27 MHz** generic timer | same |
| SCTLR | `SCTLR_EL2 = 30c50830`: MMU/caches off | `SCTLR_EL3 = 00c52838`: MMU/caches off |
| VBAR | `VBAR_EL2 = fff7feffb2f7ffe0` (garbage) | `VBAR_EL3 = 06400000` (smm64) |
| HCR_EL2 / SCR_EL3 | `80000002` | `131` |
| GICD_CTLR / GICC_CTLR | `1` / `1` (non-secure view) | `3` / `3` (secure view) |
| GICD_TYPER | `fc67`: 256 IDs, 4 CPUs, security extensions | same |
| GICD_IIDR / GICC_IIDR | `0200143b` / `0202143b`: **GIC-400** | same |
| IGROUPR0 / IGROUPR1–7 | RAZ from non-secure | `fe00ffff` / **`ffffffff`**: all SPIs are group 1 (non-secure) |
| ISENABLER0 / 1–7 | | `0000ffff` / `0`: no SPI enabled |

Decoded in `../hardware/interrupts.md` ("GIC state at handoff") and
`../booting.md` ("State of a 64-bit program at entry").

---

## 11. Reverse engineering BOLT's code

BOLT's running image was dumped from RAM on the modified box and
disassembled. This is how the splash/display behaviour in
`../hardware/display.md` was worked out.

### Getting the image

```
tools/kstb-dump 0x07008000 0x0706c000 bolt_ram.bin     # code + data + bss, ~3 min
```

- The range comes from BOLT's own `info` (code `0x070080f0–0x0703f000`, data,
  bss to `0x0706b248`). It is BOLT's memory, mapped and readable.
- Check: `crc -offset=0x070080f0 -size=0x36f10` on the board must equal the
  CRC32 of the same bytes in the file (it did: `0xd4e64a5f`). The data section
  changes while BOLT runs, so only the code section can be compared.
- The dump is not in this repository (it is Broadcom's code). Re-create it
  with the command above.

### Disassembling

```
# wrap the raw dump as an ELF linked at 0x07008000, strip mapping symbols
printf '.section .text,"ax"\n.incbin "bolt_ram.bin"\n' > wrap.s
arm-none-linux-gnueabihf-as wrap.s -o wrap.o
arm-none-linux-gnueabihf-ld -Ttext=0x07008000 -e 0x07008000 wrap.o -o bolt.elf
arm-none-linux-gnueabihf-strip bolt.elf
arm-none-linux-gnueabihf-objdump -D -M force-thumb bolt.elf > bolt_thumb.dis
```

- **Almost all of BOLT is Thumb-2.** Only the entry stub at `0x070080f0` is
  ARM (`ldr r1, [pc, #24]; str r0, [r1]; bl …`). Disassembled as ARM, the bulk
  is nonsense, with only ~600 PC-relative loads in 56,000 lines.
- Strings are found through literal pools. A `ldr rX, [pc, #n]` loads a 32-bit
  pool word that holds a string's absolute address.

### Functions identified

| Address | Function |
|---|---|
| `0x070107f4` | boot splash main (`NO_SPLASH`/`SPLASH` env, "SPLASH: starting" … "load failed") |
| `0x0701036c` | returns SplashData (built-in table at `0x07056f30`) |
| `0x070112b8` | splash memory "glue" ("SPLASH BMEM init") |
| `0x07011304` | read and decode the `flash0.splash` container (`GZBR`, 512 KB, inflate, CRC) |
| `0x07011430` | look up `bmp0..3` / `pcm0..3` in the `BRCM` payload |
| `0x07011608` | run the display script and draw (`Loaded BMP: W=%d H=%d`) |
| `0x070108e4` | draw a BMP into every set-up surface (centred, background fill, cache flush) |
| `0x070115f4` | return surface *i* from the array at `0x0706ae20` (0 if not set up) |
| `0x07030740` | `load` command: after loading, `-splash` → calls `0x070108e4` |
| `0x07010994`, `0x07025358` | "splash-feedback": draw media *n* from the container |
| `0x07024924` | startup banner (`BOLT v%d.%02d`, `Board:`, `strap=`, `otp @ …` from the fuse table at `0x070472a8`, `bond option:`), see `../hardware/audio.md` (S/PDIF) |

Helper routines seen along the way: `0x070216f0` (getenv), `0x0701c318`
(printf), `0x0701b89c` / `0x0701b804` (heap alloc / free), `0x0701e03c` /
`0x0701e130` / `0x0701e0c4` (open / read / close a device), `0x0701c87c`
(CRC32), `0x0701157c` (inflate).
