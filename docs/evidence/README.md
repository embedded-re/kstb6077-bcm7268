# Evidence: probes and raw captures

Everything the manual's *tested* statements rest on: the probe programs that
were run on the board, their console output, and raw captures of BOLT and
stock-firmware sessions. Chapters cite these files directly; this page lists
them by folder.

- A **probe** (`*.s`) is a small AArch64 program, built with
  [`tools/build-a64`](../../tools/build-a64) and run with
  [`tools/kstb-run`](../../tools/kstb-run) `--a64` (`go -64`: EL2, MMU off,
  unless the probe sets up its own). Its first comment block says what it
  does. Its output is in `<name>_output.txt` next to it, unless noted.
- Most probes are read-only, and those that touch unknown addresses use the
  abort-safe read ([`../hardware/peripherals.md`](../hardware/peripherals.md),
  "The abort-safe probe"). Read a probe before running it again: some write
  registers, the display lists' copy or RAM.
- **modified box** / **stock box**: see the conventions in
  [`../README.md`](../README.md#the-two-boxes).

## Top level: BOLT sessions and CPU state

| File | What it is | Used in |
|---|---|---|
| `help_summary.txt`, `help_detailed.txt` | BOLT's `help` and `help <command>` for every command (modified box) | [BOLT](../bolt/bolt.md) |
| `info_devices_env_rmem.txt` | BOLT `info`, `show devices`, `printenv`, `rmem` (modified box) | [BOLT](../bolt/bolt.md) |
| `page_tables.txt`, `l2tables_phy_psci_rpmb.txt` | BOLT's MMU page table read from `0x07000000`; L2 tables, PHY, PSCI and RPMB queries | [BOLT](../bolt/bolt.md) §3, §7, §8 |
| `rescue.txt` | a BOLT session from the Linux-port days, including `printenv` and the `dt show` of the original device tree | [Device tree](../hardware/device-tree.md) |
| `uart_probe.txt` | UART1/UART2 set-up and loopback test, clock and pin-mux dumps, from `BOLT>` | [UARTs](../hardware/uart.md) |
| `a64_el_probe.s` | the first AArch64 program: prints its exception level | [BOLT](../bolt/bolt.md) §9 |
| `gic64b_probe.s`, `gic_probe_el2_el3.txt` | CPU system registers and GIC state, from EL2 and EL3 | [Interrupts](../hardware/interrupts.md), [BOLT](../bolt/bolt.md) §10 |

## `audio/`

| Probe | What it does | Used in |
|---|---|---|
| `rbuf_probe.s` | samples the audio ring buffer's read pointer after `go -64` (no output file; results in the chapter) | [Audio](../hardware/audio.md) |

## `display/`

| Probe | What it does | Used in |
|---|---|---|
| `fbdraw_probe.s` | reads the GFD surface address and draws a box in the framebuffer | [Display](../hardware/display.md) |
| `video_probe.s`, `video_probe_mmu.s` | redraw speed with the MMU off and on | [Display](../hardware/display.md), "Video speed" |
| `video_demo_mmu.s` | the same drawing as a demo to watch | [Display](../hardware/display.md) |
| `vsync_probe.s` | frame counter and double buffering through the RDC registers | [Display](../hardware/display.md), "Vsync" |
| `bvn_read_probe.s` | reads every register the display lists use (590, abort-safe) | [Display](../hardware/display.md), "Display lists" |
| `rdc_ownlist_probe.s`, `rdc_ownlist_visible_probe.s` | run a copy of BOLT's per-frame list from free RAM | [Display](../hardware/display.md), "Running our own list" |
| `cmp_window_probe.s`, `cmp_window_pos_probe.s` | compositor window size and position through our own list | [Display](../hardware/display.md), "Compositor CMP0" |
| `gfd_bg_probe.s`, `gfd_hzoom_probe.s` | graphics feeder source width and scaler, compositor background | [Display](../hardware/display.md), "Graphics feeder" |

| Capture | What it is |
|---|---|
| `splash0_rdc_lists_0x7db08000.txt` | raw dump of BOLT's display lists |
| `splash0_rdc_lists_decoded.txt` | the same, decoded with [`tools/re/rdc.py`](../../tools/re/rdc.py) |
| `splash0_pixels_0x7db0b600.txt` | the RAM just before and at the start of the framebuffer |
| `make_test_bmp.py` | generates the 1920 × 1080 test BMP used with `load -splash` |

## `gpt/`

The modified box's primary GPT, before and after the `splash` partition was
added ([Display](../hardware/display.md), "Adding a splash partition").
Writing a "before" image back with the same `flash` command restores the
old table.

## `i2c/`

| Probe | What it does | Used in |
|---|---|---|
| `bsc_probe.s` | reads the five BSC controllers, their two L2 interrupt controllers and the pin mux | [I2C](../hardware/i2c.md) |
| `bsc_scan.s` | scans every address on every channel with a one-byte read | [I2C](../hardware/i2c.md) |

## `ir/`

| Probe | What it does | Used in |
|---|---|---|
| `ir_read_probe.s` | read-only dump of the IR receiver channels | [IR receiver](../hardware/ir.md) |
| `ir_nec_probe.s` | enables kbd1 as an NEC decoder and prints every frame; `_allkeys_output.txt` is the run over every key | [IR receiver](../hardware/ir.md) |

## `m2mc/`

| Probe | What it does | Used in |
|---|---|---|
| `m2mc_read_probe.s` | the blitter's state after BOLT, read-only | [2D blitter](../hardware/2d-blitter.md) |
| `m2mc_fill_probe.s` | reset and a solid fill (`_first_run` = the swapped size word) | [2D blitter](../hardware/2d-blitter.md) |
| `m2mc_scale_probe.s`, `m2mc_scale2_probe.s` | copy and 2× scaling (`_restart` = the failed restart attempt) | [2D blitter](../hardware/2d-blitter.md) |
| `m2mc_tv_probe.s` | 320 × 200 → 1600 × 1000 with striping, onto the screen | [2D blitter](../hardware/2d-blitter.md) |
| `m2mc_pal_probe.s` … `m2mc_pal4_probe.s` | palette-8 lookup and the source format word | [2D blitter](../hardware/2d-blitter.md) |
| `m2mc_paltv_probe.s` | palette image onto the screen, timed | [2D blitter](../hardware/2d-blitter.md) |
| `m2mc_cont_probe.s` | new lists without a reset | [2D blitter](../hardware/2d-blitter.md) |

## `periph/`

| Probe | What it does | Used in |
|---|---|---|
| `periph_probe.s` | reads the first word of every known block (abort-safe) | [Peripheral blocks](../hardware/peripherals.md) |
| `clk_gate_probe.s` | the clock gates of the switched-off blocks (output `clock_gate_probe_output.txt`) | [Peripheral blocks](../hardware/peripherals.md) |
| `swinit_probe.s` | SUN_TOP_CTRL `0xf0404300–0x33c`, around BOLT's reset registers (output `sw_init_probe_output.txt`) | [Peripheral blocks](../hardware/peripherals.md) |
| `pcie_usbctrl_probe.s` | words in the PCIe range and the whole USB control block | [Peripheral blocks](../hardware/peripherals.md) |
| `reset_release_probe.s` | releases one reset bit at a time (PCIe, USB BDC) (output `reset_release_output.txt`) | [Peripheral blocks](../hardware/peripherals.md) |
| `clkgen_dump.s` | every word of the clock generator `0xf04e0000–0xf04e7fff` | [Peripheral blocks](../hardware/peripherals.md), [3D GPU](../hardware/gpu.md) |
| `bpcm_read_probe.s` | reads the V3D power-island control register named by `nexus.ko`, plus two V3D registers | [3D GPU](../hardware/gpu.md) |
| `v3d_probe.s` | the V3D registers after BOLT (all abort) | [3D GPU](../hardware/gpu.md) |
| `v3d_power_probe.s` | V3D power island up and down (output `v3d_power_output.txt`) | [3D GPU](../hardware/gpu.md) |

## `ram/`

| Probe | What it does | Used in |
|---|---|---|
| `ram_scan.s` | read-only scan of RAM after `go -64` | [Memory map](../hardware/memory-map.md) |
| `ram_test.s`, `ram_gaps_probe.s` | pattern test of the free RAM, and the ranges it left out | [Memory map](../hardware/memory-map.md) |
| `bolt_reuse_probe.s` | whether BOLT's RAM is free after `go` | [Memory map](../hardware/memory-map.md) |

## `sdhci_genet/`

| File | What it is | Used in |
|---|---|---|
| `sdhci_genet_probe.s` | reads both SDHCI controllers and the GENET blocks | [Storage](../hardware/storage.md), [Ethernet](../hardware/ethernet.md) |
| `mii_read_output.txt` | PHY registers read with BOLT's `mii read` | [Ethernet](../hardware/ethernet.md) |

## `smp/`

| Probe | What it does | Used in |
|---|---|---|
| `psci_cpu_on_probe.s` | starts cores 1–3 with PSCI `CPU_ON` (output `psci_cpu_on_output.txt`) | [CPU cores](../hardware/cpu-cores.md) |

## `stock/`: the untouched box

| File | What it is | Used in |
|---|---|---|
| `android-normal-boot.txt` | a full normal boot into Android | [Stock firmware](../stock-firmware.md) |
| `android-recovery-to-bolt.txt` | normal boot → recovery (SW4) → "Reboot to bootloader" → `BOLT>` | [Stock firmware](../stock-firmware.md) |
| `attempts/` | earlier boot captures, including resets with `RR:00000000` | [Stock firmware](../stock-firmware.md) |
| `help_summary.txt`, `help_android.txt`, `info_devices_env_rmem.txt` | a read-only BOLT session | [Stock firmware](../stock-firmware.md) |
| `aon_read_abort.txt` | the AON control read that aborted at `0xf041002c` | [Stock firmware](../stock-firmware.md) |
| `ctrlc_cancel_after_reset.txt` | Ctrl-C cancelling autoboot after a reset | [Stock firmware](../stock-firmware.md) |
| `adb/` | Android 11 shell output: `/proc/interrupts`, kernel config, modules, meminfo, mounts, `getprop`, `dumpsys input` and `audio_policy`, `/dev` listing | [Stock firmware](../stock-firmware.md), [Interrupts](../hardware/interrupts.md) |

## `sys/`

| Probe | What it does | Used in |
|---|---|---|
| `addrmap1_probe.s` | wake-timer alarm, every word of the GISB arbiter block, the boot SRAM | [Peripheral blocks](../hardware/peripherals.md), [Memory map](../hardware/memory-map.md), [System blocks](../hardware/system-blocks.md) |
| `l2_probe.s` | all ten L2 interrupt controllers | [Interrupts](../hardware/interrupts.md) |
| `sys_probe.s` | reset history, straps, OTP, general control registers | [System blocks](../hardware/system-blocks.md) |

## `usb/`

| Probe | What it does | Used in |
|---|---|---|
| `usb_state_probe.s` | the USB controllers' registers after `go -64` (no output file; results in the chapter) | [USB](../hardware/usb.md) |
| `ohci_port_probe.s` | brings up OHCI1 to an enabled port (no output file; results in the chapter) | [USB](../hardware/usb.md) |

## `v3d/`

| Probe | What it does | Used in |
|---|---|---|
| `v3d_sweep_probe.s` | reads every register Linux names, after power-up | [3D GPU](../hardware/gpu.md), "Reset values" |
| `v3d_reset_probe.s` | power-up, reset and the Nexus default registers | [3D GPU](../hardware/gpu.md) |
| `v3d_tfu_probe.s` | the first job: a TFU format conversion | [3D GPU](../hardware/gpu.md) |
| `v3d_render_probe.s`, `v3d_order_probe.s` | a clear-only render job; the clear word's byte order | [3D GPU](../hardware/gpu.md) |
| `v3d_fb_scr_probe.s`, `v3d_fb_tv_probe.s` | render job in the framebuffer's layout: scratch RAM (one output per clear word), then the screen | [3D GPU](../hardware/gpu.md) |
| `v3d_tri_scr_probe.s`, `v3d_tri_tv_probe.s` | binner + render job: a triangle, scratch RAM then the screen | [3D GPU](../hardware/gpu.md) |
| `v3d_grad_scr_probe.s` | the same triangle with our own fragment shader | [3D GPU](../hardware/gpu.md) |
