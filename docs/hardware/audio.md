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
3. Set HDMI N/CTS for 1080p at 59.94 Hz (148.35 MHz) from `BOLT>`:
   ```
   e -w 0xf06fa0c8 0x040016c0      N   = 5824
   e -w 0xf06fa0cc 0x00022551      CTS = 140625
   e -w 0xf06fa0d0 0x00022551      CTS = 140625
   ```
   The sound starts immediately on the TV and plays clean (tested). The
   1080p60 values (N 6144 = `0x04001800`, CTS 148500 = `0x00024414`) also
   give sound, but with a periodic "blop" (see "The blop" below).

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

The loop length can be changed while running (tested): stop the DMA, write a
new end address and reset the read pointer, then start again.
```
e -w 0xf0ca00c0 0                 stop
e -w 0xf0ca080c 0x7daf17ff        end = start + 96000 - 1   (0.25 s)
e -w 0xf0ca0800 0x7dada100        read pointer = start
e -w 0xf0ca00c0 1                 start
```
The read pointer then stays below the new end, and the wrap bit keeps
flipping. Lengths tried: 0.5 s, 0.3 s, 0.25 s and 12,800 bytes (10 cycles of
a 300 Hz tone, played as a seamless hum).

## The blop: wrong CTS for this video clock

With N 6144 / CTS 148500, about every 4th beep of the 0.5 s test buffer
sounds different (a "blop"). With **N 5824 / CTS 140625 it goes away**.
All tests used the soft 300 Hz beep, were done by ear, and ran on 2026-10-02.

| Test | Setup | Heard |
|---|---|---|
| A | fresh audio boot, 6144/148500, 0.5 s loop | blop, ~every 4th beep |
| B | then 5824/140625 | **blop gone** |
| F1 | 2nd fresh boot, 6144/148500 | blop ~every 4th beep; its position **drifts slowly along the beep** |
| F2 | DMA stopped and restarted only | blop still there |
| F3 | 5824/140625 | **blop gone** |
| F4 | back to 6144/148500, same boot | blop **not** back (30–60 s) |
| H | 3rd fresh boot, 6144/148500, 0.25 s loop (4 beeps/s) | blop, about every 4th beep, drifting |
| H2 | 5824/140625 | **blop gone** |

What this shows (tested):
- The 59.94 Hz values remove the blop. This held three times out of three, on
  three boots and with two loop lengths.
- Once the 59.94 values have been written, the old values do not bring the
  blop back until the next reboot (F4; also tests C, D and E in the first
  session, which were all done after B). Something latches: the TV's audio
  clock recovery, or the HDMI block.
- Restarting the DMA has no effect (F2).

What it suggests (inferred):
- The TMDS clock is **148.5/1.001 MHz (1080p at 59.94 Hz)**, not 148.5 MHz,
  so format code 22 is 1080p59.94. (Later confirmed: the display's frame
  counter ticks every 16,683 µs = 59.94 Hz, see `display.md`, "Vsync and
  double buffering".) 5824/140625 are the standard HDMI values
  for that clock. They also follow BOLT's own pattern: code 21 (74.176 MHz)
  uses CTS 140625 too. The TV's info screen says "1920x1080@60hz", but TVs
  usually round 59.94 to 60.
- With CTS 148500, the TV rebuilds a 0.1 % slow audio clock, about 48 samples
  per second short. Its audio buffer then has to drop data now and then.
  The slow drift of the blop's position fits an event that isn't tied to the
  buffer.

Not explained:
- The blop period counted by ear was **about 4 beeps both at 2 beeps/s and at
  4 beeps/s**, so it looked tied to buffer laps rather than to a fixed time.
  A plain clock slip at a fixed rate would have given every 8th beep at
  4 beeps/s. Counting a drifting blop by ear is hard, so this may be a
  counting effect. A recording would settle it.
- A steady 300 Hz hum with the old values sounded clean after the latch (D)
  and gave "not sure" on a fresh boot (G). A slip may be hard to hear in a
  soft steady tone.

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
by hand gives sound with a periodic blop. The 1080p59.94 values (N = 5824,
CTS = 140625) give clean sound (tested), so code 22 is most likely 1080p at
59.94 Hz (inferred; see "The blop").

`0xf06fa0c8` keeps BOLT's upper bits: BOLT writes `(old & 0xf7f00000) | N`.
After boot it reads `0x0c000000`, so the value to write is `0x04001800`.

## S/PDIF (optical output)

The box has an **optical (TOSLINK)** S/PDIF jack. No S/PDIF receiver was
available, so nothing here is checked by listening.

### Not fused off (tested)

BOLT's startup banner (`0x07024924`, the function that prints
`BOLT v1.34 …`, `Board:`, `strap=`, `bond option:`) walks a table of chip
fuse bits at `0x070472a8`. Each entry is 12 bytes, **{register, mask, name}**,
and the register is stored as `0x204040xx` (BOLT ORs in `0xd0000000` →
`0xf04040xx`). For each register whose value is not 0, it prints
`otp @ <reg> = <value>:` and the name of every entry with any mask bit set.

The S/PDIF entry: **`audio_spdif_disable` = bit 8 (`0x100`) of `0xf0404030`**.
Other entries in the same register: `av_output_disable` `0x800000`, `en_cr`
`0x60`, `en_testport` `0x80`, `hdcp22_disable` `0x2000000`, `hdmi_rx_disable`
`0x4000000`, `hvd0/1_disable` `0x1000`/`0x2000`, `usb_p0/p1_disable`
`0x8000000`/`0x200000`. More at `0xf0404034` (`sata_disable` `0x400000`,
`pcie_disable` `0x8`, `hdcp_disable` `0x2`, …) and `0xf0404520` (Wi-Fi radio,
HDR).

On the modified box (boot banner, and read back with `d -w 0xf0404030 8`):
```
otp @ 0xf0404030 = 0x00000040: en_cr(0x00000060)
otp @ 0xf0404034 = 0x00a02000: macrovision_disable(0x00800000) mtsif_enc_ctl_disable(0x00002000) rv9_disable(0x00200000)
```
Bit 8 of `0xf0404030` is 0, so **S/PDIF is not disabled in the fuses**. (`en_cr`
is printed because its mask `0x60` covers the set bit 6.) Reading these two
registers is safe, since BOLT reads them on every boot. Never write them.

### BOLT's audio start lights the optical jack (tested, by eye)

| Boot | Optical jack |
|---|---|
| with `pcm0`, audio started by BOLT, beeps playing | **red light** |
| without `pcm0` (`SPLASH: audio not present`) | **dark** |

So something in `0x070109cc` drives the S/PDIF transmitter's input pin
(that probably includes its pin mux, inferred). Whether the line carries an S/PDIF
stream with our samples, or the pin is just driven high, can't be told
without a receiver (inferred: likely a stream).

### The three output ports in `0xf0cb…`

Step 9 of `0x070109cc` sets up three registers with the same layout. Read
back after an audio boot (tested):

| Register | BOLT writes | Read back | Bit 31 cleared (tested) |
|---|---|---|---|
| `0xf0cb0200` | `(old & 0xff0ffc00) \| 0x90000100` | `0x81084100` | TV sound and optical light unchanged |
| `0xf0cb0300` | `(old & ~0x3ff) \| 0x80f00102` | `0x81f84102` | **TV goes silent**, optical light stays on |
| `0xf0cb0c00` | `(old & 0xff0ffc00) \| 0x80400108`, then `\| 0x400108` | `0x81484108` | TV sound and optical light unchanged |

- **`0xf0cb0300` is the HDMI output port** (tested). Setting bit 31 again
  brings the sound back.
- All three read back with the extra bits `0x01084000`. These are kept from
  the old value, or set by the hardware (inferred). Bit 28 of BOLT's
  `0x90000100` reads back as 0 (self-clearing? inferred).
- No port's bit 31 turns the optical light off. So either the S/PDIF encoder
  keeps sending frames (silence still toggles the line), or the light comes
  from another step of the audio start (the clock set-up at `0xf04e28xx`, the
  reset bit `0xf11005c0`, `0xf0cb0240 |= 3`). Which port, if any, feeds
  S/PDIF is not known.
- Other step-9 writes, read back after an audio boot: `0xf0cb0220` and
  `0xf0cb0320` = `0x01060088` (BOLT: `|= 0x40008`), `0xf0cb0324` = `0x02000000`,
  `0xf0cb0240` = `0x0000000b` (BOLT: `|= 3`).

Next step: a receiver (a soundbar, or a USB S/PDIF input to record), then
the same port test while listening to the optical output.

## What stock Android shows (2026-10-04)

`dumpsys media.audio_policy` on the stock box lists HDMI, `Speaker`,
Bluetooth A2DP and USB outputs, and no S/PDIF device; Nexus creates 9 audio
outputs (`NEXUS_AudioOutput count:9` in the boot log) and drives S/PDIF
itself. So the shell can't tell which block feeds the optical output
(`../stock-firmware.md`).

## Open questions

- Why the blop count by ear looked tied to buffer laps (see "The blop").
- What latches after the 59.94 values are written (TV or box).
- Whether the optical output carries the samples, and which port feeds it.
  Needs a receiver.
- Setting up the audio block from scratch without BOLT's splash (the register
  list above is the starting point), e.g. to choose the buffer address.
- The meaning of the step 13 registers, and whether HDMI audio infoframes
  need them on other TVs.

## State after testing

The modified box's `flash0.splash` was restored to the test pattern without
`pcm0` (same 12 KB container as before the tests, CRC `0x985d62dc`, checked
on read-back). The boot log shows `SPLASH: audio not present` again, and the
audio block is not started. To repeat the tests, build a container with
`tools/make-splash --pcm`, as in the recipe above.
