# Display (HDMI framebuffer)

BOLT brings up HDMI itself during boot (its splash-screen feature) and leaves
a plain linear framebuffer behind. **The display keeps running after
`go -64`**, so bare-metal code can draw on the TV by writing RAM.

**Requirement: a valid `flash0.splash` partition.** BOLT only sets up the
display after it has read that partition at boot (see "How BOLT's splash
works" below). The stock box has one. The modified box had lost it (the
Linux port repurposed it); without it, the boot splash stops at
`SPLASH: bad file`, the display is never set up, and `load -splash` silently
does nothing. A new `splash` partition was added to the modified box
(see "Adding a splash partition" below). Since then both boxes show the splash
and keep the display running after `go -64` (tested). Raw captures are in
[`../docs/evidence/display/`](../docs/evidence/display/).

## Summary

| | |
|---|---|
| Output | HDMI, **1920 × 1080 at 59.94 Hz**, set up by BOLT's boot splash |
| Requirement | a valid `flash0.splash` partition; without it BOLT never sets up the display |
| Framebuffer | `0x7db0b700`, RGB565, pitch 3840 bytes; pixel(x, y) = `0x7db0b700 + y × 3840 + x × 2` |
| Surface register | GFD `0xf0641048` (read it instead of hard-coding the address) |
| Frame counter | `0xf0603484`, one step per frame (16,683 µs) |
| Page flip | write the buffer address to `0xf0603488` and `0xf060348c`; takes effect at the next frame |
| Frame interrupt | BVNF_0 L2 `0xf0606000` bit 0, GIC 49: latches once per frame (also while masked) |
| Display lists | RDC lists at `0x7db08000–0x7db0b6ff`: **never overwrite**. Running a copy of BOLT's list from free RAM is tested |
| State after `go -64` | still running |
| Tested | drawing, timing with and without the MMU, double buffering, a custom RDC list, compositor background, window size and position, GFD source width, the frame interrupt, scanout from Linux RAM (KMS driver `kstb-drm`) |
| Not understood | the GFD scaler, part of the compositor registers |

## Bringing up HDMI from the BOLT prompt

```
ifconfig eth0 -auto
load -raw -splash -tftp <PC IP>:test1080.bmp
```

- The file must be a **1920 × 1080, 24-bit BMP**. [`../docs/evidence/display/make_test_bmp.py`](../docs/evidence/display/make_test_bmp.py)
  generates the test pattern used here.
- BOLT converts it to RGB565, flips the BMP's bottom-up rows and draws it,
  centred, into the framebuffer(s) the boot splash already set up. It prints
  only `… bytes read`. It does **not** set up the display: with no boot-splash
  surfaces it draws nothing and reports nothing. The BMP itself lands at the
  load address (`0x80000` by default).
- `-raw` is required: without it the default zImage loader rejects the file
  (`Bad executable format`).
- From a USB stick, `load -raw -splash usbdisk0:<file>.bmp` should work the
  same way (untested).
- `load -splash -rawfs flash0.splash` does **not** work: the splash partition
  uses BOLT's own container format (`Invalid boot block on disk`). BOLT reads
  that partition itself during every boot, before the prompt and before
  AUTOBOOT. A Ctrl-C cancel ([`../docs/03-running-code.md`](../docs/03-running-code.md)) doesn't skip it.

## Framebuffer layout

```
pixel(x, y) = 0x7db0b700 + y × 3840 + x × 2          x = 0…1919, y = 0…1079
```

| Property | Value | How it was established |
|---|---|---|
| Format | **RGB565**, 16 bits per pixel, little-endian halfwords | the BMP's pure red/green/blue/yellow read back as `f800`/`07e0`/`001f`/`ffe0` |
| Colour order | R in bits 15:11, G in 10:5, B in 4:0 | bars appeared red, green, blue on screen |
| Origin | top-left, rows top to bottom | corner squares in the expected corners |
| Width × height | 1920 × 1080 | 1920-pixel row runs in memory; `0x780` / `0x438` in the display registers |
| Pitch | **3840 bytes** (no padding) | consecutive rows `0xf00` apart; `0xf00` written to `0xf0641058` |
| First pixel | `0x7db0b700` | white top-left square starts exactly there; same value in `0xf0641048` |
| Memory | BOLT `rmem` region `splash0` (`0x7db08000`, `0x3f8000` bytes) | stock `rmem` |

RGB565 examples: white `ffff`, black `0000`, red `f800`, green `07e0`,
blue `001f`, yellow `ffe0`, cyan `07ff`, magenta `f81f`.

From AArch64 at EL2 (`go -64`) the MMU is off, so every store goes straight
to DRAM and appears on screen immediately. Under BOLT's MMU the region is
mapped cached, but writes still appeared at once in testing.

## Display registers (read-only knowledge)

The graphics feeder (GFD) block at `0xf0641000`:

| Register | Value | Meaning |
|---|---|---|
| `0xf0641044` | `00000780` | source width, 1920 (see "Graphics feeder" below) |
| `0xf0641048` | `7db0b700` | **surface address: where the pixels are** |
| `0xf0641058` | `00000f00` (written by the set-up lists only) | pitch, 3840 bytes; a direct write takes effect and stays ("Pixel formats and pitch") |
| `0xf0641174` | `00000438` (written by the display lists) | height, 1080 |

**Reading `0xf064105c` gives an external abort**, which hangs BOLT. The 590
registers that BOLT's display lists use all read without an abort (see
"Display lists" below), so those are the known-safe set. Anything else in
the display blocks may abort.

Bare-metal code should read `0xf0641048` instead of hard-coding the address
([`../docs/evidence/display/fbdraw_probe.s`](../docs/evidence/display/fbdraw_probe.s) does exactly that).

## What's in front of the pixels: display lists (do not touch)

`0x7db08000–0x7db0b6ff` (the first `0x3700` bytes of `splash0`) holds **RDC
lists**. The first list entry is at `0x7db08fa0`; the bytes before it are
random leftover RAM contents (they differ between boots). With a `pcm0` in the splash
container, BOLT puts the audio buffer at `0x7dada100–0x7db08eff`, and the
lists starting at `0x7db08fa0` stay intact (tested, see [`audio.md`](audio.md)). The RDC
lists are: register-write programs that the display hardware's register-DMA
controller fetches from RAM by itself. The DTB's memory-client list names
`bvn_rdc`, and the bus arbiter lists an `rdc_0` master. The format, how
BOLT's lists run, and how to run a custom list are in "Display lists
(RDC)" below.

**Overwriting this area blanks the screen** until the next reboot (tested by
accident). Full dump: [`../docs/evidence/display/splash0_rdc_lists_0x7db08000.txt`](../docs/evidence/display/splash0_rdc_lists_0x7db08000.txt).

## Drawing from bare-metal code: the confirmed recipe

1. Boot with a valid `flash0.splash` (the boot splash sets up the display).
   Optionally replace the picture: `load -raw -splash -tftp <PC>:image.bmp`.
2. Load your program and `go -64 <addr>` (EL2, MMU off).
3. Read the base from `0xf0641048`, then write RGB565 halfwords at
   `base + y × 3840 + x × 2`.

Tested: [`../docs/evidence/display/fbdraw_probe.s`](../docs/evidence/display/fbdraw_probe.s) read `surface @ 7db0b700` and
drew a 400 × 200 magenta box in the centre of the screen, over the BMP.
The display was still on and refreshing after `go`.

Not yet known: whether the display survives a 32-bit `go`, whether the
resolution can be changed, and the meaning of the rest of the display
pipeline's registers.

## Video speed: how fast the CPU can redraw the screen

Measured on the modified box (2026-10-02) at EL2 after `go -64`, with the
generic timer (27 MHz). The area is 1600 × 1000 (a 320 × 200 image scaled ×5),
centred at (160, 40). Probes: [`../docs/evidence/display/video_probe.s`](../docs/evidence/display/video_probe.s) (MMU off,
as entered) and [`../docs/evidence/display/video_probe_mmu.s`](../docs/evidence/display/video_probe_mmu.s) (MMU on, see below).
[`../docs/evidence/display/video_demo_mmu.s`](../docs/evidence/display/video_demo_mmu.s) runs the same drawing as a demo to
watch (35 fps paced, then full speed).

| Test | MMU off | MMU on |
|---|---|---|
| A: solid fill, 16-byte stores (`stp`) | 87.2 ms/frame, 11.4 fps | **4.5 ms, 221 fps** |
| B: solid fill, 2-byte stores (`strh`) | 697.8 ms, 1.4 fps | **4.5 ms, 221 fps** |
| C: copy a 3.2 MB image from RAM (`ldp`/`stp`) | 130.1 ms, 7.6 fps | **5.3 ms, 189 fps** |
| D: scrolling bars (build one row, copy it to 1000 lines) | 130.4 ms, 7.6 fps | **4.5 ms, 221 fps** |
| E: 320 × 200 8-bit image → 256-colour palette → ×5 → screen | — | **4.7 ms, 214 fps** |

All numbers are tested. The MMU-off probe ran twice and the runs agreed to
0.01 %; the MMU-on probe ran once.

- **MMU off**, every access is uncached Device memory: writes reach about
  37 MB/s, and every store is a separate bus transaction, so 2-byte stores are
  8× slower than 16-byte ones. The image updates on screen as it is drawn.
  The scrolling bars moved visibly but roughly, with no visible tearing at
  that speed (tested, by eye).
- **MMU on**: about 700 MB/s to the screen. The framebuffer is mapped
  Normal non-cacheable, so writes go to DRAM without cache maintenance but are
  merged on the way, and 2-byte stores cost the same as 16-byte ones. Source
  data in cached RAM reads fast.
- So a 320 × 200 picture redrawn at 35 fps (28.6 ms per frame) takes **under 5 ms**
  per frame to draw at ×5, but only with the MMU on. Compiled C also needs
  the MMU on for a second reason: unaligned accesses fault on Device memory.
- Drawing straight into the visible buffer **tears**. At 35 fps and at full
  speed, moving bar edges showed as "stairs" (tested, by eye). The display
  scans the buffer out while it is being redrawn. Fix: double buffering
  synced to the frame counter, below.

## Vsync and double buffering (tested)

BOLT's display lists don't write a fixed surface address. Every frame, the
RDC loads it from its own register and writes it to the graphics feeder:

```
03000002 f0603488             RDC: load the value of 0xf0603488
0b000082 0d001082 130020c2    (arithmetic on RDC variables)
02002000 f0641048             RDC: write the result to the GFD surface address
```

The same sequence appears four times, with `0xf0603484/88/8c/98`. Read
from `BOLT>` with the watchdog armed (one word each, no abort):

| Register | Value | Meaning |
|---|---|---|
| `0xf0603484` | `5000xxxx`, low bits count up | **frame counter**: one step every 16,683 µs = **59.94 Hz** (timed over 60 steps by [`vsync_probe.s`](../docs/evidence/display/vsync_probe.s)) |
| `0xf0603488` | `7db0b700` | surface address the RDC copies into `0xf0641048` |
| `0xf060348c` | `7db0b700` | same value; written together with `+0x488` |
| `0xf0603498` | `60845e89` | didn't change between reads; unknown |

**Writing a buffer address to `0xf0603488` and `0xf060348c` flips the
screen at the next frame boundary.** `0xf0641048` then reads the new address,
and the picture switches whole, with no half-drawn frame (tested). A second
buffer at `0x7d600000` (`0x3f4800` bytes, free `rmem` below `splash0`) is
displayed fine. It has the same layout as BOLT's: pitch 3840, RGB565.

Double-buffered loop ([`../docs/evidence/display/vsync_probe.s`](../docs/evidence/display/vsync_probe.s)):
```
draw the frame into the back buffer
write the back buffer's address to 0xf0603488 and 0xf060348c
wait until 0xf0603484 changes (n times: 1 = 60 fps, 2 = 30 fps)
swap front and back
```

| Phase | Measured | Seen on the TV |
|---|---|---|
| 8 flips A ↔ B, 0.5 s apart (B = copy of A plus a magenta box) | | clean blinks |
| scrolling bars, 1 frame per tick | 16,683 µs/frame, 59.9 fps | **smooth, no stairs** |
| palette path (320 × 200 → palette → ×5), 2 ticks per frame | 33,366 µs/frame, 29.9 fps | motion clean; the picture itself shows 5 × 5 pixel blocks, as any ×5 scaling does |

- The mode is **1080p at 59.94 Hz**, not 60 Hz (tested by the counter). This
  matches the HDMI audio finding in [`audio.md`](audio.md).
- Stock Android 11 uses **1080p at 50 Hz** on the same TV (PHILIPS FTV):
  the display-frame interrupt `VEC` (GIC ID 47) counted 502 in 10.05 s, and
  `dumpsys display` reports `refreshRate=50.0`. So the 59.94 Hz is BOLT's
  choice, not the TV's ([`interrupts.md`](interrupts.md)).
- A program drawing at 35 fps on a 59.94 Hz display gets an uneven mix of 1- and 2-tick
  frames. Locking to 30 fps (2 ticks) or 60 fps keeps motion even.
- Writing only `0xf0603488`, or only `+0x48c`, wasn't tried. The probe always
  writes both.
- At the end the probe flips back to BOLT's buffer `0x7db0b700`. Nothing in
  the display lists is written.

### Frame interrupt (tested 2026-10-17, from Linux)

The stock `nexus.ko`'s interrupt map (`bint_map`: L1 bit, L2 register,
name) puts the two display interrupts the stock kernel counts
([`interrupts.md`](interrupts.md)) in front of standard Broadcom L2
controllers (`+0` status, `+4` set, `+8` clear, `+0x0c` mask status,
`+0x10` mask set, `+0x14` mask clear):

| GIC ID | Name | L2 controller | Read under kmon's/BOLT's set-up |
|---|---|---|---|
| 47 | `VEC` | `0xf06e7800` | status `0`, mask `00007fff` (15 sources, all masked); nothing latches |
| 49 | `BVNF_0` | `0xf0606000` | status `00000001`, mask `ffffffff` |

**BVNF_0 bit 0 latches once per frame, at the frame counter's step.**
Polled through `/dev/mem` for 100 ms, clearing the bit (`1` to
`0xf0606008`) each time it was set: it came back 6 times, 16.68 ms apart,
each time in the same poll in which `0xf0603484` stepped. Neither block
aborted on reads. The bit latches while masked too: unmasking it after a
pause fires at once for a frame long past (seen as a page flip reported
done one frame early, before the GFD had the new address). So a driver
should keep it unmasked, or clear it before unmasking. Under Linux it is
mainline's `brcm,l2-intc` (edge, acked through `+8`) on GIC SPI 17, used
by `kstb-drm` as its vblank interrupt (`port/README.md`, "Display").

### Scanout from Linux RAM (tested 2026-10-17)

The display reads buffers anywhere tested so far, not only in `rmem`:
Linux's CMA area at `0x77000000` (scanned out: `0x77500000` and others
`kstb-drm` allocated), RGB565, pitch 3840. The flip registers take the
address as above, and `0xf0641048` reads it back from the next frame.
Seen on the TV by the user: the text console, a CPU-drawn bar moving 16
pixels per frame (59.94 flips per second, no tearing) and a GPU-rendered
spinning triangle, each from buffers in that range.

### The minimal MMU set-up used (EL2, tested)

Identity map, 4 KB granule, `TCR_EL2 = 0x80800020` (T0SZ 32 = 4 GB, table
walks non-cacheable), `MAIR_EL2 = 0x0444ff00` (attr 0 Device-nGnRnE,
1 Normal write-back, 2 Normal non-cacheable, 3 Device-nGnRE). Level 1 table:

| Range | Mapping |
|---|---|
| `0x00000000–0x3fffffff` | 1 GB block, Normal write-back (`0x705`) |
| `0x40000000–0x7fffffff` | level 2 table of 2 MB blocks: write-back, except `0x7da00000–0x7dffffff` (splash0) **non-cacheable** (`0x709`) |
| `0x80000000–0xbfffffff` | unmapped |
| `0xc0000000–0xffffffff` | 1 GB block, Device-nGnRE, execute-never (UART, GFD, GIC) |

Then `tlbi alle2`, `ic iallu`, and set M, C and I in `SCTLR_EL2`. A vector
table at `VBAR_EL2` prints `ESR`/`ELR`/`FAR` on any exception (none
happened). The 2 MB block at `0x7de00000` also covers BL31 (`0x7df00000`,
secure); it is mapped because the framebuffer's last lines are in it, but
nothing touches BL31. Page tables at `0x10600000`, test data at
`0x10000000–0x1042ffff` (free `rmem`).

## Display lists (RDC)

Tested on the modified box on 2026-10-03, from `go -64` (EL2, MMU off).
Probes and outputs are in [`../docs/evidence/display/`](../docs/evidence/display/).

### List format

Taken from the stock Nexus driver (`stock/release/modules/nexus.ko`): the
list builders `BRDC_AddrRul_*_isr` and the list dumper
`BRDC_DBG_GetListEntry_isr`, which knows how many words follow each opcode.
Every entry starts with one command word, opcode in bits 31–24. Register
addresses in the lists are CPU addresses (`0xfxxxxxxx`). The RDC has 64
variables (`v0`–`v63`).

| Opcode | Name (from Nexus) | Words | Layout |
|---|---|---|---|
| `00` | NOP | 1 | |
| `01` | IMM_TO_REG | 3 | `01000000, reg, value` |
| `02` | VAR_TO_REG | 2 | `02000000 \| var << 12, reg` |
| `03` | REG_TO_VAR | 2 | `03000000 \| var, reg` |
| `04` | IMM_TO_VAR | 2 | `04000000 \| var, value` |
| `05` | IMMS_TO_REG | 2 + n | `05000000 \| (n − 1), reg, n values` (one register) |
| `06` | IMMS_TO_REGS | 2 + n | `06000000 \| (n − 1), reg, n values` (consecutive registers) |
| `07` / `08` | REGS_TO_REGS / REG_TO_REGS | 3 | `op \| (n − 1), source reg, destination reg` |
| `0b` / `0d` / `0f` / `13` | AND / OR / XOR / SUM | 1 | `op \| a << 12 \| b << 6 \| dest` |
| `0c` / `0e` / `10` / `14` | AND / OR / XOR / SUM with a value | 2 | `op \| a << 12 \| dest, value` |
| `11` | NOT | 1 | `11000000 \| a << 12 \| dest` |
| `12` | shift | 1 | shift amount in bits 22–18 |

Opcodes `15`–`17` exist in the dumper but don't appear in BOLT's lists.

`tools/re/rdc.py DUMP [START]` decodes a hex dump with this table, skipping
the leftover RAM between lists. The decoded lists:
[`../docs/evidence/display/splash0_rdc_lists_decoded.txt`](../docs/evidence/display/splash0_rdc_lists_decoded.txt).

Example, the surface reload that runs every frame (see "Vsync and double
buffering"):

```
03000000 f0603498        v0 = [0xf0603498]
03000001 f060348c        v1 = [0xf060348c]
0b000041                 v1 = v0 AND v1
11000000                 v0 = NOT v0
03000002 f0603488        v2 = [0xf0603488]
0b000082                 v2 = v0 AND v2
0d001082                 v2 = v1 OR v2
130020c2                 v2 = v2 + v3
02002000 f0641048        [0xf0641048] = v2       (GFD surface address)
```

### How BOLT's lists run

The RDC has descriptors. Each one holds a list address and a word count:

| Register | Descriptor 0 | Descriptor 1 |
|---|---|---|
| list address | `0xf0604000` | `0xf0604040` |
| count (words − 1) | `0xf0604008` (and `+0x18`) | `0xf0604048` (and `+0x58`) |
| config | `0xf0604010` = `06180000` | `0xf0604050` = `06190000` |

BOLT's lists end with a "tail" that points a descriptor at the next list,
bracketed by `0xf0605000`:

```
01000000 f0605000 00000000
01000000 f0604000 7db09ae0      next list
01000000 f0604010 06180000      config
01000000 f0604008 00000048      count = 73 words − 1
01000000 f0604018 00000048
01000000 f0605000 00000001
```

From the decoded lists and the live values: descriptor 0 runs a 73-word list
**every frame**. That list sets the compositor canvas and background, reloads
the GFD surface address from `0xf0603488/8c`, and counts the frame counter
`0xf0603484` up. BOLT builds it twice, at `0x7db09c60` (the second-to-last
list of the chain) and `0x7db09ae0` (the last one). The tail of `0x7db09c60`
points the descriptor at `0x7db09ae0` with a count of `0x48`, and
`0x7db09ae0` has no tail, so from then on it runs every frame and the
descriptor no longer changes. (The bytes after it are left over from the
allocation, not a tail: see "SplashData and the script runner".) Descriptor 1
(config `06190000`) still points at its first list (`0x7db09980`, count
`0x55`): BOLT never starts it, and its trigger doesn't fire at 1080p.

**The list address register reads back the previously written value**, not
the current one. In three switches, `0xf0604000` read `7db09c60` while BOLT's
`0x7db09ae0` was running, then `7db09ae0` while the custom list ran, then
`02000000` after the switch back. So the list that is actually running at
the prompt is the one at `0x7db09ae0`.

### Reading the registers the lists use

[`bvn_read_probe.s`](../docs/evidence/display/bvn_read_probe.s) read every register that the decoded lists write or read:
590 addresses, all through an abort-safe read. It left out the HDMI registers
`0xf06fa828/884/888/898`.

- All 590 read without an abort.
- 572 hold exactly a value that the lists write.
- `0xf06e07fc` and `0xf06e6288` hold the list value without its top byte
  (`61389946` → `00389946`, `60a864df` → `00a864df`).
- The rest are RDC variables, the descriptor address (see above), and HDMI
  registers that the lists change by read-modify-write.

Output: [`../docs/evidence/display/bvn_read_probe_output.txt`](../docs/evidence/display/bvn_read_probe_output.txt).

### What the lists write, block by block

The block names come from which Nexus functions use the addresses
(`tools/re/ko.py nexus.ko consts`, plus the `0xf06xxxxx` literals in the
functions). Only the registers marked tested below have been changed.

| Block | Address | Nexus functions | In BOLT's lists |
|---|---|---|---|
| RDC variables, frame counter | `0xf0603480–98` | `BRDC_*` | frame counter, surface address for the flip |
| RDC descriptors | `0xf0604000–58`, `0xf0605000` | `BRDC_Slot_*` | list chaining |
| GFD0, graphics feeder | `0xf0641000–0x1350` | `BVDC_P_GfxFeeder_BuildRul_isr`, `…BuildCfcRul_isr` | surface, size, format, scaler, colour conversion |
| CMP0, compositor | `0xf0645800–0x5f2c`, `0xf065001c` | `BVDC_P_Compositor_BuildSyncSlipRul_isr` | canvas, background, graphics window |
| display timing (VEC) | `0xf06e0000–0x7fc`, `0xf06e2400`, `0xf06e6000–0x6c00`, `0xf06e7000–0x7524` | `BVDC_P_Display_*`, `BVDC_P_Vec_*` | 256 words into `0xf06e0400–0x7fc`, probably timing microcode |
| colour conversion for HDMI | `0xf06e4000–0x41xx` | `BVDC_P_Vec_Build_DVI_CSC` | 62 registers |
| HDMI transmitter | `0xf06fa000–0xa8xx` | `BHDM_*`, `BVDC_P_Vec_Build_DVI_RM` | rate manager, read-modify-write of `+0x810/814/81c/820/854` |

### Running a custom list (tested)

The RDC can run a custom list from free RAM. [`rdc_ownlist_probe.s`](../docs/evidence/display/rdc_ownlist_probe.s):

1. Copy the 73 words at `0x7db09c60` to `0x02000000` and check the copy.
   BOLT's lists are only read.
2. Change words in the copy as needed.
3. Switch descriptor 0, the way BOLT's tails do it:
   `0xf0605000 = 0`, `0xf0604000 = 0x02000000`, `0xf0605000 = 1`. Config
   and count stay, since the copy has the same length.
4. To go back: the same three writes with `0x7db09ae0`.

While the custom list ran, the frame counter kept counting at 60 per second, and the
values from the copy appeared in the registers. Words in the copy can be
changed while it runs: the RDC fetches it from RAM every frame (with the MMU
off, no cache maintenance is needed). After the switch back, BOLT's values
returned within a second.

Why this matters: registers that the per-frame list writes can't be changed
by a direct write. The list puts its own value back at the next frame (about
16.7 ms). `0xf0645810` written directly read `0018b87b` again one second
later, and nothing showed on the TV ([`gfd_bg_probe.s`](../docs/evidence/display/gfd_bg_probe.s)). Registers that only
the set-up lists write keep a direct write (GFD `+0x01c`, `+0x044`, `+0x170`
held their values for 10 s).

## Compositor CMP0 (tested)

All changed through a custom list ([`rdc_ownlist_visible_probe.s`](../docs/evidence/display/rdc_ownlist_visible_probe.s),
[`cmp_window_probe.s`](../docs/evidence/display/cmp_window_probe.s), [`cmp_window_pos_probe.s`](../docs/evidence/display/cmp_window_pos_probe.s)), and observed on the TV.

| Register | BOLT | What it does (tested) |
|---|---|---|
| `0xf0645810` | `0018b87b` | **background colour**, `00 Y Cb Cr`. Visible wherever no window covers the canvas. `0018b87b` shows blue, `00515af0` shows red. Nexus: `BVDC_Compositor_SetBackgroundColor(r, g, b)` turns RGB into this through a colour matrix. |
| `0xf0645988` | `07800438` | **graphics window size**, `width << 16 \| height`. `03c0021c` (960 × 540): only the top-left 960 × 540 of the picture shows, at 1:1, and the rest is background. |
| `0xf064598c` | `00000000` | **graphics window position**, `x << 16 \| y`. With size 960 × 540, `01e0010e` placed the window in the centre of the screen (x 480, y 270), stable. |
| `0xf0645980` | `07800438` | 960 × 540 here showed the top-left 960 × 540 of the picture in the top-left corner and **black** everywhere else, not background. It limits something after the background is drawn; the exact role is open. |

Not understood (observed, no explanation yet):

- Window size 960 × 540 with position `010e01e0` (x 270, y 480, if the
  packing above is right) flickered between two positions a little apart
  vertically, for the full 20 s.
- A full-size window (1920 × 1080) at `01e0010e`, which doesn't fit on the
  canvas, gave unstable flicker with mostly background showing.

Other values that BOLT's per-frame list writes to the compositor every
frame: `0xf064580c = 07800438` (canvas size, named by Nexus' builder),
`0xf0645814 = 0002ff34` (blend control, "Compositor blend and the
feeder's alpha" below), `0xf0645818/1c = 000f0000`,
`0xf0645984 = 0`, `0xf0645990 = 0`, `0xf0645994 = 1`,
`0xf0645808 = 1` (written last). Their roles are not tested.

## Graphics feeder GFD0: source width and scaler

Register roles from `BVDC_P_GfxFeeder_BuildRul_isr`; effects tested on the
TV ([`gfd_hzoom_probe.s`](../docs/evidence/display/gfd_hzoom_probe.s), [`gfd_bg_probe.s`](../docs/evidence/display/gfd_bg_probe.s)).

| Register | BOLT | From Nexus | Tested |
|---|---|---|---|
| `0xf0641044` | `00000780` | source width (pixels) | **960: the feeder sends only the left 960 pixels of each line**; the right half of the screen shows the compositor background. Keeps a direct write |
| `0xf064101c` | `00100000` | horizontal step, `(step & 0x3ffff) << 3`, 1.0 = `00100000` | `00080000` (0.5) kept its value but **didn't scale** the picture |
| `0xf064117c` | `00100000` | vertical step, same format | not changed |
| `0xf0641170` | `00000031` | scaler control; Nexus sets bit 3 when the horizontal step is below 1.0 | `00000039` kept its value, no visible effect with the step above |
| `0xf06410f0–fc` | `0, 10000000, 0, 0` | horizontal filter coefficients | not changed |
| `0xf0641180/84` | `10000000, 0` | vertical filter coefficients | not changed |
| `0xf064106c` | `07800438` | output size, `width << 16 \| height` | not changed |
| `0xf0641014/18` | `00020565`, `000b0500` | pixel format | **tested**, see "Pixel formats and pitch" below |

How to make the scaler work is open: the step register keeps a direct write
but the picture isn't stretched.

### HDMI CRC: checking the picture without looking (tested 2026-10-18)

`0xf06f8014` (HDMI transmitter block, read by Nexus's
`BHDM_GetCrcValue_isr`, low 16 bits) is a checksum of what goes out on the
cable. It is running after BOLT's or kmon's set-up; nothing enables it.
Read from Linux (`kstb-reg`):

- with the text console it alternated between two values, `7fb6` and
  `2331`: the cursor blinking;
- the same picture always gives the same value: the 1920 × 1080 test
  picture of [`port/drm/kstb-fmt-test.c`](../port/drm/kstb-fmt-test.c) gave `7de8` in every
  run, after reboots too, and from RGB565 and 32-bit buffers alike;
  the same picture with red and blue swapped `13e3`;
- a changed register shows in it within 0.1 s (6 frames).

So a test can tell "this buffer is shown exactly like that one" without
anyone at the TV. It says nothing about which picture is right: one
reference has to be seen once.

### Pixel formats and pitch (tested 2026-10-18, from Linux)

The two format registers, as Nexus's `BVDC_P_GfxFeeder_BuildRul_isr` builds
them from its pixel-format code (`BPXL_eR5_G6_B5` = `06e40565`: bits 0–15
bits per component, 16–23 component positions):

```
0xf0641014  FORMAT_DEF_1 = type << 16 | A bits << 12 | R bits << 8 | G bits << 4 | B bits
0xf0641018  FORMAT_DEF_2 = A lsb << 24 | R lsb << 16 | G lsb << 8 | B lsb    (lsb 0 for a 0-bit channel)
```

Types Nexus uses: 2 for RGB565 (both component orders), 1 for its ARGB1555
codes, 4 palette, 5 alpha-only, 3 "special", 0 the rest (32-bit RGB). The
pitch register `0xf0641058` is in bytes. All three are written only by the
set-up lists, not by the per-frame list, so a direct write stays.

Tested with the HDMI CRC above:

| Change | CRC |
|---|---|
| RGB565 picture, `FORMAT_DEF_2` → `0000050b` (red at bit 0, blue at bit 11) | `13e3`, exactly the R/B-swapped picture's; back to `000b0500`: `7de8` |
| same, pitch → `0x1000` | changed (`8fea`); back to `0xf00`: `7de8` |
| `kstb-drm` with the format and pitch from the buffer ([`port/README.md`](../port/README.md), "Display") | RGB565, BGR565 (`00020565` / `0000050b`), ARGB8888 (`00008888` / `18100800`), ABGR8888 (`00008888` / `18000810`) with alpha `ff`, pitches 3840, 4096, 7680, 8192: all `7de8` |
| `FORMAT_DEF_1` `00000888` (no alpha bits) on a 32-bit buffer | changed (`6ab0`): the feeder then reads 24-bit pixels |
| type 2 with `8888` | the same as type 0 |
| type 1 with `8888` | every picture shows the compositor background, whatever the alpha byte and `0xf064103c` |

**The fourth byte of a 32-bit pixel is alpha, always**: alpha `00` showed
the compositor background (CRC `0790`), `5a` a blend (`3742`), `ff` the
picture. There is no "X" byte: XRGB8888 from a client that writes 0 there
would be invisible.

### Compositor blend and the feeder's alpha (partly understood, 2026-10-18)

`0xf0645814` (`0002ff34`, written by the per-frame list, so tested by
changing the word in BOLT's running list in RAM, `0x7db09afc`, put back
after every test; that range is otherwise left alone, a wrong word there
blanks the screen until a reboot) is the graphics window's blend control,
as `BVDC_P_Window_SetBlender_isr` packs it:

```
bits 19:16  source (2)      bits 15:8  constant alpha (ff)
bits 6:4    back factor (3) bits 2:0   front factor (4)
```

Measured with pictures of alpha `ff` and `00` (CRC `871a` = black,
`0790` = the blue background alone), back factor: 0 zero, 1 one, 2 alpha,
3 one minus alpha, 4 the constant alpha (`ff`, so one), 5–7 zero. Front
factor, with alpha `ff`: 1 and 4 show the picture, 0 and 3 give black (2
and 5–7 not measured with alpha `ff`; one run of the RGB565 picture with
front 4 gave `9778` instead of `7de8`, not repeated). **No front factor
0–7 shows an alpha-`00` pixel**: the picture is black where alpha is 0
whatever the factor, so the alpha is applied before the compositor (in
the feeder). BOLT's `34` reads as "alpha over background".

Not found: a feeder setting that ignores the alpha byte. Tried without
success: `0xf064103c` (`0000ff00`; `ffff`, `00ff`, `0`), `+0x28` and
`+0x2c`, single bits of the feeder control word `0xf0641010` (written 0 by
the per-frame list; changed in the list) and bit 1 with front factors 1, 3, 4.
Nexus computes that control word from flags (`BuildRul_isr`, near
`0x37e8xx`); their meaning is open.

**A sequence of these experiments left the feeder stuck**: every picture,
RGB565 included, gave CRC `be70` with all GFD registers back at their
values and the frame counter running; writing `1` to `0xf064100c` didn't
help, a reboot (BOLT's splash) did. The steps just before were control
word `0x100` (and earlier `0x200`–`0x2000`, which recovered at the time)
and `FORMAT_DEF_1` types 3–5. Avoid both.

## How BOLT's splash works (reverse-engineered)

From BOLT's own code, dumped from RAM and disassembled ([`../docs/02-bolt.md`](../docs/02-bolt.md) §11).
BOLT is built from Broadcom's splash app (`splash/BSEAV/app/splash/splashrun/`).

### Boot splash (`0x070107f4`, runs in `custom_early`, before the prompt)

1. Skip if env `NO_SPLASH` is set, or if `SPLASH` isn't `ENABLE`, or if
   Ctrl-S was received at boot (tested, [`../docs/03-running-code.md`](../docs/03-running-code.md)).
2. Print `SPLASH: starting`.
3. Get **SplashData**: a fixed table compiled into BOLT (at `0x07056f30`),
   holding the display set-up script (the register lists seen in front of the
   pixels) and surface descriptions.
4. Memory "glue" (`0x070112b8`): prints `SPLASH BMEM init @ 7defffff`.
5. **Read the `flash0.splash` container** (`0x07011304`). On failure:
   `SPLASH: bad file '…'` → `SPLASH: load failed` → the display is never set up.
6. Run the display script and draw `bmp0` (`0x07011608`): fills the
   **surface array** (4 pointers at `0x0706ae20`) and prints `Loaded BMP: W=… H=…`.
7. Optional audio (`pcm0`), see [`audio.md`](audio.md).

Other strings in the same code: `Splash screen disabled via Ctrl-S` (a
boot-time key), and the default file name `splash.bmp`.

### SplashData and the script runner (reverse-engineered 2026-10-16)

Read from BOLT's RAM image (`0x07008000–0x0706bfff`, sent to the PC with
kmon `tftp put 192.168.1.38:bolt_ram.bin 0x07008000 0x64000`; code-section
CRC `d4e64a5f`, the same as in [`../docs/02-bolt.md`](../docs/02-bolt.md) §11) and from BOLT's
code, disassembled as Thumb-2. Addresses in the tables are bus addresses
(`0x2xxxxxxx`); BOLT ORs in `0xd0000000`, so `0x206fa0c8` is `0xf06fa0c8`.

SplashData at `0x07056f30`:

| Offset | Value | Meaning |
|---|---|---|
| `+0x08` / `+0x0c` | `1` / `0x07056f68` | surfaces: `{…, 0x780, 0x438, pitch 0xf00, "splash.bmp"}`, entry `0x114` bytes |
| `+0x10` / `+0x14` | `1` / `0x07056f58` | displays, 16 bytes each: `{surface 0, format 22, 0x20603488, 0x2060348c}` |
| `+0x18` / `+0x1c` | `12` / `0x07056e40` | list entries, 20 bytes each; only two are used (below), the rest start with `ffffffff` |
| `+0x20` / `+0x24` | `0x565` (1381) / `0x07054318` | register table: (address, value) pairs; it ends exactly at `0x07056e40` |

The runner `0x07010374`, called at the end of the draw step `0x07011608`:

1. Writes the 1381 register pairs in table order, one store each, no delays
   or read-backs (`0x0701038e–0x070103a8`).
2. For each display, writes the surface address to both of its registers as
   64-bit stores (`strd`, high word 0): the page-flip registers `0xf0603488`
   and `0xf060348c`.
3. For each used list entry, builds its RDC lists in memory from BMEM and
   chains them (`0x070103de–0x07010726`, below).
4. Arms the even descriptors: writes `1` to descriptor + `0x20`
   (`0xf0604020` for descriptor 0) (`0x07010726–0x07010756`).

The register table by block (CPU addresses):

| Block | Writes | Block | Writes |
|---|---|---|---|
| `0xf06fa000` (HDMI) | 571 | `0xf0410000` (AON) | 34 |
| `0xf04e0000` / `0xf04e2000` / `0xf04e4000` (clocks) | 116 / 22 / 32 | `0xf0404000` (SUN_TOP) | 21 |
| `0xf1103000` | 116 | `0xf0606000`, `0xf0607000`, `0xf0608000` | 32, 32, 6 |
| `0xf0627000` | 65 | `0xf065f000` | 35 |
| `0xf06f0000`, `0xf06f2000`, `0xf06f8000` | 33, 32, 8 | `0xf041a000` | 32 |
| `0xf0080000`, `0xf0311000`, `0xf09a0000`, `0xf0c80000`, `0xf0d82000` | 32 each | others | 31 |

The two list entries, `{trigger, descriptor, count, offsets, words}`: list k
is words `offsets[k]` to `offsets[k+1] − 1` of the word array (the runner
subtracts neighbouring offsets). The second word is the RDC descriptor `d`;
its registers are at `0xf0604000 + d × 0x40`.

| Trigger | Descriptor | Lists | Offsets at | Words at | List lengths (words) |
|---|---|---|---|---|---|
| `0x18` | 0 | 8 | `0x0705707c` | `0x070570bc` | 1036, 3, 68, 47, 29, 296, 73, 73 |
| `0x19` | 1 | 6 | `0x070570a0` | `0x07058a20` | 68, 47, 29, 296, 73, 73 |

The words are RDC commands as in "List format" above (`01000000 <reg>
<value>`, `06000001 …`). The last 73-word list of descriptor 0 is the
per-frame list BOLT runs from `0x7db09ae0` ("How BOLT's lists run").

How the runner builds and starts the lists of one entry (decoded from
`0x070103de–0x07010726`; the helpers it calls are trivial: `0x070110dc`
allocates BMEM, 32-byte aligned; `0x070110ca`/`0x070110c4` return the address
unchanged; `0x070110d0` flushes the cache; `0x0701b448` is `memcpy`):

1. Every list k gets a buffer of `(len_k + 18) × 4` bytes and its words are
   copied in.
2. Every list but the last gets an 18-word tail that points the descriptor
   at the next list. `count` is the next list's length + 18 − 1, or its
   length − 1 if it is the last:
   ```
   01000000 f0605000+(d/2)*8 00000000
   01000000 f0604000+d*0x40  <next list>
   01000000 f0604010+d*0x40  06000000 | trigger << 16 | d/2     (config)
   01000000 f0604008+d*0x40  count
   01000000 f0604018+d*0x40  count
   01000000 f0605000+(d/2)*8 00000001
   ```
   This is for display format 22 (this box). For other formats the code
   leaves out the first or the last line (15 words); see `extras/mon/video.c`.
3. The last list has no tail: once reached, it runs every frame.
4. The first list is started with direct writes in the same order:
   `0xf0605000 = 0`, config, both counts (`len_0 + 18 − 1`), the list address
   (64-bit store), `0xf0605000 = 1`.

All tails in BOLT's running lists
([`../docs/evidence/display/splash0_rdc_lists_decoded.txt`](../docs/evidence/display/splash0_rdc_lists_decoded.txt))
match this word for word, and descriptor 1's counts are its list lengths
68/47/29/296/73 + 18 − 1 and 73 − 1 for the last.

### Our own display start: kmon `video init` (tested 2026-10-16)

[`../extras/mon/video.c`](../extras/mon/video.c) replays the runner. It takes SplashData from
BOLT's RAM (nothing of Broadcom's is copied into the repo), writes the
register table and the flip registers, builds the lists in its own memory at
`0x7d9f8000` and starts them as above. The framebuffer stays BOLT's
`0x7db0b700`.

**The script range is `0x07054318–0x07059347`** (`0x5030` bytes, CRC
`0ed66aea`): register table, SplashData, list offsets, and the words of both
list entries. List entry 9 (trigger `19`, 6 lists) has its words at
`0x07058a20–0x07059347`. An earlier version of this section gave
`0x07058f47` (CRC `b8aa6b9e` for `0x4c30` bytes), which leaves out the last
1 KB. The full range is the same with and without the splash (compared on
the board, both CRC `0ed66aea`).

**kmon has the range built in** (since 2026-10-17, `extras/mon/script.S` with
`extras/mon/bolt_splashdata.dat`, copied once from BOLT's RAM with `tftp put`, CRC
`0ed66aea`) and builds the lists from it, so kmon never reads BOLT's RAM for
the display. Tested 2026-10-17: BOLT's range filled with zeros, then
`video! init` with BOLT's splash lists running: 8 + 6 lists built, display
running. On a boot without the splash (Ctrl-S), kmon started the display and
audio from the built-in copy. (From 2026-10-16 to 2026-10-17 kmon copied the
range out of BOLT's RAM at start-up instead, to `0x00832000`.)

Tested on a boot where BOLT skipped its splash (Ctrl-S,
[`../docs/03-running-code.md`](../docs/03-running-code.md)):

| | Before | After `video init` |
|---|---|---|
| frame counter `0xf0603484` | standing | counts at ~60 Hz |
| descriptor 0 config | `00000000` | `06180000` |
| descriptor 0 list (reads back the previous write) | `7db09c60` (left from the last boot) | `7d9f9920` = our second-to-last list |
| TV | dark | kmon's console (checked on the TV) |

The 1381 register writes take 297 µs. The display needs a few frames after
the last write before the frame counter moves. At start-up, kmon does this
itself when the frame counter stands still, before it confirms the boot to
kboot. With BOLT's splash running, it leaves the display alone (tested).

### HDMI cable and TV (tested 2026-10-16)

Two snapshots of every readable HDMI transmitter register (`0xf06fa000–
0xf06fafff`, 571 words), with the cable in and out, differed only in the
counters and these:

| Register | Cable in | Cable out | Reading |
|---|---|---|---|
| `0xf06fa1a8` | `1` | `0` | **hot-plug detect**, bit 0 |
| `0xf06fa86c` | `3` | `0` | receiver sense (the TV terminates the lines) |
| `0xf06fa848` | `001a5a8f` | `001a5a80` | low 4 bits follow the cable (meaning unknown) |
| `0xf06fa900`, `0xf06fa918` | `0061807c` | `0061807e`, then `…7f` after replugging | bit 1 set at unplug, bit 0 at plug: latched events (inferred: interrupt status, the source of GIC 59) |

The display keeps running without a TV (frame counter moving), and nothing
needs redoing when the TV comes back:

| Test | Result |
|---|---|
| unplug, replug while kmon runs | console back on the TV by itself; kmon printed `hdmi: TV disconnected`, `hdmi: TV connected` |
| boot with no TV, BOLT's splash | the splash starts the display anyway; `video`: `no TV`; TV plugged in: console shows |
| boot with no TV, no splash (Ctrl-S) | kmon starts display and audio itself; TV plugged in: console shows, `beep` heard |

Not tested: a TV that is switched off but still connected, and other TVs.

### `load -splash`

The `load` command loads the file, then (if `-splash` was given) calls the
draw routine `0x070108e4` with the loaded image. That routine walks the
SplashData surface list. For each surface present in the array at
`0x0706ae20`, it parses the BMP, fills the background, draws the image
centred and flushes the cache. Surfaces that were never set up are skipped
**silently**. So `load -splash` can only redraw a display that step 6 of the
boot splash created.

### `flash0.splash` container format

BOLT reads the first 512 KB (`0x80000` bytes) of the partition:

```
+0x00  'G' 'Z' 'B' 'R'     magic
+0x04  u32  uncompressed size
+0x08  u32  compressed size
+0x0c  u32  CRC32 of the uncompressed data (standard zlib crc32, tested)
+0x10  compressed data      (a zlib stream as made by compress(); inflated, then CRC-checked)
```

If the magic isn't `GZBR`, BOLT uses the bytes as they are (uncompressed).
The (decompressed) payload:

```
+0x00  'B' 'R' 'C' 'M'
+0x04  u32  0x00010000       version
+0x0c  up to 4 entries of 12 bytes, to +0x3c:
       { char tag[4]; u32 size; u32 offset; }   tags "bmp0".."bmp3", "pcm0".."pcm3"
       (offset is from the start of the payload; bmp items are used in place,
        pcm items are copied out)
```

An older format, a bare BMP starting with `BM`, is still accepted with the
warning `SPLASH: Old format file in flash. Use splash_create_flash_file to
create newer format.`

Error strings tied to this format: `heap alloc failed`, `file read failed`,
`bad compressed file`, `cannot alloc`, `uncompress failed`,
`crc mismatch %x != %x`, `Invalid format, or unprogrammed`.

Tested on the modified box:

| Container | Boot log | Result |
|---|---|---|
| uncompressed `BRCM` + 320 × 180 BMP (172,916 bytes) | `Loaded BMP: W=320 H=180` | small image, centred: **BOLT doesn't scale** |
| `GZBR` + zlib(`BRCM` + 1920 × 1080 BMP): 6.2 MB → 12,080 bytes | `Loaded BMP: W=1920 H=1080` | full screen |

`tools/make-splash image.png splash.bin` builds the compressed container from
any image (use 1920 × 1080).

## Adding a splash partition (modified box)

Done on 2026-10-02. The modified box's GPT had no `splash` entry, so the space was taken from the
end of `ROOT`. `KRN`, `DTB` and everything before `ROOT` are unchanged.

| | Before | After |
|---|---|---|
| entry 8 `ROOT` | LBA 4339712–15273594 (5338.8 MB) | LBA 4339712–**15239167** (5322 MB) |
| entry 9 `splash` | — | LBA **15239168–15273594** (16.8 MB, bytes `0x1D1100000–0x1D21CF600`), type Microsoft basic data like the other Broadcom partitions |

Method (all from BOLT, over the serial console):

1. Read the primary GPT into RAM: `load -raw -rawfs -addr=0x02000000 -max=0x4400 flash0`.
   Dump it to the PC with [`tools/kstb-dump`](../tools/kstb-dump) and decode it.
2. On the PC, change `ROOT`'s end LBA, add the `splash` entry, recompute the
   entry-array CRC and the header CRC. Check with `fdisk -l` on a sparse image.
3. Upload sectors 0–5 (`0xc00` bytes; sector 0 unchanged) to RAM with
   `tools/kstb-run --no-go`, then
   `flash -noerase -mem=0x02200000 -memsize=0xc00 mem0 flash0`.
   Read it back and compare CRCs.
4. Reboot: `show devices` lists `flash0.splash`, so BOLT uses the primary GPT.
5. Write the container: `flash -noerase -mem=<addr> -memsize=<size> mem0 flash0.splash`.

BOLT uses only the first 512 KB of the partition. The rest now holds a data
file for bare-metal programs at offset 1 MB ([`storage.md`](storage.md)).

The before/after images of sectors 0–5 are in [`../docs/evidence/gpt/`](../docs/evidence/gpt/). Writing the
"before" image back with the same `flash` command restores the original table.

Constraints found on the way:

- BOLT's `-offset` options are **signed 32-bit**. Raw `flash0` offsets of 2 GB
  or more fail (`0x80000000` → 0 bytes) or wrap around (`0x100000000` → sector 0).
  Data above 2 GB has to be reached through a partition device (offset 0 of
  `flash0.splash`).
- So the **backup GPT** (at the end of the eMMC, 7.4 GB) could not be
  updated. It still describes the old layout. BOLT ignores it, but Linux or
  `fdisk` on the real disk would report the mismatch.
- `flash -noerase` from `mem0` writes exactly the given bytes at the start
  of the target, and nothing else (tested on `SWAP` with a before/after CRC
  of the first 1 MB, then restored).
