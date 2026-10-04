# M2MC 2D blitter

The M2MC is the BCM7268's 2D blitter. It works through a list of packets in
RAM, one blit per packet. On this board it has filled, copied and scaled
RGB565 surfaces and expanded an 8-bit palette image, straight into the
framebuffer.

Everything here was tested on the modified box from AArch64 at EL2, CPU MMU
off. Probe sources and outputs are in [`../evidence/m2mc/`](../evidence/m2mc/).

## Summary

| | |
|---|---|
| Base address | **`0xf09b0000`** (bus `0x209b0000`; the stock `nexus.ko` `BGRC_` module names one core only) |
| State after BOLT | powered and clocked; answers reads |
| Clocks | `0xf04e04e8` = `7` (all on), mux `0xf04e0510` = `0` |
| SRAM power | `0xf0404628` = `0` (on) |
| Work | a chain of 32-byte-aligned packets in RAM, one blit per packet |
| Tested | solid fill, copy, 2× scaling (nearest and bilinear), 5× scaling with striping, palette-8 → RGB565, new lists without a reset |
| Speed | 320×200 palette-8 → 1600×1000 RGB565 in **1.65 ms** (≈ 0.97 billion output pixels/s) |

## Quick reference

| Do | How |
|---|---|
| Reset | see [Reset](#registers-reset-and-a-solid-fill) below (Nexus `BGRC_P_ResetDevice` order) |
| Start the first list after a reset | `+0x14` = first packet's physical address, then `+0x0c` = `6` |
| Add more work later | write the new packet's address into word 0 of the last packet sent, `dsb`, then `+0x0c` = `3` |
| Wait for the end | `+0x10` reads `2`, `+0x18` = last packet's address, `+0x1c` reads `0` |
| Mark the last packet | word 0 = `1`, bit 14 set in blit word 0 |

## Registers, reset and a solid fill

Tested on the modified box, 2026-10-03.

**After BOLT** ([`m2mc_read_probe.s`](../evidence/m2mc/m2mc_read_probe.s),
read-only, output next to it), the block is powered and answers. Register meanings come from
Nexus's `BCHP_PWR` code and `BGRC_` functions:

| Register | Value | Meaning |
|---|---|---|
| `0xf04e04e8` | `00000007` | M2MC0 clocks: bit 0 `SYSTEM_M2MC0`, bits 1–2 `HW_M2MC0`, all on |
| `0xf04e0510` | `00000000` | M2MC0 clock mux select |
| `0xf0404628` | `00000000` | M2MC0 SRAM power: bit 0 = 0 is on, bit 1 = 0 is not busy |
| `+0x0c` / `+0x10` / `+0x14` / `+0x18` / `+0x1c` | `0` | list control, list status, first packet address, current packet address, blit status (names from Nexus's watchdog message) |
| `+0x60` / `+0x64` / `+0x68` / `+0x6c` | `02008020` / `20` / `0` / `00300000` | no name; Nexus overwrites them at reset |
| `+0xb0` | `00000002` | no name; Nexus writes a value that depends on the memory configuration |
| `+0x2f0`, `+0x1808` | `0` | no name |

**Reset** ([`m2mc_fill_probe.s`](../evidence/m2mc/m2mc_fill_probe.s), Nexus `BGRC_P_ResetDevice` order):

1. `+0x1808` = 1, then 0.
2. `+0x2f0` = 1.
3. Wait until `+0x1c` reads non-zero. It read `01000000` when the probe first
   looked (the printed 1.7 ms is UART time).
4. `+0x1808` = 1, then 0.
5. `+0x2f0` = `100`, `+0x60` = `82409024`, `+0x64` = `24`, `+0x6c` =
   `00100000`, `+0x68` = `111`. All read back as written; `+0x1c` read `0`
   again.

`+0xb0` was left at its value `2`.

**How work is given to it:** a list of packets in RAM. Write the first
packet's physical address to `+0x14`, then `6` to `+0x0c`. When it's done,
`+0x10` reads `2`, `+0x18` holds the last packet's address and `+0x1c` reads
`0`.

**The packet** (Nexus `BGRC_PACKET_P_WriteHwPkt`; 32-byte aligned, 492 bytes
for all groups). It starts with 2 words: the next packet's address (`1` =
last packet), then a group mask (`7ff0` = groups 14 to 4). After that come
the groups from bit 14 down. The words below are what this probe sent; they
were rebuilt from Nexus's packet code (`BGRC_PACKET_P_ProcessSwPacket`,
`…ResetState`, `…ProcSwPktFillBlit`):

| Group (bit) | Words | Sent |
|---|---|---|
| source feeder (14) | 19 | `1`, 12 × `0`, `00211e01`, `0`, `0`, `00211e01`, `ff`, `ffff0000`: a constant-colour source |
| destination feeder (13) | 12 | all `0` except word 9 = `1e01`: no destination read |
| output feeder (12) | 10 | `1`, `0`, `02400000` (address), `80` (pitch 128), `0`, `0`, `0`, `565`, `000b0500`, `1000`: RGB565 |
| blit (11) | 20 | `00024000`, then three rectangles (source, destination, output), each `y << 16 \| x` and `width << 16 \| height`, height rounded up to 16 after the first and third; last word `00020000` |
| scaler (10) | 13 | all `0` (off) |
| blend (9) | 4 | `201`, `205`, `0`, `fa`: colour = source × 1, alpha = source alpha × 1 |
| ROP (8) | 5 | `cc`, 4 × `0` |
| colour keys (7, 6) | 5 + 5 | `0` |
| filter coefficients (5) | 16 | `0`, `400`, `0`, `200`, repeated 4 times |
| colour matrix (4) | 12 | `0` |

RGB565 is described by the 3 words `565`, `000b0500`, `1000` (channel
widths, channel positions, flags), from Nexus's
`s_BGRC_PACKET_P_DevicePixelFormats` (format 1).

**Result:** a 16×8 red rectangle at (8,4) in a 64×32 RGB565 test surface at
`0x02400000`, pitch 128. The 64 KB around it was filled with `deadbeef`
first.

- The list had finished when the probe first looked: `+0x10` =
  `2`, `+0x18` = the packet address, `+0x1c` = `0`.
- Exactly the 128 pixels of the rectangle read `f800` (red). No other
  halfword in the surface or in the rest of the 64 KB changed.
- The first run sent the size as `height << 16 | width` (`00080010`) and got
  an 8-wide, 16-tall rectangle at the same corner
  ([`m2mc_fill_output_first_run.txt`](../evidence/m2mc/m2mc_fill_output_first_run.txt)). So the size word is
  **`width << 16 | height`**, as `BGRC_PACKET_P_ProcSwPktFillBlit` builds it.
- The block stays powered after the probe, as BOLT left it.

## Copy and 2× scaling

Tested on the modified box, 2026-10-03.

**Source surface:** a 16×8 RGB565 surface at `0x02400000`, pitch 32, with
pixel (x, y) = `y << 8 | x`. Each output area was filled with `deadbeef`
first.

**Source feeder words** (19 words; Nexus `BGRC_PACKET_P_ProcessSwPaket`,
source feeder case; the rest of the group is `0`):

| Word | Value |
|---|---|
| 0 | `1` (source on) |
| 1 | `0` (address bits 32 and up) |
| 2 | source address |
| 5 | pitch in bytes |
| 11, 12, 13 | `565`, `000b0500`, `00211001` (RGB565) |
| 16 | `00211e01` |

**Rectangles:** source and output rectangles are words 1–2 and 11–12 of
the blit group: position `y << 16 | x`, size `width << 16 | height`. Words
3, 10 and 13 are heights rounded up to 16.

**Scaler group** (13 words):

| Word | Value |
|---|---|
| 0 | `14` |
| 5, 6, 7, 8 | horizontal phase as 24 bits, step, phase as 32 bits, step |
| 9, 10, 11, 12 | the same four values for vertical |

- The step is source size `<< 20` / output size; 2× is `0x80000`.
- The phase is (step − `0x100000`) / 2; 2× is `-0x40000`, which is `00fc0000`
  as 24 bits and `fffc0000` as 32 bits.
- Bit 31 of blit word 0 turns the scaler on.

[`m2mc_scale2_probe.s`](../evidence/m2mc/m2mc_scale2_probe.s) (output
[`m2mc_scale2_output.txt`](../evidence/m2mc/m2mc_scale2_output.txt)) runs one list of
three chained packets:

| Packet | What | Result |
|---|---|---|
| 1 | copy 16×8 → 16×8 at `0x02500000`, scaler off (blit word 0 = `0`) | **exact copy** |
| 2 | 16×8 → 32×16 at `0x02510000`, scaler word 0 = `04000014` (**bit 26: filter off**) | **every pixel exactly doubled** horizontally and vertically (nearest neighbour) |
| 3 | 16×8 → 32×16 at `0x02520000`, scaler word 0 = `14`, filter words `0, 400, 0, 200` (bilinear) | blended values between neighbours, e.g. `0040` between rows `0000` and `0100` |

In all three, nothing outside the output image changed.

**Several packets in one list:** word 0 of each packet holds the next
packet's address, and only the last packet has `1` there and bit 14 set in
blit word 0. After the list, `+0x18` held the last packet's address.

**Writing `6` to `+0x0c` again does not start a second list.** An earlier
run tried three separate lists ([`m2mc_scale2_output_restart.txt`](../evidence/m2mc/m2mc_scale2_output_restart.txt)). After the
first list, `+0x14` = next packet and `+0x0c` = `6` left `+0x18` at the
first packet: the 100 ms wait timed out, and nothing new was drawn. Nexus
doesn't restart a list either: it links new packets onto the last one and
writes `3` to `+0x0c`.

**Filter words `0, 400, 0, 400`** (an earlier run,
[`m2mc_scale_probe.s`](../evidence/m2mc/m2mc_scale_probe.s) /
[`m2mc_scale_output.txt`](../evidence/m2mc/m2mc_scale_output.txt)) did not give point sampling.
The 32×16 output came out about twice as bright: blue reached `1f` where the
source's highest blue is `0f`. To scale without filtering, use bit 26 of
scaler word 0.

## Large images: 320×200 → 1600×1000 on the TV

Tested on the modified box, 2026-10-03.

[`m2mc_tv_probe.s`](../evidence/m2mc/m2mc_tv_probe.s) (output
[`m2mc_tv_output.txt`](../evidence/m2mc/m2mc_tv_output.txt)) scales a
320×200 RGB565 test image 5× with the filter off (nearest neighbour).

**Striping:** with widths over 128 pixels, the blitter works in vertical
stripes. The stripe settings come from Nexus `BGRC_PACKET_P_SetScaler`,
worked out by hand from the disassembly. They go in blit words 14–19:

| Word | Value | Meaning |
|---|---|---|
| 14, 15 | `0077fff8` | input stripe width, = step × output stripe >> 4 |
| 16 | `600` | output stripe width, = ((128 − 2 × overlap) << 20) / step, rounded down to even |
| 17, 18 | `4` | overlap |
| 19 | `00030000` | bit 16: striping on (bit 17 is set at reset) |

The other words:
- Scaler word 0 = `04000014`.
- Step `0x33333` both ways.
- Phase `00f99999` (24 bits) and `fff99999` (32 bits).
- Source size `320 << 16 | 200`, output size `1600 << 16 | 1000`, rounded
  heights 208 and 1008.

**Run 1, scratch RAM:** the output went into a framebuffer-sized area at
`0x03000000` (pitch 3840, filled with `deadbeef`), at the same place as on
screen. Every one of the 1,600,000 output pixels equals source pixel
(x/5, y/5). Exactly 1,600,000 halfwords of the 4 MB area changed, so nothing
outside the rectangle was written.

**Run 2, the screen:** the CPU cleared the framebuffer (`0x7db0b700`,
1920×1080, pitch 3840) to black. A second M2MC reset followed: after a reset,
`+0x0c` = `6` starts a new list again. Then the blitter drew the same packet
with the output at (160,40) = `0x7db31040`. The picture showed on the TV,
centred: the white border, the colour steps and the blue checkerboard, each
source pixel as a sharp 5×5 block.

Each list had finished when the probe first looked. The screen list's real
time was measured later: 1.65 ms ([Timing](#timing)).

The test image's red channel is `(x >> 3) & 31`. It wraps to 0 at source
column 256, which made a hard edge 320 pixels from the right of the picture.
That edge is in the source image itself.

## Palette-8 lookup

Tested on the modified box, 2026-10-03.

The M2MC converts an 8-bit indexed image to RGB565 through a 256-entry
palette, and can scale it in the same packet. Probes and outputs are in
[`../evidence/m2mc/`](../evidence/m2mc/) (`m2mc_pal*`).

**Palette-8 source format.** Nexus `BGRC_PACKET_P_ConvertPixelFormat` maps
BPXL `0x12e40008` to BM2MC format **33**. Its table entry
(`s_BGRC_PACKET_P_DevicePixelFormats`) is `00030008, 0, 00001c00`. The source
feeder words 11–13 are then:

| Word | Value |
|---|---|
| 11 | `00030008` |
| 12 | `0` |
| 13 | `00211c01` (table word 2, ORed with `00210000` and bit 0, as Nexus does) |

**Palette.** Group bit 1 of the packet (mask `7ff2` instead of `7ff0`): 2
words, the palette's address, then `0`. They come after the colour matrix
group, at the end of the packet. Each palette entry is a 32-bit word
**`AARRGGBB`**.

**Test** ([`m2mc_pal4_probe.s`](../evidence/m2mc/m2mc_pal4_probe.s)):
- Source: a 16×8 index image, index = y·16 + x.
- Palette: entry i = `ff00a030 | i << 16`, so red = i.
- Output: 16×8 RGB565.

Every pixel came out as `(i >> 3) << 11 | 0506`: red = i, green = `a0`,
blue = `30`. Nothing outside the image changed. The 8-bit to 5- or 6-bit
step cuts off the low bits (index 7 gave red 0, index 8 gave red 1).

**Where the palette goes.** After the list, the M2MC's registers
`0xf09b0400`–`0xf09b05fc` held palette entries 0–127, in order
([`m2mc_pal_probe.s`](../evidence/m2mc/m2mc_pal_probe.s) read `+0x000`–`+0x5fc` only). So the block copies the
palette from RAM into its own registers. The same read-back shows that the
packet's groups land in registers starting at `0xf09b0104`. The source feeder
address is at `+0x10c`, the output feeder at `+0x180`, the blit group at
`+0x1a8` and the blend group at `+0x230`. That is `0x104` plus Nexus's
`s_BGRC_PACKET_P_DeviceRegisterOffsets`, and the palette group is at
`0x104 + 0x2fc` = `+0x400`. Reads of `+0xb4`–`+0xfc`, `+0x1f8` and
`+0x2f4`–`+0x3fc` aborted (synchronous external abort).

**Format 27 is not palette-8.** BPXL `0x01390008` maps to format 27
(`00058000, 0, 00000e00`). With it, the palette was still loaded, but the
colour came from the source's constant colour (source feeder word 18). With
the constant `ff123456`, every pixel was `11aa`
([`m2mc_pal2_probe.s`](../evidence/m2mc/m2mc_pal2_probe.s)). From
its BPXL code, format 27 is probably A8 (alpha only); that is inferred, not
tested.

Word 13 changes ([`m2mc_pal3_probe.s`](../evidence/m2mc/m2mc_pal3_probe.s), format 27, constant `ff123456`):

| Word 13 | Output | R, G, B (8-bit) |
|---|---|---|
| `00210e01` (Nexus) | `11aa` | `12, 34, 56`: the constant |
| `00200e01` (bit 16 cleared) | `81aa` | `80, 34, 56` |
| `00010e01` (bit 21 cleared) | `1410` | `12, 80, 80` |
| `00000e01` (both cleared) | `8410` | `80, 80, 80` |

With `00210001` (bits 9–11 cleared) the output was `0000`. Read together
with the format table, bits 9–12 of word 13 probably switch channels 0–3
off: `0e00` leaves only the alpha channel, `1c00` (palette-8) only
channel 0, which holds the index. This is inferred.

**320×200 palette image → 1600×1000 on the TV**
([`m2mc_paltv_probe.s`](../evidence/m2mc/m2mc_paltv_probe.s)).
These are the [large-image](#large-images-320200--16001000-on-the-tv) packets with the palette-8 source (pitch
320) and a palette.
- In scratch RAM, every output pixel equalled the CPU's own palette lookup of
  source pixel (x/5, y/5). Exactly 1,600,000 halfwords changed.
- On the screen, the picture showed centred.

### Timing

The probe read `CNTPCT_EL0` (27 MHz) right before writing `6` to
`+0x0c`, then polled until `+0x18` = the packet, `+0x10` = `2` and `+0x1c` =
`0`, with no UART output in between. The whole lookup + 5× scale +
framebuffer write took **44,494 and 44,513 ticks = 1.65 ms** (two runs:
[`m2mc_paltv_output.txt`](../evidence/m2mc/m2mc_paltv_output.txt),
[`m2mc_paltv_output_run2.txt`](../evidence/m2mc/m2mc_paltv_output_run2.txt)).
That is about 0.97 billion output pixels per second.

**The "1.7 ms" in earlier probe outputs is UART time.** The probes' `poll`
prints `"  poll ok after us: "` before it reads the end time. These 20
characters take 20 × 86.8 µs = 1,736 µs at 115,200 baud. That is exactly the
`000006c8` every poll printed. Those polls only show that the job was
already done at the first read.

## New lists without a reset

Tested on the modified box, 2026-10-03.

Nexus `BGRC_PACKET_P_ProcessSwPktFifo` gives the blitter new work while it
keeps running. It writes the first new packet's address into word 0 of the
last packet it sent before, then writes **`3`** to `+0x0c`. Only the very
first batch uses `+0x14` and `6`.

[`m2mc_cont_probe.s`](../evidence/m2mc/m2mc_cont_probe.s) (output
[`m2mc_cont_output.txt`](../evidence/m2mc/m2mc_cont_output.txt)), with
one M2MC reset at the start and three packets. Each packet is the 16×8
palette-8 → RGB565 packet from [Palette-8 lookup](#palette-8-lookup), with word 0 = `1` and
bit 14 set in blit word 0.

| Step | What the CPU does | Output |
|---|---|---|
| 1 | `+0x14` = A, `+0x0c` = `6`. A uses palette 1 (red = i) | `0506 … 7d06` at `0x02500000` |
| 2 | A's word 0 = B in RAM, `dsb`, `+0x0c` = `3`. B uses palette 2 (blue = i) | `0000, 0001, … 000f` at `0x02510000` |
| 3 | B's word 0 = C, `dsb`, `+0x0c` = `3`. C uses palette 1 | `0506 … 7d06` at `0x02520000` |

- After each step, `+0x18` held that step's packet, `+0x10` read `2` and
  `+0x1c` read `0`.
- All three outputs were exact, and nothing outside them changed.
- Each packet loaded its own palette.
- `+0x0c` read `6` after the start and `2` after each `3`.
- `+0x14` kept the first packet's address.

So after one reset, each new frame's packet can be linked onto the previous
one and started with `+0x0c` = `3`.

## Not tested

- Blending of two surfaces (the destination feeder was always off).
- Colour keys, ROP codes other than `cc`, the colour matrix.
- Other pixel formats than RGB565 and palette-8 (format 27 is probably A8;
  inferred).
- Interrupts: every test polled.
