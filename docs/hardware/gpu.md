# V3D GPU: map

The BCM7268's 3D GPU is a Broadcom **V3D 3.3** (VideoCore 3D). This page maps
it: which blocks are where, what each one does, what has been tested on the
board and what hasn't. The detailed write-ups of each test are in
`peripherals.md` (sections "V3D GPU: …"); probe sources and outputs are in
`../bolt/raw/v3d/`.

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
(`../bolt/raw/v3d/v3d_sweep_probe.s`, output `v3d_sweep_output.txt`). All
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

| Step | Section in `peripherals.md` |
|---|---|
| Power island on and off | "V3D GPU: off by its power island…" |
| Reset and Nexus default registers | "V3D GPU: reset and default registers" |
| TFU job | "V3D GPU: first job, a TFU conversion" |
| Render job: clear, store as `rgba8`, byte order | "V3D GPU: first render job…" |
| Render job straight into the framebuffer (RGB565, stride 1920) | "V3D GPU: rendering straight into the framebuffer" |
| Binner job, NV shader record, fragment shader on the QPUs: a triangle | "V3D GPU: first triangle" |
| Our own fragment shader (colour from the pixel position) | "V3D GPU: our own first QPU program" |

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
