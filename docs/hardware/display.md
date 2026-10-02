# Display (HDMI framebuffer)

BOLT can bring up HDMI itself (its splash-screen feature) and leaves a plain
linear framebuffer behind. **The display keeps running after `go -64`**, so
bare-metal code can draw on the TV by writing RAM. Everything below was tested
on a stock box (`../stock-firmware.md`); the modified box runs the same BOLT
and should behave the same (⚠️ not yet tested there). Raw captures are in
`../bolt/raw/display/`.

## Bringing up HDMI from the BOLT prompt

```
ifconfig eth0 -auto
load -raw -splash -tftp <PC IP>:test1080.bmp
```

- The file must be a **1920 × 1080, 24-bit BMP**. `../bolt/raw/display/make_test_bmp.py`
  generates the test pattern used here.
- BOLT converts it to RGB565, flips the BMP's bottom-up rows, writes it into
  the framebuffer and turns HDMI on. It prints only `… bytes read`. The BMP
  itself lands at the load address (`0x80000` by default).
- `-raw` is required: without it the default zImage loader rejects the file
  (`Bad executable format`).
- From a USB stick, `load -raw -splash usbdisk0:<file>.bmp` should work the
  same way (⚠️ untested).
- `load -splash -rawfs flash0.splash` does **not** work: the splash partition
  uses BOLT's own container format (`Invalid boot block on disk`). BOLT reads
  that partition automatically during a normal boot, before AUTOBOOT. A boot
  cancelled with Ctrl-C (`../booting.md`) skips it, so the display stays off
  until `load -splash`.

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
| `0xf0641044` | `00000780` | width, 1920 |
| `0xf0641048` | `7db0b700` | **surface address: where the pixels are** |
| `0xf0641058` | `00000f00` (written by the display lists) | pitch, 3840 bytes |
| `0xf0641174` | `00000438` (written by the display lists) | height, 1080 |

Reading `0xf0641040–0x4f` works. **Reading `0xf064105c` gives an external
abort**, which hangs BOLT. Only the offsets above are known safe to read.

Bare-metal code should read `0xf0641048` instead of hard-coding the address
(`../bolt/raw/display/fbdraw_probe.s` does exactly that).

## What's in front of the pixels: display lists (do not touch)

`0x7db08000–0x7db0b6ff` (the first `0x3700` bytes of `splash0`) holds **RDC
lists**: register-write programs that the display hardware's register-DMA
controller fetches from RAM by itself. The DTB's memory-client list names
`bvn_rdc`, and the bus arbiter lists an `rdc_0` master. The list format is
recognisable:

```
01000000 f0641058 00000f00      write 1 register: pitch = 3840
01000000 f0641174 00000438      height = 1080
06000011 f06e4140 …             write a block of registers (⚠️ count presumably in the low bits)
01000000 f0604000 7db09c60      RDC: pointer to the next list
04000000 … / 04000001 …         ⚠️ other opcodes, probably mask / read-modify-write operations
```

Only the `01000000 <register> <value>` form is understood with confidence: it
matches the width, pitch and height values found elsewhere.

Registers named in the lists: `0xf0604000/40` (RDC), `0xf0641xxx` (GFD),
`0xf06e4xxx`, `0xf06e6c00`, `0xf06fa0xx–0xf06fa8xx` (display pipeline).

**Overwriting this area blanks the screen** until the next reboot (tested by
accident). Full dump: `../bolt/raw/display/splash0_rdc_lists_0x7db08000.txt`.

## Drawing from bare-metal code: the confirmed recipe

1. In BOLT: `load -raw -splash -tftp <PC>:image.bmp` (or USB).
2. Load your program and `go -64 <addr>` (EL2, MMU off).
3. Read the base from `0xf0641048`, then write RGB565 halfwords at
   `base + y × 3840 + x × 2`.

Tested: `../bolt/raw/display/fbdraw_probe.s` read `surface @ 7db0b700` and
drew a 400 × 200 magenta box in the centre of the screen, over the BMP.
The display was still on and refreshing after `go`.

Not yet known: whether the display survives a 32-bit `go`, whether the
resolution can be changed, and the meaning of the rest of the display
pipeline's registers.
