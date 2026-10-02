# Audio (HDMI sound from BOLT's splash code)

BOLT's boot splash can play a sound stored as `pcm0` in the `flash0.splash`
container. On this box that code **fails halfway**: it starts the audio
hardware but never sets up HDMI audio. Writing three HDMI registers by hand
finishes the job. After that, **the hardware loops over a plain buffer in
DRAM**, and that buffer can be rewritten from `BOLT>` or bare-metal code
while it plays.

Marked **tested** = seen/heard on the modified box (2026-10-02). Everything
else is inferred from BOLT's code (`../bolt/bolt.md` §11).

## Making sound: the confirmed recipe

1. Put a `pcm0` into the splash container and flash it (`flash0.splash` only):
   ```
   tools/make-splash --pcm sound.raw picture.png splash.bin
   tools/kstb-run --no-go --addr 0x02500000 splash.bin
   flash -noerase -mem=0x02500000 -memsize=<size> mem0 flash0.splash
   ```
   `sound.raw` = **48 kHz, 32-bit signed little-endian, stereo interleaved
   (L, R, L, R …)**, sample value in the top 16 bits. No WAV header.
2. Reboot. The log shows `SPLASH: start audio`, then
   `SPLASH: failed starting audio -1` (tested). The audio DMA is now looping
   over the buffer. The TV was silent on a first boot like this (tested), but
   see "Stopping it" below.
3. Set HDMI N/CTS for 1080p (148.5 MHz) from `BOLT>`:
   ```
   e -w 0xf06fa0c8 0x04001800      N   = 6144
   e -w 0xf06fa0cc 0x00024414      CTS = 148500
   e -w 0xf06fa0d0 0x00024414      CTS = 148500
   ```
   The sound starts immediately on the TV (tested).

**Stopping it:** `e -w 0xf0ca00c0 0` stops the DMA; the read pointer stands
still. Setting N back to 0 is **not** a reliable mute. Once the TV has had
working N/CTS, it kept playing the looping buffer as sharp, broken-up beeps,
even after a reboot of the box left N = CTS = 0 (tested).

**Write only these three registers.** BOLT's audio code also writes
`0xf06fa828` and `0xf06fa884/888/898` (step 13 below). Doing that on this box
**corrupted the picture** while still giving sound (tested). On this box those
registers already hold video settings: `0x00510300`, `0x7ef4`, `0x7cc500` and
`0x0a4d5cd5`. Putting `0xf06fa828` back did not restore the picture; a reboot did.

## What was measured

| Fact | Value | How |
|---|---|---|
| Buffer address | `0x7dada100–0x7db08eff` (192,000-byte `pcm0`) | ring buffer 0 registers after boot (tested) |
| Ring buffer 0 regs | `0xf0ca0800` read ptr, `+4` write ptr, `+8` start, `+0xc` end | values read back (tested) |
| Playback | **loops forever**; bit 31 of the read pointer flips on every pass | read pointer sampled over time (tested) |
| Data rate | ≈ 384,000 bytes/s = 48,000 frames × **8 bytes** | read pointer: one lap + 29,952 bytes in 0.578 s (tested) |
| Sample format | 32-bit stereo interleaved, 48 kHz | a 0.5 s buffer with one beep gives **2 beeps per second** (tested, by ear) |
| CPU writes reach the hardware | yes, with no cache flush | buffer rewritten with `kstb-run --no-go --addr 0x7dada100` while playing; the sound changed (tested) |
| Survives `go -64` | yes | `../bolt/raw/audio/rbuf_probe.s`: the read pointer kept advancing ~96 KB per 0.25 s under bare-metal code (tested) |
| Display lists | intact; they start at `0x7db08fa0`, just past the buffer end | compared with `../bolt/raw/display/splash0_rdc_lists_0x7db08000.txt` (tested) |

Probe output after `go -64` (read pointer every 0.25 s):
```
7dadf600  7daf6e00  fdadf800  fdaf7000  7dadfa00  7daf7200 ...
```

**Not yet explained:** about every 4th beep (≈ every 2 s) sounds different
(a "blop"). The buffer has no 2-second pattern, so the cause lies elsewhere:
the TV, the HDMI audio packets, or the hardware.

## How BOLT's audio code works (reverse-engineered)

### Where the audio step sits

The boot splash (`0x070107f4`, see `display.md`) does this after drawing `bmp0`:

```
item = lookup(1 /* "pcm" */, 0)          0x07011430
if (!item || !item->pointer)  "SPLASH: audio not present"   <- without pcm0 (tested)
else {
    "SPLASH: start audio"
    rc = splash_audio(item->size,         r0
                      item->pointer,      r1  copy of the pcm bytes (BMEM buffer)
                      0,                  r2  always 0 here (see below)
                      SplashData)         r3  0x07056f30
    if (rc) "SPLASH: failed starting audio %d"
}
```

- `r2` is the return value of the draw step `0x07011608`. The code only
  reaches this point when that value is 0, so `r2` is **always 0**.
  `0x070109cc` has a second code path for `r2 ≠ 0`, but the splash never takes
  it. `0x070109cc` has only this one caller.
- The pcm bytes are copied into a buffer from `BMEM_AllocAligned`
  (`0x070110dc`). **No header is checked or removed**: there is no `RIFF`/`WAVE`
  string in BOLT, and nothing on this path parses one. That is why `pcm0` must
  be raw samples.
- BOLT has no per-file setting for rate or format. Both are fixed in the code
  (48 kHz, 32-bit stereo).

### What `0x070109cc` does, step by step

The addresses written as `0x20caXXXX | 0xd0000000` in the code are the same
registers at `0xf0caXXXX`.

| Step | Registers | What is written | Reading (inferred) |
|---|---|---|---|
| 1 | `0xf11005c0` | clear bit 31 | probably takes the audio block out of reset or turns its clock on |
| 2 | `0xf0cb0804–0xf0cb0c8c` | many fixed values (`0x29fc8`, `0xce10`, `0x1c1a1`, `0x330bb`, …) | audio block set-up |
| 3 | — | `0x070260b4(1)` | wait 1 ms |
| 4 | `0xf04e2800–0xf04e281c`, `0xf04e0314` bit 0 cleared | `0x18a0`, `0x83126`, `0x2000064`, … | audio clock/PLL. `0xf04e…` is the clock generator area (UART clock registers are at `0xf04e0488`) |
| 5 | `0xf0ca4800–0xf0ca60c0`, `0xf0ca4000–402c`, `0xf0ca41b0…`, `0xf0ca2800–282c` | tables of `0x800000`, `0x100000`, `n << 20` … | routing/mixer tables |
| 6 | `0xf0ca01b0` | nibbles taken from the memory-controller info (`0x070123e0`, prints `DDR%d`) | tells the audio DMA where DRAM is |
| 7 | `0xf0ca0800–0xf0ca0d3f` | all zero, then the ring buffers (below) | **ring buffers** |
| 8 | `0xf0ca0048–0xf0ca0080` (15 words) | `0x420` each; then `0xf0ca0048` = `0x80000420`, later bit 0 set (read back `0x80000421`, tested) | per-channel config; channel 0 enabled |
| 9 | `0xf0cb0200`, `0xf0cb0300`, `0xf0cb0324`, `0xf0cb0c00`, `0xf0ca4000–4010`, `0xf0ca40b0`, `0xf0ca2020` | enable bits | start the data path |
| 10 | `0xf0ca00c0` bit 0 (read back 1, tested), `0xf06fa0e0` bit 0 | set | start playback; HDMI side enable |
| 11 | read `SplashData+20 → +4` (video format code) | — | choose the HDMI audio clock values. **Fails here on this box** |
| 12 | `0xf06fa0c8`, `0xf06fa0cc`, `0xf06fa0d0` | **N** and **CTS** | HDMI audio clock regeneration |
| 13 | `0xf06fa884` = `0x0d`, `+4` = `0x701`, `+0x14` = `0x8200000`; `0xf06fa828` field = `0x640d00` | | HDMI audio packet set-up. **Breaks the picture on this box** (tested) |

Returns 0 on success, 1 if the memory-controller info is missing, and **-1 if
the video format isn't one it knows**.

### Ring buffer

BOLT doesn't stream the sound. It points the hardware at the whole pcm buffer
in one go:

```
for i in 0..41:                       registers 0xf0ca0800 + i*24
    +0x00 = buf + i*0x800             read pointer
    +0x04 = buf + i*0x800             write pointer
    +0x08 = buf + i*0x800             start
    +0x0c = buf + i*0x800 + size - 1  end
```

Only ring buffer 0 is used, and it covers exactly the `pcm0` bytes. Read
back after boot (tested):
```
f0ca0800  fdadde00 7dada100 7dada100 7db08eff
          read     write    start    end
```
Bit 31 of the read pointer is a wrap flag. The rest of the read pointer is an
address inside the buffer.

With `r2 = 0` (the splash case), read = write = start, and bit 10 of the
channel config is set (`0x80000420`). The hardware then plays the whole buffer
in a loop without looking at the write pointer (tested). The unused `r2 ≠ 0`
path sets the write pointer to `end − 1` and clears bit 10 (`0x80000020`). That
is probably a "play once, then stop" mode (inferred, not tried).

### HDMI audio clock (N/CTS): why it fails here

HDMI audio needs two numbers so the TV can rebuild the audio clock:
`128 × sample rate = TMDS clock × N / CTS`. BOLT has only two cases:

| Format code | N | CTS | Check | Video modes with that clock |
|---|---|---|---|---|
| 28 | 6144 (`0x1800`) | 27000 (`0x6978`) | 27 MHz × 6144 / 27000 = 128 × **48 kHz** | 480p / 576p |
| 21 | 11648 (`0x2d80`) | 140625 (`0x22551`) | 74.176 MHz × 11648 / 140625 = 128 × **48 kHz** | 720p / 1080i at 59.94 Hz |
| anything else | not written | not written | | returns **-1** |

The format code on this box is **22** (tested, read-only):
```
BOLT> d -w 0x07056f58 0x20
07056f58  00000000 00000016 20603488 2060348c
07056f68  00000000 06e40565 00000780 00000438      0x780 × 0x438 = 1920 × 1080
```
`SplashData+20` points to `0x07056f58`, and nothing else in BOLT refers to
it, so 22 is compiled in. Writing the 1080p60 values (N = 6144, CTS = 148500)
by hand gives working sound (tested). Which exact mode code 22 is (1080p60,
or 1080p at 59.94 Hz, where the standard values are N = 5824, CTS = 140625)
isn't known.

`0xf06fa0c8` keeps BOLT's upper bits: BOLT writes `(old & 0xf7f00000) | N`.
After boot it reads `0x0c000000`, so the value to write is `0x04001800`.

## Open questions

- What the "blop" every ~2 s is.
- Whether S/PDIF can be fed the same way (BOLT's code doesn't touch it, as
  far as is known; the only related string is `audio_spdif_disable`).
- Setting up the audio block from scratch without BOLT's splash (the register
  list above is the starting point), e.g. to choose the buffer address.
- The meaning of the step 13 registers, and whether HDMI audio infoframes
  need them on other TVs.

## State after testing

The modified box's `flash0.splash` was restored to the test pattern without
`pcm0` (same 12 KB container as before the tests, CRC-checked read-back).
The boot log shows `SPLASH: audio not present` again, and the audio block is
not started. To repeat the tests, build a container with
`tools/make-splash --pcm`, as in the recipe above.
