# V3D GPU

The BCM7268's 3D GPU is a Broadcom **V3D 3.3** (VideoCore 3D). This page maps
it: which blocks are where, what each one does, what has been tested on the
board and what hasn't. Every step that was driven is written up in
[Bring-up, step by step](#bring-up-step-by-step). Probe sources and outputs
are in [`../evidence/v3d/`](../evidence/v3d/).

Sources:
- Register names: Linux `drivers/gpu/drm/v3d/v3d_regs.h` (6.6).
- How the stock software drives it: the stock `nexus.ko` (`BVC5_` functions)
  and `libGLES_nexus.so` (`v3d_*`, `glxx_*`).
- Everything marked *tested* was done on the modified box from AArch64 EL2.

## Configuration (tested, from the ID registers)

| Register | Value | Meaning |
|---|---|---|
| hub `IDENT0` `0xf1200008` | `42554856` | ASCII "VHUB" |
| hub `IDENT1` `0xf120000c` | `000e1133` | V3D 3.3; 1 core, 1 host; has TFU, TSY, MSO; no L3 cache |
| hub `IDENT2` `0xf1200010` | `00000100` | has an MMU |
| hub `IDENT3` `0xf1200014` | `00000100` | IP revision 1 |
| core `IDENT0` `0xf1208000` | `03443356` | ASCII "V3D", version 3 |
| core `IDENT1` `0xf1208004` | `41101423` | 2 slices × 4 QPUs = **8 QPUs**, 1 TMU, 16 semaphores, VPM 4 KB, revision 3 |
| core `IDENT2` `0xf1208008` | `40078121` | not decoded (Linux names only bit 28) |
| bridge `REVISION` `0xf1204000` | `00000200` | bridge major version 2 |
| `MMU_DEBUG_INFO` `0xf1201238` | `00000aa0` | Linux's fields: PA width 10, VA width 10, MMU version 0 |

## Address map

The whole GPU sits behind a power island and aborts every access while it is
off.

| CPU address | Block | Status |
|---|---|---|
| `0xf041d020` | power island control (outside the GPU) | **tested**: up `1d00`, down `b00` |
| `0xf1200000` | hub: `AXICFG`, `UIFCFG`, `IDENT0–3`, hub interrupts `+0x50–0x64` | **tested**: `AXICFG`, `IDENT`, `INT_STS` bit 1 = TFU done |
| `0xf1200400` | TFU (texture formatting unit) | **tested**: one raster → lineartile job |
| `0xf1201000` | `MMUC_CONTROL` | written by the Nexus defaults (`1`) |
| `0xf1201200` | GPU MMU (`MMU_CTL`, page table base, hit/miss counters, violation capture) | read only; off after reset, so the GPU uses physical addresses (**tested**) |
| `0xf1204000` | bridge: `REVISION`, `SW_INIT_0` (`+0x08`), `SW_INIT_1` (`+0x0c`) | **tested**: `SW_INIT_0` pulse resets the GPU |
| `0xf1204100` | GCA: `CACHE_CTRL` (`+0x0c`), `SAFE_SHUTDOWN` (`+0xb0`) / `_ACK` (`+0xb4`), Nexus defaults at `+0x00` and `+0x20` | **tested**: safe shutdown before reset |
| `0xf1208000` | core control: `IDENT0–2`, `MISCCFG`, L2C/SLC/L2T cache control, core interrupts `+0x50–0x64` | **tested**: cache flushes before jobs; `INT_STS` bit 0 = render done, bit 1 = binner done |
| `0xf1208100` | CLE (control list executor): thread 0 = binner, thread 1 = renderer; status, current/end address, job queue `+0x15c–0x178` | **tested**: both threads, started through the queue registers |
| `0xf1208300` | PTB (primitive tile binner): current pointer, overflow memory, `BXCF` | **tested**: `BPOS` = 0; `BPCA` shows tile memory use |
| `0xf1208650–0x6fc` | performance counters (V3D 3.x layout per Linux: enable `+0x674`, clear `+0x670`, sources `+0x660/684`, counters `+0x680–0x6fc`) | read only |
| `0xf1208800` | GMP (GPU memory protection) | read only |
| `0xf1208900` | CSD (compute shader dispatch) in Linux, but only on V3D 4.1+ | read only, reads 0 |
| `0xf1208f00` | error/debug status (`FDBGO/B/R/S`, `ERR_STAT`) | read only |

## Reset values (tested)

Read right after power-up and reset, before the Nexus default registers
(`../evidence/v3d/v3d_sweep_probe.s`, output `v3d_sweep_output.txt`). All
166 addresses Linux names were read with the abort-safe read. **None
aborted**, including the CSD registers V3D 3.3 doesn't have. So the GPU
answers 0 at unused offsets, and a zero here doesn't prove a register exists.

The non-zero ones (the ID registers are in the table above):

| Register | Value |
|---|---|
| hub `AXICFG` `0xf1200000` | `0000000f` (written by the reset sequence) |
| hub `UIFCFG` `0xf1200004` | `00000045` |
| `TFU_CS` `0xf1200400` | `00002000` |
| `MMU_ADDR_CAP` `0xf1201214` | `00000fff` |
| `MMU_BYPASS_END` `0xf1201220` | `00000fff` |
| bridge `SW_INIT_1` `0xf120400c` | `00000001` |
| `GCA_CACHE_CTRL` `0xf120410c` | `00111400` |
| `MISCCFG` `0xf1208018` | `00000006` |
| `L2CACTL` `0xf1208020` | `00000001` (L2 cache enabled) |
| `L2TFLEND` `0xf1208038` | `ffffffff` |
| V3.x `PCTR_0_EN` `0xf1208674` | `80000000` |
| `GMP_STATUS` `0xf1208800` | `00000030` |
| `ERR_FDBGS` `0xf1208f10` | `00000007` |
| `ERR_STAT` `0xf1208f20` | `00001000` |

The other 143 read `0`.

## What has been driven (tested)

| Step | Section |
|---|---|
| Power island on and off | [Power island](#power-island) |
| Reset and Nexus default registers | [Reset and default registers](#reset-and-default-registers) |
| TFU job | [TFU job](#tfu-job-a-format-conversion) |
| Render job: clear, store as `rgba8`, byte order | [Render job](#render-job-a-clear-with-no-shaders) |
| Render job straight into the framebuffer (RGB565, stride 1920) | [Rendering into the framebuffer](#rendering-into-the-framebuffer) |
| Binner job, NV shader record, fragment shader on the QPUs: a triangle | [A triangle](#a-triangle-binner-and-render-jobs) |
| Our own fragment shader (colour from the pixel position) | [A fragment shader of our own](#a-fragment-shader-of-our-own) |

## Bring-up, step by step

Each step below builds on the one before it. All of them run from AArch64 at
EL2 with the CPU MMU off.

### Power island

Tested on the modified box, 2026-10-02.

Register ranges (Linux binding `brcm,bcm-v3d.yaml`, example `brcm,7268-v3d`;
the stock `nexus.ko` uses the same addresses as bus `0x212xxxxx`): hub
`0xf1200000`, bridge (reset control) `0xf1204000`, GCA cache controller
`0xf1204100`, core0 `0xf1208000`; interrupts SPI 78 and 77.

After BOLT, every V3D register aborts, the bridge included
([`v3d_probe.s`](../evidence/periph/v3d_probe.s)), although the V3D clocks
are on (see "Clocks" below). The switch is the
**power island control register `0xf041d020`**, found in the stock driver
(`nexus.ko`: `BVC5_P_HardwareBPCMPowerUp` / `…PowerDown`, bus `0x2041d020`;
[`stock-drivers.md`](../stock-drivers.md)).
[`v3d_power_probe.s`](../evidence/periph/v3d_power_probe.s) used Nexus's sequence:

| Step | Write `0xf041d020` | Wait for | Took | `0xf041d020` after |
|---|---|---|---|---|
| after BOLT | — | | | `424e0908` |
| power up | `0x00001d00` | `(v & 0x74000000) == 0x34000000` | 1,649 µs | `3c001908` |
| power down (Nexus does it only if bit 26 is set) | `0x00000b00` | `(v & 0x72000000) == 0x42000000` | 1,648 µs | `424e0908` |

Powered up, the GPU answers (abort before and after):

| Register | Value | Meaning (Linux `v3d_regs.h` field names) |
|---|---|---|
| bridge `0xf1204000` | `00000200` | revision 2.0 |
| hub IDENT0 `0xf1200008` | `42554856` | ASCII "VHUB" |
| hub IDENT1 `0xf120000c` | `000e1133` | version 3, revision 3 = **V3D 3.3**; 1 core, 1 host; bits 17–19 set (TFU, TSY, MSO) |
| hub IDENT2 `0xf1200010` | `00000100` | bit 8: has an MMU |
| hub IDENT3 `0xf1200014` | `00000100` | IP revision 1 |
| core0 IDENT0 `0xf1208000` | `03443356` | ASCII "V3D" + version 3 |
| core0 IDENT1 `0xf1208004` | `41101423` | VPM 4 KB, 16 semaphores, 1 TMU, 4 QPUs per slice, 2 slices, revision 3 |
| core0 IDENT2 `0xf1208008` | `40078121` | not decoded |

This matches the DTB's `brcm,v3d-v3.3.1.0`.

**Clocks** ([`clkgen_dump.s`](../evidence/periph/clkgen_dump.s), read after BOLT; register meanings from Nexus's
`BCHP_PWR_P_HW_ControlId`): V3D PLL regulator `0xf04e01d4` = `1`, PLL power
`0xf04e01e4` = `1`, PLL reset `0xf04e01e8` = `0` (released), channel 0
`0xf04e01c0` = `0000000c` (not held, divider 6), core clock mux `0xf04e04d0`
= `0`, V3D clock enables `0xf04e04c8` = `0000001f`. Powering the island was
enough; no clock was changed.

### Reset and default registers

Tested on the modified box, 2026-10-02.

`../evidence/v3d/v3d_reset_probe.s` (output next to it) powers up, then
replays the stock driver's bring-up: `BVC5_P_HardwareResetV3D` and
`BVC5_P_HardwareSetDefaultRegisterState` from `nexus.ko`. It reads the
registers after each step and powers down at the end. No access aborted.

**Reset** (Nexus's order; the last write is from Linux `v3d_reset_by_bridge`):

| Write | Meaning (Linux `v3d_regs.h`) | Result |
|---|---|---|
| `0xf12041b0` = `1` | GCA `SAFE_SHUTDOWN` enable | `0xf12041b4` (`SAFE_SHUTDOWN_ACK`) read `3` within the 100 ms timeout |
| `0xf1204008` = `1`, then `0` | bridge `SW_INIT_0` (bit 0: `V3D_CLK_108_SW_INIT`) | `0xf12041b0` and `…b4` read `0` again: the GCA was reset too |
| `0xf1200000` = `f` | hub `AXICFG` (`MAX_LEN` 15) | |

Right after power-up, every register already reads its reset value. The
reset changed none of the 34 registers read: a power-up starts the GPU from
reset.

**Reset values** (after power-up, and the same after the reset):

| Register | Value | Meaning |
|---|---|---|
| GCA `0xf1204100` / `0xf120410c` / `0xf1204120` | `00010002` / `00111400` / `00100000` | `+0x0c` is `CACHE_CTRL`; the others have no Linux name |
| GCA `0xf12041d0` / `0xf12041d4` | `00000001` / `00000000` | Nexus writes them from its settings (values not known) |
| hub `AXICFG` `0xf1200000` | `0000000f` | |
| hub `INT_STS` / `INT_MSK_STS` `0xf1200050` / `…5c` | `0` / `0` | no hub interrupt pending, none masked |
| `TFU_CS` `0xf1200400` | `00002000` | `NFREE` = 32, `CVTCT` = 0, not busy |
| `TFU_SU` `0xf1200404` | `0` | |
| `MMUC_CONTROL` `0xf1201000` | `0` | MMU cache off |
| `0xf1201100`, `MMU_CTL` `0xf1201200`, `MMU_PT_PA_BASE` `0xf1201204` | `0` | **GPU MMU off**, no page table |
| core `MISCCFG` `0xf1208018` | `00000006` | `QRMAXCNT` = 3 |
| core `L2TFLSTA` / `L2TFLEND` `0xf1208034` / `…38` | `0` / `ffffffff` | |
| core `INT_STS` / `INT_MSK_STS` `0xf1208050` / `…5c` | `0` / `0` | |

**Default registers** (Nexus's 15 writes in order, minus `0xf12041d0` and
`0xf12041d4`, whose values come from Nexus settings):

| Write | Meaning | Reads back |
|---|---|---|
| `0xf1208060` = `ffffffff`, `0xf1208058` = `ffffffff`, `0xf1208064` = `7` | core `INT_MSK_SET` (mask all), `INT_CLR`, `INT_MSK_CLR` (unmask bits 0–2) | `INT_MSK_STS` = `00ff0038`: the core has interrupt bits 0–5 and 16–23, and bits 0–2 are unmasked |
| `0xf1204100` = `000e0000` | GCA `+0x00` | `000e0000` |
| `0xf1204120` = `000f0002` | GCA `+0x20` | `000f0002` |
| `0xf1208018` = `1` | core `MISCCFG`: `OVRTMUOUT`, `QRMAXCNT` 0 | `00000001` |
| `0xf1200000` = `f` | hub `AXICFG` | `0000000f` |
| `0xf1201000` = `1` | `MMUC_CONTROL`: `ENABLE` | `00000001` |
| `0xf1208138` = `1` | core `CLE_RFC` | `00000000` |
| `0xf12081b0` = `3` | core `+0x1b0` (no Linux name) | `00000001` |
| `0xf1200470` = `3` | hub `+0x470` (no Linux name) | `00000001` |
| `0xf1208034` = `0`, `0xf1208038` = `ffffffff` | core `L2TFLSTA`, `L2TFLEND` | unchanged |

Three registers read back something other than what was written:
`CLE_RFC` (`0`), core `+0x1b0` (`1`) and hub `+0x470` (`1`).

### TFU job: a format conversion

Tested on the modified box, 2026-10-02.

The TFU (texture formatting unit) reads an image from memory and writes it
back in one of the GPU's tiled layouts. It needs no shader and no control
list. `../evidence/v3d/v3d_tfu_probe.s` (output next to it) does the
bring-up above, then runs one job with the **GPU MMU off** (its reset state):
the GPU uses physical addresses, and no page table is needed.

Sources: the register order and fields come from `nexus.ko`
`BVC5_P_HardwareIssueTFUJob`. The format codes come from the name tables in
the stock `libGLES_nexus.so` (`v3d_maybe_desc_tfu_type`, `…_iformat`,
`…_oformat`, `…_rgbord`). Some of the codes:

| Field | Codes |
|---|---|
| type (`ICFG` bits 9–15) | 0 `r8`, 2 `rg8`, **4 `rgba8`**, 6 `rgb565`, 7 `rgba4`, 8 `rgb5_a1`, 9 `rgb10_a2`, 14 `rgba16`, 18 `rgba16f`, 31 `rgba32f`, 32–38 ETC2/EAC, 39–47 YUV, 48–50 BC1–3, 64–77 ASTC, 90–92 YUV 3-plane |
| input format (`ICFG` bits 18–21) | **0 `raster`**, 1 `sand_128`, 2 `sand_256`, 11 `lineartile`, 12/13 `ublinear_1/2`, 14 `uif_no_xor`, 15 `uif_xor` |
| output format (`IOA` bits 3–5) | **3 `lineartile`**, 4/5 `ublinear_1/2`, 6 `uif_no_xor`, 7 `uif_xor` (no raster output) |
| RGB order (`ICFG` bits 16–17) | 0 `rgba`, 1 `abgr`, 2 `argb`, 3 `bgra` |

The job. The CPU wrote the buffers first (CPU MMU off, so they went straight
to DRAM):

| Step | Value |
|---|---|
| input `0x02000000` | 16×16 pixels, 32 bits each, pixel (x, y) = `0xaa00yyxx` |
| output `0x02100000` | 64 KB filled with `0xdeadbeef` |
| `0xf1200424` `COEF0` | `0` |
| `0xf1200420` `IOS` | `00100010` (height 16 << 16, width 16) |
| `0xf120041c` `IOA` | `02100018` (address, output format 3) |
| `0xf1200414` `IIS` | `00000010` (stride 16 **pixels**) |
| `0xf1200418` `IUA`, `0xf1200410` `ICA` | `0` |
| `0xf120040c` `IIA` | `02000000` |
| `0xf1200408` `ICFG` | `00000801` (`IOC`, type 4, input format 0): **this write starts the job** |

Result:

- `TFU_CS` went from `00002000` to `00012000`: `CVTCT` (conversions done)
  counted to 1. The poll saw it at its first read. (The 1.7 ms the probe
  printed is UART time, not job time: see
  [2D blitter, timing](2d-blitter.md#timing).)
- The hub's `INT_STS` `0xf1200050` read `00000002` (`TFUC`: TFU done). The
  CPU's IRQs were masked, so no interrupt was taken.
- The job registers `IIA` … `COEF0` read `0` both before and after the job.
  `ICFG` read back `00000801`.
- Exactly 256 words (1 KB) of the output changed, nothing in the other 63 KB.
  The CPU saw the result straight away. A GCA flush afterwards (`0xf120410c`
  = `00111401`; the flush bit cleared itself) changed nothing.
- **Layout of `lineartile`** for 32-bit pixels: the image is cut into 4×4-pixel
  utiles (64 bytes each, pixels in raster order inside). The utiles are stored
  in Z order (Morton): utile 0 = (0,0), 1 = (4,0), 2 = (0,4), 3 = (4,4),
  4 = (8,0), and so on. All 256 words match this, and each input pixel
  appears exactly once.

### Render job: a clear with no shaders

Tested on the modified box, 2026-10-03.

`../evidence/v3d/v3d_render_probe.s` (output next to it) runs a render job
with no binner and no shaders. The GPU clears one 64×64 tile to a colour in
its tile buffer, then stores the tile to RAM as a raster image. The GPU MMU
is off. Like the TFU job, this needs no page table.

Sources: the control lists follow the stock `libGLES_nexus.so`
(`create_cls_and_flush`, `glxx_hw_create_generic_tile_list`). The packet
names, sizes and fields come from its `v3d_cl_print_*` / `v3d_cl_pack_*`
functions and the opcode table `v3d_maybe_desc_cl_opcode`. The submission
follows `nexus.ko` (`BVC5_P_HardwareIssueRenderJob`,
`BVC5_P_HardwarePrepareForJob`).

Render control list (90 bytes, at `0x01001200` inside the program):

| Bytes | Packet |
|---|---|
| `79 00 40 00 40 00 40 00 0e` | tile rendering mode config, common: 1 render target, 64×64, 32 bpp, early-Z off, store render target 0 only |
| `79 02 08 1b f0 00 00 20 02` | colour config, render target 0: internal type 8-bit, output `rgba8` (27), memory format raster, address `0x02200000` |
| `79 01 00 00 00 00 00 30 02` | Z/stencil config: 32-bit float, address `0x02300000` (not stored) |
| `79 04 00 ff 00 ff 00 00 00` | clear colour: bytes 2–5 = word 0 = `0xff00ff00` |
| `79 06 00 00 40 00 00 00 00` | clear part 3: raster padded width 64 pixels |
| `79 03 00 00 00 80 3f 00 00` | Z/stencil clear values: stencil 0, depth 1.0 |
| `7c 00 00 00` | tile coordinates (0,0) |
| `1d 08 02 00 00 00 00` | store general: buffer none, clears on (a "dummy tile", as libGLES emits) |
| `13` | clear VCD cache |
| `7e 04` | tile list initial block size 64, chain |
| `7a 00 00 01 01 01 10 00 10` | supertile config: 1×1 tiles per supertile, frame 1×1 supertiles and 1×1 tiles, raster order |
| `14 <start> <end>` | generic tile list `0x01001280`–`0x01001283` (end exclusive) |
| `17 00 00` | supertile (0,0) |
| `0d` | end render |

Generic tile list (run for each tile): `7d` (implicit tile coordinates),
`18` (store subsample: render target 0 as set in the common config), `12`
(return). The CPU MMU is off, so the CPU's writes to the lists reach DRAM
directly. Each list is followed by 32 zero bytes, as libGLES does.

Submission:

| Write | Meaning |
|---|---|
| `0xf1208030` = `1` | `L2TCACTL`: flush the L2T cache |
| `0xf1208024` = `0f0f0f0f` | `SLCACTL`: flush the slice caches |
| `0xf1208058` = `7` | core `INT_CLR` |
| `0xf1208178` = `1` | `CT1QCFG` (Nexus writes `1` when no binner output is used) |
| `0xf1208164` = `01001200` | `CT1QBA`: control list start |
| `0xf120816c` = `0100125a` | `CT1QEA`: control list end; **this write starts the job** |

Result:

- Core `INT_STS` `0xf1208050` bit 0 (`FRDONE`, frame done) was set when the
  poll first looked (the printed 1.7 ms is UART time). `CLE_RFC` `0xf1208138`
  counted from 0 to 1.
  `CT1CA` = `CT1EA` = `0100125a`: the list was read to its end. `CT1CS` read
  `0` (stopped at the end, no error).
- The colour buffer held exactly 4096 words (64×64) of `ff00ff00`, in rows
  of 256 bytes. The 48 KB after it and the whole 64 KB depth buffer still
  read `deadbeef`: nothing else was written.
- The stored word equals the clear word.

**Byte order** (`v3d_order_probe.s`, output `v3d_order_output.txt`): the
same job, with the clear packet `79 04 00 11 22 33 44 00 00`. The clear word
is packet bytes 2–5, so the word was `0x33221100`. Every word of the tile
read `33221100`: the clear word's lowest byte goes to the lowest address,
with no reordering. Nothing outside the tile changed. Which byte is red was
found with a 565 output ([next section](#rendering-into-the-framebuffer)): byte 0.

### Rendering into the framebuffer

Tested on the modified box, 2026-10-03.

The same clear-only render job can store its tile as RGB565 into a surface
laid out like the framebuffer (1920×1080, pitch 3840). Three render control
list changes from the job above:

| Packet | Change |
|---|---|
| colour config `79 02 08 07 f0 <address>` | output format **7** (`bgr565`) instead of 27 (`rgba8`) |
| clear part 3 `79 06 00 00 80 07 00 00 00` | bytes 4–5: raster row stride **1920** pixels (was 64) |
| clear colour `79 04 <word> 00 00 00` | the clear word in bytes 2–5 |

The packet layouts come from libGLES `v3d_cl_tile_rendering_mode_cfg_indirect`
and `v3d_cl_rcfg_clear_colors`. libGLES's own tables
(`v3d_pixel_format_to_rt_format`) give `bgr565` internal type 8-bit and
32 bpp, the same as `rgba8`, so byte 2 of the colour config stays `08`. In
libGLES's name table, format 7 is `bgr565` and format 27 is `rgba8`.

**Scratch RAM first** (`../evidence/v3d/v3d_fb_scr_probe.s`, outputs
`v3d_fb_scr_output_<word>.txt`). The tile went to `0x031dcb40` inside a 4 MB
area at `0x03000000` filled with `deadbeef`, at the place (928,508) has on
screen. Every run changed exactly 4096 halfwords: 64 rows of 64 pixels, with
3840 bytes from row to row. Pixel 64 of each row and the row below the tile
were unchanged. The depth buffer was unchanged.

| Clear word | Bytes 0–3 | Every pixel |
|---|---|---|
| `000000ff` | `ff 00 00 00` | `f800` |
| `0000ff00` | `00 ff 00 00` | `07e0` |
| `ff000000` | `00 00 00 ff` | `0000` |
| `ffffffff` | `ff ff ff ff` | `ffff` |
| `20408000` | `00 80 40 20` | `0408` |
| `f8f80000` | `00 00 f8 f8` | `001e` |

So the clear word is **byte 0 red, byte 1 green, byte 2 blue, byte 3
alpha**. Format 7 stores ordinary RGB565 with red in the top bits, the same
layout as the framebuffer. Alpha is dropped. The 8-bit values are rounded,
not cut: `f8` became blue 30 (248 × 31 / 255 = 30.2), `80` became green 32.

**Then the screen** (`v3d_fb_tv_probe.s`, output `v3d_fb_tv_output.txt`):
the same job with the clear word `000000ff` and the tile address `0x7dce8240`
= (928,508) in the framebuffer `0x7db0b700`. A 64×64 red square appeared in
the middle of the splash screen. Reading back: the tile was `f800`. The
pixels to its right and below it still held the splash background (`07e0`).

### A triangle: binner and render jobs

Tested on the modified box, 2026-10-03.

A binner job followed by a render job draws a flat-coloured triangle. There's
no vertex shader: the vertices are given in screen pixels (an "NV" shader
record). There's one fragment shader, copied from libGLES. Probe:
`../evidence/v3d/v3d_tri_scr_probe.s` (scratch RAM), then
`v3d_tri_tv_probe.s` (screen).

**Sources.** Packet names and which list may hold them come from libGLES
`v3d_desc_cl_opcode`, `v3d_cl_instr_ok_in_bin` and
`v3d_cl_instr_ok_in_render`. `vertex_array_prims` (`24`) is allowed only in
the binning list, so the binner is needed. Packet layouts come from libGLES's
`v3d_cl_pack_*` functions. The draw packets come from `glxx_draw_rect`, and
the binning list start from `create_cls_and_flush`.

**Binner job** (Nexus `BVC5_P_HardwareIssueBinnerJob` order; register names
from Linux `v3d_regs.h`):

| Write | Meaning |
|---|---|
| `0xf1208030` = `1`, `0xf1208024` = `0f0f0f0f`, `0xf1208058` = `7` | cache flushes, `INT_CLR`, as for the render job |
| `0xf120830c` = `0` | `PTB_BPOS`: no overflow memory |
| `0xf1208170` = `04000000` | `CT0QMA`: tile allocation memory |
| `0xf1208174` = `00100000` | `CT0QMS`: its size, 1 MB |
| `0xf1208160` = binning list start | `CT0QBA` |
| `0xf1208168` = binning list end | `CT0QEA`: **starts the binner** |

Done: core `INT_STS` bit 1 (`FLDONE`) was set when the poll first looked.
`CT0CA` = `CT0EA` (the list was read to its end), `CLE_BFC` `0xf1208134` = 1,
and `PTB_BPCA` `0xf1208300` = `04003000` (12 KB of tile memory used).

**Binning control list:**

| Bytes | Packet |
|---|---|
| `78 12 00 10 04 01 10 00 00` | tile binning mode config: tile state array `0x04100000` (4 KB, zeroed) with auto-init, initial block 64 bytes, block 128 bytes; **width 1, height 1 tiles**; 1 render target, 32 bpp |
| `13`, `06` | clear VCD cache, start tile binning |
| `0e 00`, `5c 00000000` | wait transform feedback 0, occlusion query counter off (as libGLES) |
| `6b 0000 0000 4000 4000` | clip window (0,0) 64×64 |
| `6d 00000000 0000803f` | clip Z 0.0 to 1.0 |
| `60 03 70 00` | config bits: both faces, depth function "always" |
| `57 00000000` | colour write masks: all on |
| `6c` + 8 × `00` | viewport offset 0 |
| `49 44` | VCM cache size |
| `44` + record address \| 2 | `nv_shader`: the record below, 2 attribute arrays |
| `24 04 03000000 00000000` | `vertex_array_prims`: triangles, 3 vertices, first 0 |
| `04` | flush |

The tile counts in the config are **counts, not minus one**: libGLES computes
`((pixels - 1) >> shift) + 1`. A first run with `00 00 00` there read its
whole list, but `FLDONE` never came, `PTB_BPCA` moved by `0xaa000` and no
triangle was drawn (`v3d_tri_scr_output_run1_tiles0.txt`).

**NV shader record** (60 bytes, 32-byte aligned; values from libGLES
`v3d_create_nv_shader_record`, layout from `v3d_unpack_shadrec_gl_main` /
`_gl_attr`):

| Words | Value |
|---|---|
| 0–1 | `0`, `00010001` |
| 2 | address of 32 bytes of default attribute values: vec4(0,0,0,0), vec4(0,0,0,1.0) |
| 3, 4 | fragment shader code address, fragment shader uniforms address |
| 5–8 | `0` (no vertex or coordinate shader) |
| 9–11 | attribute 0: defaults address, `00000408`, `0` |
| 12–14 | attribute 1: vertex data address, `0000440b`, stride 12 |

Vertices are 12 bytes each: X and Y in pixels as fixed point (`<< 8`), then Z
(`0`). The triangle is (8,8), (56,8), (32,56).

**Fragment shader:** libGLES `v3d_clear_shader_color`, the path for 8-bit
render targets. It's 6 instructions:
`3c403186bb800000 3c003188b682d000 3c403186bb800000 3c203187b682d000`
`3c003186bb800000 3c003186bb800000`. Decoded with the V3D QPU field layout
(inferred), they are: load a uniform, write it to the tile buffer with the
next uniform as the write config, load a uniform, write it and end the
thread, two `nop`s. Uniforms: `R | G << 16` as half floats, `ffffffff`,
`B | A << 16`. Blue: `0`, `ffffffff`, `3c003c00`.

**Render job:** the RGB565 framebuffer-layout job from the previous section
(tile cleared to red), plus:
- `7b 00 00 00 04` in the render control list: tile list base, set 0,
  `0x04000000` (the tile allocation memory).
- `15 00` in the generic tile list after `7d`: branch to the binner's list
  for this tile.

**Result in scratch RAM:**
- Exactly 4096 halfwords changed: 2944 red `f800` and 1152 blue `001f`.
  1152 is the triangle's area (48 × 48 / 2).
- Per row, the blue pixels run from x = 8–55 on row 8, narrowing by one
  pixel on each side every two rows, down to 31–32 on row 54. Rows 0–7 and
  55–63 have none (`v3d_tri_scr_output_spans.txt`).

**On the screen:** the same jobs with the tile at (928,508) in the
framebuffer showed a red square with a blue triangle pointing down.
Reading the framebuffer back gave the same per-row spans
(`v3d_tri_tv_output.txt`).

### A fragment shader of our own

Tested on the modified box, 2026-10-03.

The triangle job from the previous section, with a fragment shader written
and encoded here instead of copied from libGLES
(`../evidence/v3d/v3d_grad_scr_probe.s`, output `v3d_grad_scr_output.txt`).
The encoder/disassembler is `tools/re/qpu.py`. Its instruction layout
follows Mesa's `qpu_pack.c`. libGLES's own tables match Mesa's V3D 3.3
tables entry for entry: the signal table, the magic write-address names
and the small immediates (see [QPU instructions](#qpu-instructions)).

| Word | Instruction |
|---|---|
| `3c003180bb808000` | `fxcd r0` (pixel x as a float) |
| `55e03001bbe0c022` | `fycd r1 ; fmul r0, r0, 2^-6` (small immediate) |
| `55e03046bbe40022` | `fmul r1, r1, 2^-6` |
| `3c00318835808000` | `vfpack tlbu, r0, r1`: R and G as half floats, first tile-buffer write, with the next uniform (`ffffffff`) as its config |
| `030031c63c000000` | load immediate into `tlb`: `3c000000` = B 0.0, A 1.0 |
| `3c203186bb800000` | thread switch (end of program) |
| `3c003186bb800000` ×2 | `nop` (delay slots) |

The plain `fmul` (21) and `vfpack` (53) opcode numbers were built from
Mesa's pack rules; this run confirms them.

Result: the tile was cleared to black. Exactly the 1152 triangle pixels got
a colour, with red rising from left to right, green from top to bottom, and
blue 0. All 1152 values equal this model: R = x/64 and G = y/64, with x
and y the pixel's **integer** coordinates (no +0.5), each rounded to a half
float, then **rounded** to 8 bits and on to RGB565. Truncating instead gives
970 mismatches; using x + 0.5 gives 467.

So `fxcd`/`fycd` give the integer pixel coordinates as floats. A shader can
write the tile buffer with `tlbu` (config from the uniform stream) followed
by `tlb`, and the load immediate works as a full 32-bit move into a magic
register.

## QPU instructions

`tools/re/qpu.py` disassembles and encodes 64-bit QPU instructions. Its
layout is Mesa's (`qpu_pack.c`). These tables were checked against
`libGLES_nexus.so` and match Mesa's V3D 3.3 tables entry for entry:

| libGLES table | Address | Content |
|---|---|---|
| signals | `0x19841c` | 32 entries: 1 thrsw, 2 ldunif, 4 ldtmu, 8 ldvary, 15 small_imm, 16/17 ldtlb/ldtlbu, 22 ucb, 23 rotate, 24 ldvpm, combinations in between, 18–21 invalid |
| magic write addresses | `0x1b2d50` | 0–5 r0–r4, r5quad; 6 nop; 7 tlb; 8 tlbu; 9 tmu; 10 tmul; 11 tmud; 12 tmua; 13 tmuau; 14 vpm; 15 vpmu; 16 sync; 17 syncu; 19 recip; 20 rsqrt; 21 exp; 22 log; 23 sin; 24 rsqrt2 |
| small immediates | `0x19835c` | 0–15, −16…−1, then the floats 2⁻⁸ … 2⁷ |

libGLES knows one instruction type Mesa's V3D 3.3 support doesn't:
`v3d_qpu_instr_get_type` treats mul opcode 0 with signal bits `11xxx` as a
**load immediate** (32-bit value in the low word). Its no-colour clear
shader uses it, and so does the tested gradient shader. Mul opcode 0 with
signal bits `10xxx` is a branch, and the remaining patterns are three more
types (probably semaphores and barriers: the same name table lists
`acquire`, `release`, `inc_semaphore` …), not decoded yet.

Tested on the QPUs so far: `fxcd`, `fycd`, `fmul` with a small immediate,
`vfpack`, `or`, `nop`, load immediate, `ldunif`, writes to `tlb`/`tlbu`,
`thrsw`. The ALU opcode ranges for everything else are Mesa's and untested.

## Control list opcodes (from libGLES)

libGLES has a table of every control-list opcode name
(`v3d_desc_cl_opcode`, table at `0x1b3410` in `libGLES_nexus.so`). It also
has functions that say which list may hold each one
(`v3d_cl_instr_ok_in_bin`, `v3d_cl_instr_ok_in_render`). Names and the
bin/render columns come from libGLES. "Used" means a tested probe sent it.

| Op | Name | Bin | Render | Used |
|---|---|---|---|---|
| `00` | halt | y | y | |
| `01` | nop | y | y | |
| `04` | flush | y | | yes |
| `05` | flush_all_state | y | | |
| `06` | start_tile_binning | y | | yes |
| `07` / `08` | incr_semaphore / wait_semaphore | y | y | |
| `09` | wait_prev_frame | y | y | |
| `0a` / `0b` | enable_z_only / disable_z_only | | y | |
| `0c` | end_z_only | y | y | |
| `0d` | end_render | | y | yes |
| `0e` | wait_transform_feedback | y | | yes |
| `0f` | branch_sub_autochain | y | y | |
| `10` / `11` / `12` | branch / branch_sub / return | y | y | `12` yes |
| `13` | clear_vcd_cache | y | y | yes |
| `14` | generic_tile_list | | y | yes |
| `15` | branch_implicit_tile | | y | yes |
| `16` | branch_explicit_supertile | | y | |
| `17` | supertile_coords | | y | yes |
| `18` / `19` | store_subsample / _ex | | y | `18` yes |
| `1a` / `1b` | load / end_tile | | y | |
| `1d` / `1e` | store_general / load_general | | y | `1d` yes |
| `20`–`22` | indexed prim lists (plain, indirect, instanced) | y | | |
| `24` | vertex_array_prims | y | | yes |
| `25`–`27` | indirect / instanced / single-instance vertex array prims | y | | |
| `29` / `2a` | vg_coord_array_prims / vg_inline_prims | y | | |
| `2b` / `2c` | base_vertex_base_instance / indirect_primitive_limits | y | | |
| `30` / `31` | compressed prim lists (written by the binner) | | y | |
| `34` / `35` | clipped prims (written by the binner) | | y | |
| `37` | set_primitive_id | y | | |
| `38` | prim_list_format | | y | |
| `40` | gl_shader | y | y | |
| `44` | nv_shader | y | y | yes |
| `45` / `46` | vg_shader / vg_inline_shader | y | y | |
| `49` | vcm_cache_size | y | y | yes |
| `4a` / `4b` | transform feedback enable / flush | y | | |
| `4c` / `4d` | clear_slice_caches / flush_l2t | y | y | |
| `50` | stencil_cfg | y | y | |
| `54` / `56` | blend_cfg / blend_ccolor | y | y | |
| `57` | color_wmasks | y | y | yes |
| `58` / `59` | zero_all_centroid_flags / centroid_flags | y | y | |
| `5b` | sample_state | y | y | |
| `5c` | occlusion_query_counter_enable | y | y | yes |
| `60` | cfg_bits | y | y | yes |
| `61` / `62` | zero_all_flatshade_flags / flatshade_flags | y | y | |
| `68` / `69` / `6a` | point_size / line_width / depth_offset | y | y | |
| `6b` | clip (clip window) | y | y | yes |
| `6c` | viewport_offset | y | y | yes |
| `6d` | clipz | y | y | yes |
| `6e` / `6f` | clipper_xy / clipper_z | y | | |
| `78` | tile_binning_mode_cfg | y | | yes |
| `79` | tile_rendering_mode_cfg | | y | yes |
| `7a` | multicore_rendering_supertile_cfg | | y | yes |
| `7b` | multicore_rendering_tile_list_base | | y | yes |
| `7c` / `7d` | tile_coords / implicit_tile_coords | | y | yes |
| `7e` | tile_list_initial_block_size | | y | yes |

## Not mapped yet

Blocks and functions that are named (Linux, libGLES, Nexus) but not driven
or tested on the board:

| Block / function | Where | What's known |
|---|---|---|
| GPU MMU | `0xf1201200` | registers named by Linux; off after reset; page table format not explored |
| `MMUC_CONTROL` | `0xf1201000` | only written with Nexus's default `1` |
| Performance counters | `0xf1208650–0x6fc` | read only (all 0 except `PCTR_0_EN` = `80000000`); counter sources not explored |
| GMP (memory protection) | `0xf1208800` | read only (`GMP_STATUS` = `30`) |
| Error/debug registers | `0xf1208f00` | read only (`ERR_STAT` = `1000`, `FDBGS` = `7`); meanings unknown |
| Interrupts | hub and core `INT_*` | only polled; not routed to the CPU's GIC |
| PTB overflow memory | `PTB_BPOA`/`BPOS` `0xf1208308/30c` | set to 0; never needed yet |
| L2C / SLC / L2T caches | core `+0x20`, `+0x24`, `+0x30` | flushed before jobs only |
| TMU (texture unit) | inside the core, used from shaders | not used |
| VPM, vertex and coordinate shaders | `gl_shader` (`40`) | not used |
| Varyings and interpolation | `ldvary`, flat/centroid flags (`58`–`62`) | not used |
| Depth/stencil, blending, colour write masks other than "all on" | `50`, `54`, `56`, `57`, Z/stencil stores | not used |
| Frames bigger than one tile | supertile config, tile lists per tile | only 1×1 tile |
| Other primitives and draws | indexed lists, instancing, points, lines, transform feedback | only `vertex_array_prims` with triangles |
| QPU instructions | `tools/re/qpu.py` | about ten tested; branches, flags, SFU, semaphore/barrier types not tested |
| TFU options | `0xf1200400` | one format conversion tested; mipmaps, other formats not |
| Timing | | no GPU job timed yet |
