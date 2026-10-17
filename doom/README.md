# Doom on the KSTB6077 (bare metal, AArch64 EL2)

[doomgeneric](https://github.com/ozkl/doomgeneric) (git submodule, unmodified)
running without an OS, started from BOLT with `go -64`. Video goes to HDMI,
sound effects to HDMI audio, and input comes from a USB keyboard and mouse,
the IR remote or the serial console.

Tested on the modified box on 2026-10-15: the game runs, picture and sound
effects are fine, and the IR remote controls it.

## Files

| File | What it does |
|---|---|
| `start.S` | entry point: stack, exception vectors (print ESR/ELR/FAR), FP/SIMD on (`CPTR_EL2.TFP` = 0), `.bss`, identity-mapped MMU + caches (map from [`../docs/evidence/display/vsync_probe.s`](../docs/evidence/display/vsync_probe.s)) |
| `dg_kstb.c` | platform layer: video, timer, input, WAD check, `main` |
| `dg_sound.c` | sound effects mixed into the HDMI audio ring buffer; music is a stub |
| [`../usb/`](../usb/) | OHCI1 USB driver and HID boot keyboard (shared with the monitor) |
| [`../libc/`](../libc/) | the small C library (shared with the monitor): the ~40 functions doomgeneric calls, and their headers |
| `kstb.h` | addresses and RAM layout |
| `Makefile` | clang + ld.lld → `build/doom.bin`, linked at `0x01000000` |
| [`../tools/kstb-doom`](../tools/kstb-doom) | starts it from `BOLT>` |

## Build and run

```
git submodule update --init      # once
make -C doom                      # -> doom/build/doom.bin
```

The usual way to start it is from kmon, the monitor ([`../mon/README.md`](../mon/README.md)),
which has `doom.bin` stored in flash: `tools/kstb-mon`, then `doom`. Quitting
from Doom's menu returns to the `kmon>` prompt (tested). To test a new build
without flashing, serve `doom/build` with a TFTP server ([`../docs/booting.md`](../docs/booting.md)), get the board to
`BOLT>`, then:

```
tools/kstb-doom                   # play
tools/kstb-doom --watchdog 120    # test run: the watchdog reboots the board after 120 s
```

`kstb-doom` brings up `eth0`, loads the WAD from `flash0.splash` to
`0x10000000`, loads `doom.bin` by TFTP to `0x01000000`, compares its CRC with
the local file, then runs `go -64 0x01000000`. Over TFTP the load takes about
a second; over serial (`kstb-run`) the same 570 KB takes about 7 minutes.

## RAM layout

| Range | Use |
|---|---|
| `0x00600000–0x007fffff` | USB descriptors and buffers (non-cacheable, `DMA_BASE`) |
| `0x01000000–0x0110ffff` | program: code, data, `.bss`, 256 KB stack, page tables |
| `0x10000000–0x10dc6fff` | `KWAD` image with the WAD, loaded by BOLT ([`../docs/hardware/storage.md`](../docs/hardware/storage.md)) |
| `0x20000000–0x3fffffff` | `malloc` heap; Doom's zone takes 32 MB of it (`-mb 32`) |
| `0x7d600000`, `0x7db0b700` | the two framebuffers (non-cacheable) |
| `0x7dada100–0x7db08eff` | audio ring buffer, set up by BOLT's splash (non-cacheable) |

## Video

Doom draws 320×200 with 8-bit colour indices (`CMAP256`). `DG_DrawFrame`
converts them through the palette to RGB565, scales ×5 to 1600×1000 and
writes them into the back buffer, centred at (160, 40) on the 1920×1080
screen. It then flips by writing the buffer address to `0xf0603488/8c` and
waiting for the frame counter `0xf0603484` to step
([`../docs/hardware/display.md`](../docs/hardware/display.md), "Vsync and double buffering").

## Sound

The code doesn't start the audio hardware; BOLT's boot splash does
([`../docs/hardware/audio.md`](../docs/hardware/audio.md)). The splash container in `flash0.splash`
now holds a `pcm0` of 0.5 s of silence (192,000 zero bytes, built with
`tools/make-splash --pcm`), so every boot logs:

```
SPLASH: start audio
SPLASH: failed starting audio -1
```

and the audio DMA loops over `0x7dada100–0x7db08eff`. At start-up
`dg_sound.c`:
- reads that range from the ring buffer 0 registers (`0xf0ca0808/0c`), and
  gives up (no sound, game still runs) if the read pointer `0xf0ca0800` doesn't
  move within 5 ms;
- writes HDMI N/CTS for 1080p59.94 (`0xf06fa0c8` = `0x040016c0`,
  `0xf06fa0cc/d0` = `0x00022551`), and only those three HDMI registers;
- on every frame, mixes the active effects up to 60 ms ahead of the read
  pointer, and writes silence over the rest of the ring. A stall (level load,
  wipe) then plays silence instead of repeating old sound.

Effects are the WAD's 8-bit DMX lumps (11,025 Hz), stepped to 48 kHz without
interpolation (as in the original game), panned with Doom's `sep`, and
summed and clipped to 16 bits.

Not done: music (`DG_music_module` is a stub).

## Input

USB keyboard on the USB-A port (tested 2026-10-15, "very much playable"),
driver in [`../usb/`](../usb/) ([`../docs/hardware/usb.md`](../docs/hardware/usb.md)). `main` enumerates it at
start-up (about 150 ms; without a keyboard it prints "nothing connected" and
goes on). It reports real presses and releases, several keys at once:

| Key | Doom |
|---|---|
| W / S | forward / back |
| A / D | strafe left / right |
| arrows | move / turn |
| F | fire |
| E, space | use (doors, switches) |
| Shift | run |
| Alt | strafe (with left/right) |
| Enter, Esc, Tab, Backspace | as labelled (menu, automap) |
| F1–F12, Pause | as labelled |
| other letters, digits, `-` `=` | themselves (weapons, `y`/`n`, cheat codes) |

W, A, S, D, E and F reach Doom as game keys, not as letters, so they can't
be typed (e.g. in a savegame name; not tested).

Keys are mapped as a US layout. The keyboard is polled once per `DG_GetKey`
call; a rollover report (too many keys) is ignored. The keyboard also works
behind a USB hub (tested 2026-10-17).

USB mouse (boot protocol, on the USB-A port or a hub; tested 2026-10-17 with
a Dell `413c:301a` behind the hub, "turning feels fine"):

| Mouse | Doom |
|---|---|
| left / right | turn (strafe while the strafe key or the right button is held) |
| forward / back | **nothing** ("novert", the user's choice: it walked the player by accident) |
| left button | fire |
| right button | strafe |
| middle button | forward |

The movement of all reports in one tic is summed and posted as one
`ev_mouse` at the end of the tic's key loop (Doom keeps only the last mouse
event per tic). Over 10 counts per tic counts double, as Chocolate Doom's
default acceleration. Options → Mouse Sensitivity scales it.

Neither the remote nor the serial terminal reports key releases. A key counts
as held while its repeats keep arriving. It is released 150 ms (remote) or
200 ms (terminal) after the last one.

IR remote (tested), receiver set up as in [`../docs/hardware/ir.md`](../docs/hardware/ir.md):

| Remote | Doom |
|---|---|
| arrows | move / turn |
| OK | fire, and Enter in menus |
| BACK, MENU | Esc (menu) |
| RED | use (doors, switches) |
| YELLOW | automap |
| GREEN / BLUE | `y` / `n` |
| VOL+ / VOL− | strafe right / left |
| 1–7 | weapons |
| PLAY/PAUSE | pause |

The remote sends only one key at a time, so you can't move and fire together.

Serial console in raw mode (`kstb -i`), not tested yet: arrows move, `z`
fires, space uses, `,` `.` strafe, plus Enter, Esc, Tab, digits and
F1–F12. Other letters pass through unchanged, so cheat codes work.

## WAD

The stored WAD ([`../docs/hardware/storage.md`](../docs/hardware/storage.md)) has the episode 4 maps, so
Doom runs as Ultimate Doom. It lacks the episode menu graphic `M_EPI4`, and
without it the game stopped with `W_GetNumForName: M_EPI4 not found!`
(tested). At start-up `dg_kstb.c` draws a replacement ("THY FLESH CONSUMED",
from the WAD's own `STCFN` font at ×2) into a one-lump PWAD in RAM, and passes
it with `-file kstbfix.wad`.

Before Doom starts, the WAD's CRC is checked against the `KWAD` header
(66 ms for 14.4 MB).

## Limits

- Files Doom writes (config, savegames) live in RAM and are lost at reboot.
  Saving and loading in one session: not tested.
- `malloc` is a bump allocator. `free` only gives back the newest block,
  which is enough because Doom allocates through its own zone.
- Quitting from the menu calls `exit(0)`: started from kmon, Doom silences
  its sound and returns to the `kmon>` prompt (tested); started from BOLT, it
  does a software reset back to BOLT. Errors (`I_Error`) print the message and stop.
