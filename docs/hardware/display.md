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
`../bolt/raw/display/`.

## Bringing up HDMI from the BOLT prompt

```
ifconfig eth0 -auto
load -raw -splash -tftp <PC IP>:test1080.bmp
```

- The file must be a **1920 × 1080, 24-bit BMP**. `../bolt/raw/display/make_test_bmp.py`
  generates the test pattern used here.
- BOLT converts it to RGB565, flips the BMP's bottom-up rows and draws it,
  centred, into the framebuffer(s) the boot splash already set up. It prints
  only `… bytes read`. It does **not** set up the display: with no boot-splash
  surfaces it draws nothing and reports nothing. The BMP itself lands at the
  load address (`0x80000` by default).
- `-raw` is required: without it the default zImage loader rejects the file
  (`Bad executable format`).
- From a USB stick, `load -raw -splash usbdisk0:<file>.bmp` should work the
  same way (⚠️ untested).
- `load -splash -rawfs flash0.splash` does **not** work: the splash partition
  uses BOLT's own container format (`Invalid boot block on disk`). BOLT reads
  that partition itself during every boot, before the prompt and before
  AUTOBOOT. A Ctrl-C cancel (`../booting.md`) doesn't skip it.

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

1. Boot with a valid `flash0.splash` (the boot splash sets up the display).
   Optionally replace the picture: `load -raw -splash -tftp <PC>:image.bmp`.
2. Load your program and `go -64 <addr>` (EL2, MMU off).
3. Read the base from `0xf0641048`, then write RGB565 halfwords at
   `base + y × 3840 + x × 2`.

Tested: `../bolt/raw/display/fbdraw_probe.s` read `surface @ 7db0b700` and
drew a 400 × 200 magenta box in the centre of the screen, over the BMP.
The display was still on and refreshing after `go`.

Not yet known: whether the display survives a 32-bit `go`, whether the
resolution can be changed, and the meaning of the rest of the display
pipeline's registers.

## How BOLT's splash works (reverse-engineered)

From BOLT's own code, dumped from RAM and disassembled (`../bolt/bolt.md` §11).
BOLT is built from Broadcom's splash app (`splash/BSEAV/app/splash/splashrun/`).

### Boot splash (`0x070107f4`, runs in `custom_early`, before the prompt)

1. Skip if env `NO_SPLASH` is set, or if `SPLASH` isn't `ENABLE`.
2. Print `SPLASH: starting`.
3. Get **SplashData**: a fixed table compiled into BOLT (at `0x07056f30`),
   holding the display set-up script (the register lists seen in front of the
   pixels) and surface descriptions.
4. Memory "glue" (`0x070112b8`): prints `SPLASH BMEM init @ 7defffff`.
5. **Read the `flash0.splash` container** (`0x07011304`). On failure:
   `SPLASH: bad file '…'` → `SPLASH: load failed` → the display is never set up.
6. Run the display script and draw `bmp0` (`0x07011608`): fills the
   **surface array** (4 pointers at `0x0706ae20`) and prints `Loaded BMP: W=… H=…`.
7. Optional audio (`pcm0`).

Other strings in the same code: `Splash screen disabled via Ctrl-S` (a
boot-time key), and the default file name `splash.bmp`.

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

## Adding a splash partition (modified box, done 2026-10-02)

The modified box's GPT had no `splash` entry, so the space was taken from the
end of `ROOT`. `KRN`, `DTB` and everything before `ROOT` are unchanged.

| | Before | After |
|---|---|---|
| entry 8 `ROOT` | LBA 4339712–15273594 (5338.8 MB) | LBA 4339712–**15239167** (5322 MB) |
| entry 9 `splash` | — | LBA **15239168–15273594** (16.8 MB, bytes `0x1D1100000–0x1D21CF600`), type Microsoft basic data like the other Broadcom partitions |

Method (all from BOLT, over the serial console):

1. Read the primary GPT into RAM: `load -raw -rawfs -addr=0x02000000 -max=0x4400 flash0`.
   Dump it to the PC with `tools/kstb-dump` and decode it.
2. On the PC, change `ROOT`'s end LBA, add the `splash` entry, recompute the
   entry-array CRC and the header CRC. Check with `fdisk -l` on a sparse image.
3. Upload sectors 0–5 (`0xc00` bytes; sector 0 unchanged) to RAM with
   `tools/kstb-run --no-go`, then
   `flash -noerase -mem=0x02200000 -memsize=0xc00 mem0 flash0`.
   Read it back and compare CRCs.
4. Reboot: `show devices` lists `flash0.splash`, so BOLT uses the primary GPT.
5. Write the container: `flash -noerase -mem=<addr> -memsize=<size> mem0 flash0.splash`.

The before/after images of sectors 0–5 are in `../bolt/raw/gpt/`. Writing the
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
