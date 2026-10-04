// KSTB6077 / BCM7268 SoC map: the data behind docs/map/index.html.
//
// Every block and connection here comes from the manual (docs/). Add a new
// finding here, with its status, and the map redraws itself.
//
// status (blocks and connections):
//   tested    read, written or seen on the board
//   dtb       named by a device tree, not tested
//   nexus     from the stock Broadcom drivers (nexus.ko), not tested
//   bolt      from BOLT's disassembled code, not tested
//   inferred  a conclusion, not checked directly
//   unknown   exists, but its address or role is not known
//   seen      a part visible on the PCB, not tested electrically
//
// layers (connections):
//   reg     register access over the GISB bus (CPU or RDC writes a block's registers)
//   irq     interrupt: source -> L2 controller bit -> GIC ID
//   data    DMA / memory traffic between a block and DRAM
//   stream  on-chip data stream between blocks (pixels, audio samples)
//   pwr     clock gates, power islands, resets
//   ext     board wiring: pins and buses from the SoC to parts on the PCB

window.KSTB_MAP = {
  doc_base_local: "../",
  doc_base_web: "https://github.com/embedded-re/KAON-KSTB6077/blob/main/docs/",

  layers: [
    { id: "reg",    label: "Register bus",      hint: "CPU or RDC writing a block's registers over GISB" },
    { id: "irq",    label: "Interrupts",        hint: "source → L2 bit → GIC ID" },
    { id: "data",   label: "DRAM data",         hint: "DMA and memory traffic" },
    { id: "stream", label: "On-chip streams",   hint: "pixels and audio between blocks" },
    { id: "pwr",    label: "Clock / power / reset", hint: "gates, power islands, reset bits" },
    { id: "ext",    label: "Board wiring",      hint: "pins and buses from the SoC to parts on the PCB" }
  ],

  // Areas on the drawing. x, y, w, h in drawing units; cols = node grid columns.
  // Areas on the drawing. x, y, w, h in drawing units; cols = node grid columns;
  // rail = y of the interrupt rail this group taps into.
  groups: [
    { id: "cpu",   label: "CPU cluster",          x: 36,   y: 56,  w: 300, h: 180, cols: 2, rail: 248 },
    { id: "gicg",  label: "Interrupts",           x: 352,  y: 56,  w: 170, h: 180, cols: 1 },
    { id: "sram",  label: "Boot SRAM",            x: 538,  y: 56,  w: 150, h: 180, cols: 1, rail: 248 },
    { id: "hif",   label: "HIF",                  x: 704,  y: 56,  w: 320, h: 180, cols: 2, rail: 248 },
    { id: "memc",  label: "Memory controller",    x: 1040, y: 56,  w: 316, h: 180, cols: 1, rail: 248 },

    { id: "sys",   label: "System control",       x: 36,   y: 322, w: 300, h: 330, cols: 2, rail: 308 },
    { id: "upg",   label: "UPG peripherals",      x: 352,  y: 322, w: 336, h: 330, cols: 2, rail: 308 },
    { id: "aon",   label: "Always-on (AON)",      x: 704,  y: 322, w: 336, h: 330, cols: 2, rail: 308 },
    { id: "io",    label: "I/O controllers",      x: 1056, y: 322, w: 300, h: 330, cols: 1, rail: 308 },

    { id: "disp",  label: "Display pipeline",     x: 36,   y: 680, w: 652, h: 180, cols: 5, rail: 666 },
    { id: "gfx",   label: "Graphics",             x: 704,  y: 680, w: 336, h: 180, cols: 2, rail: 666 },
    { id: "aud",   label: "Audio",                x: 1056, y: 680, w: 300, h: 180, cols: 1, rail: 666 },

    { id: "xpt",   label: "Transport / security", x: 36,   y: 888, w: 652, h: 232, cols: 2, rail: 874 },
    { id: "unk",   label: "Named, not mapped",    x: 704,  y: 888, w: 652, h: 232, cols: 3, rail: 874 },

    { id: "dram",  label: "Regions after go -64", x: 1420, y: 76,  w: 300, h: 1044, cols: 1, offchip: true },
    { id: "board", label: "", x: 0, y: 0, w: 0, h: 0, cols: 1, hidden: true }
  ],

  // The PCB. Sides follow the photos docs/images/PCB_Front.jpg / PCB_Back.jpg: rear connectors along
  // the top, front LEDs / IR / SW1 / UART header along the bottom.
  board: { x: -360, y: -290, w: 2450, h: 1640,
           silk: "DT-DVB-T Android TV rev1.0 · KM_SH368AT",
           holes: [[-318, -248], [2048, -248], [-318, 1308], [2048, 1308]],
           edges: [["rear panel", 870, -268], ["front", 870, 1338]] },

  // Chip packages drawn on the board.
  packages: [
    { id: "soc",  x: 16,   y: 16, w: 1360, h: 1124, label: "U1 · BCM7268 B0", sub: "7268ZBKFEBB03 · 4 × B53 · 1656 MHz" },
    { id: "ddr",  x: 1404, y: 16, w: 332,  h: 1124, label: "K4F6E3S4HM-MGCJ", sub: "LPDDR4 · 2 GB · back side, under U1", back: true }
  ],

  // The GISB register bus, drawn as a spine across the die, with drops in the gaps.
  spine: { y: 276, h: 30, x1: 36, x2: 1356,
           label: "GISB register bus · CPU 0xf0000000–0xf12fffff = bus 0x20000000–0x212fffff",
           groups: ["sys", "upg", "aon", "io", "hif", "memc"],
           drops: [[340, 291, 680], [692, 291, 1120], [1044, 291, 888]] },

  // Interrupt bus: rails between the rows, risers in the gaps, into the GIC.
  irqbus: { trunk: 248, rails: [248, 308, 666, 874], x1: 36, x2: 1356,
            risers: [[348, 666], [700, 874], [1052, 874]], gic_x: 437 },

  // CPU address -> bus address: 0xf0000000–0xf12fffff = 0x20000000–0x212fffff
  // (peripherals.md "Bus addresses"; BOLT and Nexus use the same offset).
  bus: { lo: "0xf0000000", hi: "0xf12fffff", offset: "0xd0000000" },

  // Addresses the manual names as "nothing here".
  holes: [
    { addr: "0xf0402800", note: "Not a block: a false hit in BOLT's code. Reading it aborts." }
  ],

  nodes: [
    // ---- CPU cluster ----
    { id: "cores", g: "cpu", span: 2, label: "4 × Brahma-B53", sub: "ARMv8-A · MIDR 420f1000", status: "tested",
      after: "go -64: core 0 at EL2, MMU and caches off. Cores 1–3 off until PSCI CPU_ON.",
      regs: [["MIDR_EL1", "420f1000", "Broadcom Brahma-B53"], ["SCTLR_EL2", "30c50830", "MMU, caches off at entry"], ["VBAR_EL2", "garbage", "set your own vector table"]],
      doc: "hardware/cpu-cores.md" },
    { id: "timer", g: "cpu", label: "Generic timer", sub: "CNTFRQ 27 MHz", status: "tested",
      after: "Running. No MMIO address: system registers only.",
      regs: [["CNTFRQ_EL0", "019bfcc0", "27,000,000 Hz"], ["CNTPCT_EL0", "", "counter"]],
      doc: "hardware/system-blocks.md" },
    { id: "l2c", g: "cpu", label: "L2 cache", sub: "1 MB", status: "bolt",
      after: "Size from BOLT's banner.", doc: "../README.md" },

    // ---- GIC ----
    { id: "gic", g: "gicg", label: "GIC-400", sub: "0xffd01000 / 0xffd02000", range: ["0xffd01000", "0xffd02fff"], status: "tested",
      after: "Enabled. Every SPI in group 1 (non-secure), every SPI disabled and untargeted.",
      regs: [["0xffd01000", "GICD_CTLR", "1 (non-secure view)"], ["0xffd01004", "GICD_TYPER", "0000fc67: 256 IDs, 4 CPUs"], ["0xffd01080+", "IGROUPR", "ffffffff: all SPIs group 1"], ["0xffd01100+", "ISENABLER", "SPIs all 0"], ["0xffd02000", "GICC_CTLR", "1"]],
      note: "On the CPU's private bus, not on GISB. Unmapped under BOLT's MMU: reach it from go -64 or EL3 only.",
      doc: "hardware/interrupts.md" },

    // ---- Boot SRAM ----
    { id: "bootsram", g: "sram", label: "Boot SRAM", sub: "0xffe00000 · 128 KB", range: ["0xffe00000", "0xffe1ffff"], status: "tested",
      after: "All zero except a leftover BOLT stack frame at 0xffe0ff24.",
      doc: "hardware/memory-map.md" },

    // ---- HIF ----
    { id: "cpubiu", g: "hif", label: "CPU BIU", sub: "0xf0205000", range: ["0xf0205000", "0xf02050ff"], status: "dtb",
      note: "PSCI sets the bit for core n at +0x8c to power it on (from PSCI's console messages; inferred).",
      doc: "hardware/cpu-cores.md" },
    { id: "hifcont", g: "hif", label: "HIF continuation", sub: "0xf0452000", range: ["0xf0452000", "0xf04520ff"], status: "dtb",
      note: "PSCI writes each core's start address at +0x8 / +0x10 / +0x18 (from PSCI's messages; inferred).",
      doc: "hardware/cpu-cores.md" },
    { id: "hifl2", g: "hif", label: "HIF L2", sub: "0xf0201000", range: ["0xf0201000", "0xf020102f"], status: "tested",
      after: "STATUS 03000000 (NAND bits), all masked.", doc: "hardware/interrupts.md" },
    { id: "hifspil2", g: "hif", label: "HIF SPI L2", sub: "0xf0201a00", range: ["0xf0201a00", "0xf0201a2f"], status: "tested",
      after: "STATUS 0, mask 7f.", doc: "hardware/interrupts.md" },

    // ---- Memory controller ----
    { id: "memc", g: "memc", label: "MEMC0", sub: "0xf1100000 · rev B.2.2", range: ["0xf1100000", "0xf1102fff"], status: "tested",
      after: "Running; set up by the boot loaders. First words match the DTB's revisions.",
      regs: [["0xf1100000", "general", "0000b220: rev B.2.2"], ["0xf1101000", "arbiter", "00000148"], ["0xf1102000", "DDR", "0000aa65"]],
      note: "The DTB's client list names the DMA masters: bvn_*, hvd0, m2mc_0–2, raaga, aud_aio, v3d, sid, xpt_*, vec_*.",
      doc: "hardware/peripherals.md" },
    { id: "ddrphy", g: "memc", label: "DDR PHY", sub: "0xf1108000 · 0xf1120000", range: ["0xf1108000", "0xf1120fff"], status: "tested",
      after: "Shim 4024201f, PHY 00004800 (v72), DTU 0xf1109000 / 0xf1110000.", doc: "hardware/peripherals.md" },

    // ---- System control ----
    { id: "gisbarb", g: "sys", label: "GISB arbiter", sub: "0xf0400000", range: ["0xf0400000", "0xf04007ff"], status: "tested",
      after: "Records the last failed access.",
      regs: [["0xf04007ec", "error address", "last aborted address"], ["0xf04007f8", "error master", "40 = bit 6 = cpu_0"], ["0xf04007f4", "error status", "0000083d after a failed read"], ["0xf0400008", "timeout?", "000278d0"]],
      note: "Masters (DTB): bsp 0, scpu 1, cpu_0 6 (tested), webcpu 7, jtag 8, ssp 9, rdc_0 10, hvd_0 11, raaga 17, pcie 19, bbsi_spi 23, avs 24. No V3D master listed.",
      doc: "hardware/peripherals.md" },
    { id: "suntop", g: "sys", label: "SUN_TOP_CTRL", sub: "0xf0404000", range: ["0xf0404000", "0xf04047ff"], status: "tested",
      after: "Chip ID 72680010, straps 00000f1e (boot from eMMC).",
      regs: [["0xf0404000", "chip family ID", "72680010 = BCM7268 B0"], ["0xf040401c", "straps", "00000f1e"], ["0xf0404030/34, 520", "OTP", "read only, never write"], ["0xf0404304/308", "software reset", "1 then 1 = reboot"], ["0xf0404318/31c", "block reset set / clear", "write-only, one bit per block"], ["0xf0404100+", "pin mux", ""]],
      doc: "hardware/system-blocks.md" },
    { id: "sysl2", g: "sys", label: "sys L2", sub: "0xf0403000", range: ["0xf0403000", "0xf0403047"], status: "tested",
      after: "All masked. Bit 2 (gisb_tea) set by aborted reads.", doc: "hardware/interrupts.md" },
    { id: "clkgen", g: "sys", label: "Clock generator", sub: "0xf04e0000", range: ["0xf04e0000", "0xf04e7fff"], status: "tested",
      after: "PLLs, dividers, gates, muxes. 418 words answer, 7,774 abort.",
      regs: [["0xf04e0488", "UART gates", "bits 4/5/6, 1 = off; reads 0"], ["0xf04e04e8", "M2MC0 clocks", "7 = on"], ["0xf04e04c8", "V3D clocks", "1f = on"], ["0xf04e0458", "PCIe clocks", "f = on"], ["0xf04e03e0", "GENET", "000fffff"], ["0xf04e6134", "bond option", "low byte 03"]],
      doc: "hardware/peripherals.md" },
    { id: "tmon", g: "sys", label: "Temperature (TMON)", sub: "0xf04d1500", range: ["0xf04d1500", "0xf04d15ff"], more: [["0xf04c4000", "0xf04c4fff", "AVS CPU data memory"]], status: "tested",
      after: "Running. °C = (410040 − code × 487) / 1000.", regs: [["0xf04d1500", "STATUS", "bit 11 valid, bits 10:1 code"]],
      doc: "hardware/system-blocks.md" },
    { id: "avsl2", g: "sys", label: "AVS L2", sub: "0xf04d1200", range: ["0xf04d1200", "0xf04d1247"], more: [["0xf04d1100", "0xf04d11ff", "AVS L2 interrupt block"]], status: "tested",
      after: "STATUS 04000000 (sw_intr), all masked.", doc: "hardware/interrupts.md" },

    // ---- UPG ----
    { id: "uart0", g: "upg", label: "UART0 · console", sub: "0xf040c000", range: ["0xf040c000", "0xf040c01f"], status: "tested",
      after: "115200 8N1, polled, 81 MHz clock, divisor 44.",
      regs: [["+0x00", "RBR / THR", ""], ["+0x0c", "LCR", "03 = 8N1"], ["+0x14", "LSR", "bit 5 = room to send"]],
      doc: "hardware/uart.md" },
    { id: "uart1", g: "upg", label: "UART1", sub: "0xf040d000", range: ["0xf040d000", "0xf040d01f"], status: "tested",
      after: "Reset state, clocked. Loopback works. Pins unknown.", doc: "hardware/uart.md" },
    { id: "uart2", g: "upg", label: "UART2", sub: "0xf040e000", range: ["0xf040e000", "0xf040e01f"], status: "tested",
      after: "Reset state, clocked. Loopback works. Pins unknown.", doc: "hardware/uart.md" },
    { id: "wdt", g: "upg", label: "Watchdog", sub: "0xf040a6a8 · 27 MHz", range: ["0xf040a6a8", "0xf040a6af"], more: [["0xf040a680", "0xf040a6a7", "UPG timers (BOLT /bolt timer-wdog = 0xf040a680)"]], status: "tested",
      after: "Off. Start: TIMEOUT, then ff00, 00ff to +0x4.", doc: "hardware/system-blocks.md" },
    { id: "gpio", g: "upg", label: "Main GPIO", sub: "0xf040a500 · 4 banks", range: ["0xf040a500", "0xf040a57f"], status: "tested",
      after: "Every pin an input.", doc: "hardware/gpio.md" },
    { id: "bsc0", g: "upg", label: "I2C ch0 · HDMI DDC", sub: "0xf040a300", range: ["0xf040a300", "0xf040a357"], status: "tested",
      after: "Answers at 0x30, 0x3a, 0x50 (EDID, HDCP).", doc: "hardware/i2c.md" },
    { id: "bsc4", g: "upg", label: "I2C ch4", sub: "0xf040a400", range: ["0xf040a400", "0xf040a457"], status: "tested",
      after: "Every address times out: not wired or not muxed.", doc: "hardware/i2c.md" },
    { id: "upgl2", g: "upg", label: "UPG L2", sub: "0xf040a600", range: ["0xf040a600", "0xf040a61f"], status: "tested",
      after: "Mask 7 (all masked).", doc: "hardware/interrupts.md" },
    { id: "bscl2", g: "upg", label: "UPG BSC L2", sub: "0xf040a640", range: ["0xf040a640", "0xf040a65f"], status: "tested",
      after: "Mask 7 (all masked).", doc: "hardware/interrupts.md" },

    // ---- AON ----
    { id: "aonctrl", g: "aon", label: "AON control + SRAM", sub: "0xf0410000", range: ["0xf0410000", "0xf04105ff"], more: [["0xf0410700", "0xf041071f", "AON pin mux 0x700, pad control 0x714"]], status: "tested",
      after: "Reset history cleared by BOLT. +0x2c aborts.",
      regs: [["0xf041006c", "reset history", "RR bits; BOLT keeps a copy at 0x07069844"], ["0xf0410200–5ff", "AON SRAM", "1 KB, survives resets"], ["0xf041002c", "", "external abort: do not read"]],
      doc: "hardware/system-blocks.md" },
    { id: "syspm", g: "aon", label: "AON L2 sys_pm", sub: "0xf0410640", range: ["0xf0410640", "0xf041066f"], status: "tested",
      after: "Mask 007fffff. Masked sources don't show in STATUS.", doc: "hardware/interrupts.md" },
    { id: "aongpio", g: "aon", label: "AON GPIO", sub: "0xf0419c80", range: ["0xf0419c80", "0xf0419cbf"], status: "tested",
      after: "Outputs: LED1 green 18, red 17. Inputs: SW1 14, SW4 7, LED3 16 (input until set up).",
      regs: [["0xf0419c84", "DATA", "LEDs active-low"], ["0xf0419c88", "IODIR", "1 = input"], ["0xf0419c9c", "STAT", "write-1-to-clear"]],
      doc: "hardware/gpio.md" },
    { id: "aonl2", g: "aon", label: "AON L2 (UPG)", sub: "0xf0419c00", range: ["0xf0419c00", "0xf0419c1f"], status: "tested",
      after: "Mask 7f. STATUS shows sources even while masked.", doc: "hardware/interrupts.md" },
    { id: "ir", g: "aon", label: "IR receiver kbd1–3", sub: "0xf0419900", range: ["0xf0419900", "0xf0419a3f"], more: [["0xf0419880", "0xf0419883", "PM AON config word"]], status: "tested",
      after: "Off. kbd1 decodes the Kaon remote once set up as NEC.",
      regs: [["0xf0419900", "STATUS", "bit 0 = code ready"], ["0xf0419910", "DATA0", "32-bit code"], ["0xf0419914", "CMD", "bit 4 CIR on, bit 5 IRQ enable"]],
      doc: "hardware/ir.md" },
    { id: "bscaonl2", g: "aon", label: "AON BSC L2", sub: "0xf0419c40", range: ["0xf0419c40", "0xf0419c5f"], status: "tested",
      after: "Mask f (all masked).", doc: "hardware/interrupts.md" },
    { id: "bsc123", g: "aon", label: "I2C ch1–3", sub: "0xf0419a80…b80", range: ["0xf0419a80", "0xf0419bd7"], status: "tested",
      after: "ch3: device at 0x67 (Si2168C per Nexus). ch1 times out, ch2 no ACK.", doc: "hardware/i2c.md" },
    { id: "waketimer", g: "aon", label: "Wake timer", sub: "0xf041a080 · seconds", range: ["0xf041a080", "0xf041a093"], status: "tested",
      after: "Counting seconds since power-on.", regs: [["0xf041a084", "COUNTER", "seconds"], ["0xf041a088", "ALARM", ""]],
      doc: "hardware/system-blocks.md" },
    { id: "v3dpwr", g: "aon", span: 2, label: "V3D power island", sub: "0xf041d020", range: ["0xf041d020", "0xf041d023"], status: "tested",
      after: "Off (424e0908). 1d00 = up, b00 = down.", doc: "hardware/gpu.md" },

    // ---- I/O ----
    { id: "sdhci0", g: "io", label: "SDHCI 0 · SD", sub: "0xf0200000", range: ["0xf0200000", "0xf02001ff"], status: "tested",
      after: "Removable-slot type, no card, power off. No slot on the case.", doc: "hardware/storage.md" },
    { id: "sdhci1", g: "io", label: "SDHCI 1 · eMMC", sub: "0xf0200200", range: ["0xf0200200", "0xf02003ff"], more: [["0xf0200400", "0xf020043f", "unnamed block after the SDHCI cfg blocks (answers; +0x0c aborts)"]], status: "tested",
      after: "8-bit, 50 MHz, SDMA. Holds BOLT's last CMD18 read.", doc: "hardware/storage.md" },
    { id: "genet", g: "io", label: "GENET 0 Ethernet", sub: "0xf0480000 · v5", range: ["0xf0480000", "0xf048ffff"], more: [["0xf04a0000", "0xf04affff", "GENET 1 (clocked, unused)"]], status: "tested",
      after: "Idle: TX/RX off, no MAC address. Internal PHY at MDIO 1, link 100 full.",
      regs: [["+0x808", "UMAC_CMD", "010000d8: TX/RX off"], ["+0xe14", "MDIO command", ""]],
      doc: "hardware/ethernet.md" },
    { id: "usb", g: "io", label: "USB", sub: "0xf0b00200 · EHCI/OHCI/xHCI", range: ["0xf0b00200", "0xf0b02fff"], more: [["0xf0b00000", "0xf0b001ff", "start of the USB block"]], status: "tested",
      after: "Controllers reset by go, PHY stays up. BDC held in reset (0xf0b00234 bit 23).",
      regs: [["0xf0b00300 / 500", "EHCI0 / EHCI1", "BT / USB-A"], ["0xf0b00400 / 600", "OHCI0 / OHCI1", "BT / USB-A"], ["0xf0b01000", "xHCI", "USB 3 side only"], ["0xf0b02000", "BDC", "device mode, in reset"]],
      doc: "hardware/usb.md" },
    { id: "pcie", g: "io", label: "PCIe · Wi-Fi", sub: "0xf0460000", range: ["0xf0460000", "0xf046ffff"], status: "tested",
      after: "Held in reset by 0xf0469210 bit 1: every read aborts. Released, it reads 726814e4.", doc: "hardware/peripherals.md" },

    // ---- Display ----
    { id: "rdc", g: "disp", label: "RDC", sub: "0xf0603000", range: ["0xf0603000", "0xf0605fff"], status: "tested",
      after: "Runs BOLT's 73-word list every frame.",
      regs: [["0xf0603484", "frame counter", "+1 per frame, 59.94 Hz"], ["0xf0603488 / 48c", "surface address", "write both to flip"], ["0xf0604000", "descriptor 0 list address", "reads back the previous value"]],
      doc: "hardware/display.md" },
    { id: "gfd", g: "disp", label: "GFD0", sub: "0xf0641000", range: ["0xf0641000", "0xf0641fff"], status: "tested",
      after: "Feeds the 1920 × 1080 RGB565 framebuffer.",
      regs: [["0xf0641044", "source width", "780"], ["0xf0641048", "surface address", "7db0b700"], ["0xf064105c", "", "external abort: do not read"]],
      doc: "hardware/display.md" },
    { id: "cmp", g: "disp", label: "CMP0", sub: "0xf0645800", range: ["0xf0645800", "0xf0645fff"], more: [["0xf0645000", "0xf06457ff", "display pipeline, name unknown (written by BOLT's splash script)"], ["0xf0650000", "0xf06500ff", "compositor register written by the lists (0xf065001c)"]], status: "tested",
      after: "Canvas 1920 × 1080, blue background.",
      regs: [["0xf0645810", "background", "00 Y Cb Cr"], ["0xf0645988", "window size", "w << 16 | h"], ["0xf064598c", "window position", "x << 16 | y"]],
      doc: "hardware/display.md" },
    { id: "vec", g: "disp", label: "VEC", sub: "0xf06e0000", range: ["0xf06e0000", "0xf06e7fff"], status: "nexus",
      after: "1080p at 59.94 Hz. Registers readable; roles from Nexus names.", doc: "hardware/display.md" },
    { id: "hdmi", g: "disp", label: "HDMI TX", sub: "0xf06fa000", range: ["0xf06fa000", "0xf06fafff"], status: "tested",
      after: "Video on. Audio N/CTS 0 until set by hand.",
      regs: [["0xf06fa0c8", "N", "040016c0 for 59.94"], ["0xf06fa0cc / 0d0", "CTS", "00022551"], ["0xf06fa828, 884/888/898", "", "do not write: breaks the picture"]],
      doc: "hardware/display.md" },

    // ---- Graphics ----
    { id: "m2mc", g: "gfx", label: "M2MC 2D blitter", sub: "0xf09b0000", range: ["0xf09b0000", "0xf09b1fff"], status: "tested",
      after: "Powered and clocked. Works from a packet list in RAM.",
      regs: [["+0x0c", "list control", "6 start, 3 continue"], ["+0x14", "first packet", ""], ["+0x18", "current packet", ""]],
      doc: "hardware/2d-blitter.md" },
    { id: "v3d", g: "gfx", label: "V3D 3.3 GPU", sub: "0xf1200000 · 8 QPUs", range: ["0xf1200000", "0xf120bfff"], status: "tested",
      after: "Power island off: every access aborts until powered.",
      regs: [["0xf1200000", "hub", "AXICFG, IDENT, interrupts"], ["0xf1204000", "bridge", "SW_INIT reset"], ["0xf1208000", "core", "CLE, PTB, caches"]],
      doc: "hardware/gpu.md" },

    // ---- Audio ----
    { id: "fmm", g: "aud", label: "Audio FMM", sub: "0xf0ca0000 · ring buffers", range: ["0xf0ca0000", "0xf0caffff"], status: "tested",
      after: "Idle unless the splash has a pcm0. Then it loops forever.",
      regs: [["0xf0ca00c0", "DMA", "1 run, 0 stop"], ["0xf0ca0800", "ring 0 read pointer", "bit 31 flips each lap"], ["0xf0ca0808 / 80c", "ring 0 start / end", ""]],
      doc: "hardware/audio.md" },
    { id: "aout", g: "aud", label: "Audio output ports", sub: "0xf0cb0000", range: ["0xf0cb0000", "0xf0cbffff"], status: "tested",
      after: "0xf0cb0300 is the HDMI port (bit 31 = on). S/PDIF port unknown.", doc: "hardware/audio.md" },

    // ---- Transport / security ----
    { id: "xptctl", g: "xpt", label: "XPT control", sub: "0xf0a00000", range: ["0xf0a00000", "0xf0a0ffff"], status: "bolt",
      after: "BOLT uses it before its SHA DMA.", doc: "hardware/peripherals.md" },
    { id: "xptdma", g: "xpt", label: "XPT DMA", sub: "0xf0a68000", range: ["0xf0a68000", "0xf0a70fff"], status: "bolt",
      after: "BOLT's memory-to-memory DMA for hashing.", doc: "hardware/peripherals.md" },
    { id: "xptsec", g: "xpt", label: "XPT security / SHA", sub: "0xf0380000", range: ["0xf0380000", "0xf038ffff"], status: "bolt",
      after: "Hashes images for BOLT.", doc: "hardware/peripherals.md" },
    { id: "bsp", g: "xpt", label: "BSP security CPU", sub: "0xf032c800", range: ["0xf032c800", "0xf032c8ff"], status: "dtb",
      after: "First word 0. Security firmware v4.2.5.", doc: "hardware/peripherals.md" },

    // ---- Named, not mapped ----
    { id: "hvd", g: "unk", label: "Video decoder HVD", sub: "address unknown", status: "unknown",
      after: "Named by the memory controller; interrupt 85 fires under Android.", doc: "hardware/peripherals.md" },
    { id: "raaga", g: "unk", label: "Audio DSP RAAGA", sub: "address unknown", status: "unknown",
      after: "Interrupts 86 / 87 under Android.", doc: "hardware/peripherals.md" },
    { id: "sata", g: "unk", label: "SATA AHCI", sub: "0xf0b12000", range: ["0xf0b12000", "0xf0b12fff"], more: [["0xf0b10100", "0xf0b101ff", "SATA PHY (original DTB only)"]], status: "tested",
      after: "Answers (CAP e9327f00, 1 port). Strap says disabled; no connector seen.", doc: "hardware/peripherals.md" },
    { id: "nand", g: "unk", label: "NAND controller", sub: "0xf0203000 · v7.2", range: ["0xf0203000", "0xf02039ff"], status: "tested",
      after: "Answers, but the box has no NAND.", doc: "hardware/peripherals.md" },
    { id: "spi", g: "unk", label: "MSPI / QSPI", sub: "0xf0418000 · 0xf0203a00", range: ["0xf0418000", "0xf04180ff"], more: [["0xf0203a00", "0xf0203dff", "BSPI / QSPI (disabled)"]], status: "dtb",
      after: "Disabled in the DTB.", doc: "hardware/peripherals.md" },
    { id: "spil2", g: "unk", label: "AON SPI L2", sub: "0xf0419000", range: ["0xf0419000", "0xf041901f"], status: "tested",
      after: "Mask 3 (all masked). Sources: MSPI done, spare.", doc: "hardware/interrupts.md" },
    { id: "pwm", g: "unk", label: "PWM 0 / 1", sub: "0xf0408000 / 9000", range: ["0xf0408000", "0xf0409fff"], status: "tested",
      after: "First word 00000022. Not used.", doc: "hardware/peripherals.md" },

    // ---- Parts on the PCB (board level). Fixed positions; "where" says how the position is known. ----
    { id: "emmc", g: "board", kind: "chip", x: 1790, y: 120, w: 250, h: 110, label: "eMMC U3", sub: "SanDisk SDINBDG4-8G", status: "tested",
      after: "Linux names the card DG4008, 7.28 GiB. User area 7458 MB (flash0). Boot partitions 4 MB each (BOLT lives there), RPMB 4 MB with the key never programmed.",
      where: "Front, right next to the SoC (U3, photo).", doc: "hardware/storage.md" },
    { id: "wifi", g: "board", kind: "chip", x: -330, y: 560, w: 250, h: 300, label: "Wi-Fi BCM43570", sub: "BCM43570KFEB1G · PCIe 14e4:aa31", status: "tested",
      after: "Seen by the stock kernel (DHD driver, PCIe link 2.5 Gbps ×1). After BOLT the PCIe controller is held in reset and the power pins are inputs.",
      where: "Front left, under the shield; antenna connectors J30, J31, J12 (photo).", doc: "stock-firmware.md" },
    { id: "bt", g: "board", kind: "chip", x: -330, y: 370, w: 250, h: 90, label: "Bluetooth adapter", sub: "USB 0a5c:2045", status: "tested",
      after: "Enumerated by BOLT and Linux on EHCI0 / OHCI0, full speed.", where: "Position on the board not identified.", doc: "hardware/usb.md" },
    { id: "usba", g: "board", kind: "conn", x: -390, y: 120, w: 260, h: 100, label: "USB-A port (J2)", sub: "USB 2.0 host", status: "tested",
      after: "Low/full speed via OHCI1, high speed via EHCI1. A keyboard was brought up from bare metal on OHCI1.", where: "Left edge (photo).", doc: "hardware/usb.md" },
    { id: "rj45", g: "board", kind: "conn", x: 640, y: -330, w: 210, h: 110, label: "Ethernet RJ45 J1", sub: "10/100", status: "tested",
      after: "Link 100 Mbit/s full duplex through the SoC's internal PHY.", where: "Rear edge, J1 (photo).", doc: "hardware/ethernet.md" },
    { id: "hdmiconn", g: "board", kind: "conn", x: 300, y: -330, w: 230, h: 100, label: "HDMI out J4", sub: "1080p59.94 from BOLT", status: "tested",
      after: "Video from BOLT's splash; hot-plug arrives on interrupt 59.", where: "Rear edge, J4 (photo).", doc: "hardware/display.md" },
    { id: "spdif", g: "board", kind: "conn", x: 30, y: -330, w: 180, h: 100, label: "Optical S/PDIF J5", sub: "TOSLINK", status: "tested",
      after: "Lights red while BOLT's splash audio runs (by eye). The signal itself is not checked.", where: "Rear edge, J5, between the power jack and HDMI (photo).", doc: "hardware/audio.md" },
    { id: "power", g: "board", kind: "conn", x: -160, y: -330, w: 170, h: 100, label: "Power jack J7", sub: "DC in", status: "seen",
      after: "", where: "Rear edge, J7 (photo).", doc: "../README.md" },
    { id: "sw2", g: "board", kind: "button", x: -345, y: -330, w: 165, h: 100, label: "SW2 power switch", sub: "cuts power", status: "seen",
      after: "Probably the hard power switch: not software-visible (inferred).", where: "Rear edge, far left (silkscreen SW2 on the back of the PCB).", doc: "hardware/gpio.md" },
    { id: "tuner", g: "board", kind: "conn", x: 960, y: -330, w: 270, h: 150, label: "DVB-T tuner + RF in", sub: "shielded can", status: "seen",
      after: "Not tested. Driven by Nexus under Android.", where: "Rear right: the shielded can with the F connector (photo).", doc: "../README.md" },
    { id: "demod", g: "board", kind: "chip", x: 990, y: -150, w: 230, h: 80, label: "Demodulator Si2168", sub: "I2C ch3 · 0x67", status: "tested",
      after: "Marking Si2168 60 / 1931D1N002 (photo). Answers at 0x67 on BSC ch3; Nexus opens it as a Si2168C.", where: "Front top right, below the tuner can (photo).", doc: "hardware/i2c.md" },
    { id: "ir1", g: "board", kind: "chip", x: 120, y: 1250, w: 210, h: 80, label: "IR1 receiver", sub: "→ kbd1 · NEC", status: "tested",
      after: "Feeds the SoC's IR receiver block directly (no GPIO). The Kaon remote's 41 codes are decoded.", where: "Front left, IR1 (photo).", doc: "hardware/ir.md" },
    { id: "led1", g: "board", kind: "led", led: ["#3ccf6a", "#ff5a4a"], x: 700, y: 1262, w: 190, h: 76, label: "LED1 power", sub: "green 18 · red 17", status: "tested",
      after: "Active-low. Both on = orange.", where: "Front (photo).", doc: "hardware/gpio.md" },
    { id: "led3", g: "board", kind: "led", led: ["#4a8dff"], x: 910, y: 1262, w: 170, h: 76, label: "LED3 blue", sub: "AON GPIO 16", status: "tested",
      after: "Active-low. Input after BOLT: set ODEN, DATA, then IODIR.", where: "Front (photo).", doc: "hardware/gpio.md" },
    { id: "sw1", g: "board", kind: "button", x: 1100, y: 1262, w: 170, h: 76, label: "SW1 standby", sub: "AON GPIO 14", status: "tested",
      after: "Active-low. Interrupt chain tested up to the AON L2 (bit 3, GIC ID 98).", where: "Front right (photo).", doc: "hardware/gpio.md" },
    { id: "uarthdr", g: "board", kind: "conn", x: 1430, y: 1250, w: 230, h: 80, label: "Console CN2", sub: "UART0 · 115200 8N1", status: "tested",
      after: "The serial console: BOLT and everything else print here.", where: "Front right, next to SW1; the wires are soldered to CN2 under the glue (photo).", doc: "booting.md" },
    { id: "cn1", g: "board", kind: "conn", x: 1770, y: 1110, w: 200, h: 110, label: "CN1 header", sub: "4 pins · not fitted", status: "seen",
      after: "Function unknown. Pin 1 is the square pad at the top.", where: "Front right edge, just above the console header CN2 (photo).", doc: "booting.md" },
    { id: "sw4", g: "board", kind: "button", x: 1840, y: 520, w: 230, h: 86, label: "SW4 recovery / BT pair", sub: "AON GPIO 7", status: "tested",
      after: "Active-low. Held at power-on: recovery. Linux polls it (no interrupt).", where: "Right edge (photo).", doc: "hardware/gpio.md" },

    // ---- DRAM regions (off-chip) ----
    { id: "r_low",   g: "dram", label: "Reserved, no DMA", sub: "0x00000000 · 4 KB", range: ["0x00000000", "0x00000fff"], status: "dtb",
      after: "Leave alone (DTB reserved-nodma). Unmapped under BOLT (NULL guard).", doc: "hardware/memory-map.md" },
    { id: "r_free1", g: "dram", label: "Free", sub: "0x00001000 · 16 MB", range: ["0x00001000", "0x00ffffff"], status: "tested",
      after: "Free after go -64. BOLT's flash staging buffer 0x00040000 while flash runs.", doc: "hardware/memory-map.md" },
    { id: "r_prog",  g: "dram", label: "Your program", sub: "0x01000000 · 2 MB", range: ["0x01000000", "0x011fffff"], status: "tested",
      after: "kstb-run loads here; go -64 enters here.", doc: "booting.md" },
    { id: "r_free2", g: "dram", label: "Free · scratch", sub: "0x01200000 · 82 MB", range: ["0x01200000", "0x063fffff"], status: "tested",
      after: "Used by the M2MC and V3D tests (surfaces 0x02…, tile memory 0x04000000).", doc: "hardware/memory-map.md" },
    { id: "r_psci",  g: "dram", label: "PSCI monitor smm64", sub: "0x06400000 · 64 KB", range: ["0x06400000", "0x0640ffff"], status: "tested",
      after: "EL3 code. Keep it: smc #0 calls it (CPU_ON).", doc: "hardware/cpu-cores.md" },
    { id: "r_free3", g: "dram", label: "Free", sub: "0x06410000 · 11 MB", range: ["0x06410000", "0x06efffff"], status: "tested",
      after: "Free.", doc: "hardware/memory-map.md" },
    { id: "r_bolt",  g: "dram", label: "BOLT", sub: "0x06f00000 · 35 MB", range: ["0x06f00000", "0x091fffff"], status: "tested",
      after: "Page table 0x07000000, code, heap, stack, DTB 0x07613000. Free after go -64.", doc: "bolt/bolt.md" },
    { id: "r_free4", g: "dram", label: "Free · bulk", sub: "0x09200000 · 1.8 GB", range: ["0x09200000", "0x7db07fff"], status: "tested",
      after: "Free, pattern-tested. Data file loaded at 0x10000000; MMU tables 0x10600000; OHCI HCCA test 0x11000000.", doc: "hardware/memory-map.md" },
    { id: "r_fb2",   g: "dram", label: "Second framebuffer", sub: "0x7d600000 · 4 MB", range: ["0x7d600000", "0x7d9f47ff"], status: "tested",
      after: "Back buffer for double buffering (free rmem).", doc: "hardware/display.md" },
    { id: "r_audio", g: "dram", label: "Audio ring buffer", sub: "0x7dada100 · 192,000 B", range: ["0x7dada100", "0x7db08eff"], status: "tested",
      after: "Only when the splash has a pcm0. Free otherwise.", doc: "hardware/audio.md" },
    { id: "r_lists", g: "dram", label: "Display lists", sub: "0x7db08000 · 14 KB", range: ["0x7db08000", "0x7db0b6ff"], status: "tested",
      after: "Never write: blanks the screen until reboot. First entry at 0x7db08fa0.", doc: "hardware/display.md" },
    { id: "r_fb",    g: "dram", label: "Framebuffer", sub: "0x7db0b700 · RGB565", range: ["0x7db0b700", "0x7defffff"], status: "tested",
      after: "1920 × 1080, pitch 3840. pixel(x, y) = 0x7db0b700 + y × 3840 + x × 2.", doc: "hardware/display.md", fb: true },
    { id: "r_bl31",  g: "dram", label: "BL31 (secure)", sub: "0x7df00000 · 1 MB", range: ["0x7df00000", "0x7dffffff"], status: "dtb",
      after: "ARM Trusted Firmware. Never read or write.", doc: "hardware/memory-map.md" },
    { id: "r_srr",   g: "dram", label: "SRR (secure)", sub: "0x7e000000 · 32 MB", range: ["0x7e000000", "0x7fffffff"], status: "dtb",
      after: "Secure reserved region. Never read or write.", doc: "hardware/memory-map.md" }
  ],

  // [from, to, layer, label, status, badge]  (badge: the GIC ID shown on the block)
  edges: [
    // register bus masters
    ["cores", "gisbarb", "reg", "CPU loads / stores · master cpu_0 (bit 6)", "tested"],
    ["rdc", "gisbarb", "reg", "bus master rdc_0 (bit 10)", "dtb"],
    ["rdc", "gfd", "reg", "list writes every frame", "tested"],
    ["rdc", "cmp", "reg", "list writes every frame", "tested"],
    ["rdc", "vec", "reg", "list writes (timing)", "tested"],
    ["rdc", "hdmi", "reg", "list writes (rate manager)", "tested"],
    ["cores", "gic", "reg", "private bus 0xffd…, not GISB", "tested"],

    // interrupts straight to the GIC (IDs; tested = listed by the stock kernel)
    ["timer", "gic", "irq", "PPI 26 27 29 30", "tested", "26–30"],
    ["bsp", "gic", "irq", "32 BSP · 33 SCPU", "tested", "32 · 33"],
    ["fmm", "gic", "irq", "45 AIO", "tested", "45"],
    ["m2mc", "gic", "irq", "46 GFX", "tested", "46"],
    ["vec", "gic", "irq", "47 VEC · 50/s at 1080p50", "tested", "47"],
    ["clkgen", "gic", "irq", "56 CLKGEN", "tested", "56"],
    ["avsl2", "gic", "irq", "57", "tested", "57"],
    ["hdmi", "gic", "irq", "59 HDMI_TX · hot-plug", "tested", "59"],
    ["hifl2", "gic", "irq", "63", "tested", "63"],
    ["hifspil2", "gic", "irq", "64", "tested", "64"],
    ["sdhci0", "gic", "irq", "69 mmc0", "tested", "69"],
    ["sdhci1", "gic", "irq", "70 mmc1", "tested", "70"],
    ["pcie", "gic", "irq", "78 Wi-Fi INTA · 83 MSI", "tested", "78 · 83"],
    ["hvd", "gic", "irq", "85 HVD0_0", "tested", "85"],
    ["raaga", "gic", "irq", "86 / 87", "tested", "86 · 87"],
    ["memc", "gic", "irq", "88 MEMC0", "tested", "88"],
    ["sata", "gic", "irq", "89", "dtb", "89"],
    ["sysl2", "gic", "irq", "91", "tested", "91"],
    ["syspm", "gic", "irq", "93", "tested", "93"],
    ["bscl2", "gic", "irq", "95", "tested", "95"],
    ["bscaonl2", "gic", "irq", "96", "tested", "96"],
    ["upgl2", "gic", "irq", "97", "tested", "97"],
    ["aonl2", "gic", "irq", "98 · SW1, IR", "tested", "98"],
    ["spil2", "gic", "irq", "100", "tested", "100"],
    ["uart0", "gic", "irq", "102", "tested", "102"],
    ["uart1", "gic", "irq", "103", "tested", "103"],
    ["uart2", "gic", "irq", "104", "tested", "104"],
    ["v3d", "gic", "irq", "109 V3D · 110 hub", "tested", "109 · 110"],
    ["xptctl", "gic", "irq", "111–121 XPT", "tested", "111–121"],
    ["usb", "gic", "irq", "122 123 124 126 127", "tested", "122–127"],
    ["genet", "gic", "irq", "129 / 130", "tested", "129 · 130"],

    // interrupts through L2 controllers
    ["gisbarb", "sysl2", "irq", "bit 0 timeout · bit 2 target error", "tested"],
    ["aongpio", "aonl2", "irq", "bit 3 gio (SW1 chain)", "tested"],
    ["ir", "aonl2", "irq", "bit 0 kbd1 · 1 kbd2 · 2 kbd3", "tested"],
    ["bsc123", "bscaonl2", "irq", "bits 0–2 · iicd busy", "tested"],
    ["bsc0", "bscl2", "irq", "bit 0 iica", "nexus"],
    ["bsc4", "bscl2", "irq", "bit 1 iice", "nexus"],
    ["gpio", "upgl2", "irq", "bit 0 gio", "dtb"],
    ["waketimer", "syspm", "irq", "bit 4", "dtb"],
    ["aongpio", "syspm", "irq", "bit 6 GPIO wake-up", "dtb"],
    ["genet", "syspm", "irq", "bit 18 Wake-on-LAN", "dtb"],
    ["tmon", "avsl2", "irq", "bit 6 tmon", "dtb"],

    // DRAM data
    ["cores", "memc", "data", "loads / stores", "tested"],
    ["memc", "dram", "data", "LPDDR4 ×32 · 1856 MHz", "tested"],
    ["cores", "r_psci", "data", "smc #0 → EL3 monitor", "tested"],
    ["rdc", "r_lists", "data", "fetches lists every frame", "tested"],
    ["gfd", "r_fb", "data", "scans out the framebuffer", "tested"],
    ["gfd", "r_fb2", "data", "after a flip", "tested"],
    ["fmm", "r_audio", "data", "loops over the ring", "tested"],
    ["m2mc", "r_free2", "data", "packets, source surfaces", "tested"],
    ["m2mc", "r_fb", "data", "blits into the framebuffer", "tested"],
    ["v3d", "r_free2", "data", "control lists, tile memory", "tested"],
    ["v3d", "r_fb", "data", "stores tiles as RGB565", "tested"],
    ["sdhci1", "r_bolt", "data", "SDMA for BOLT's reads", "tested"],
    ["usb", "r_free4", "data", "OHCI HCCA (test at 0x11000000)", "tested"],
    ["xptdma", "r_bolt", "data", "BOLT's SHA DMA", "bolt"],

    // on-chip streams
    ["gfd", "cmp", "stream", "graphics surface", "inferred"],
    ["cmp", "vec", "stream", "composited frame", "inferred"],
    ["vec", "hdmi", "stream", "1080p59.94 timing", "inferred"],
    ["fmm", "aout", "stream", "48 kHz samples", "tested"],
    ["aout", "hdmi", "stream", "HDMI audio port 0xf0cb0300", "tested"],

    // clocks, power, resets
    ["clkgen", "uart0", "pwr", "gate 0xf04e0488 bit 4", "tested"],
    ["clkgen", "uart1", "pwr", "gate bit 5", "tested"],
    ["clkgen", "uart2", "pwr", "gate bit 6", "tested"],
    ["clkgen", "m2mc", "pwr", "0xf04e04e8 = 7", "tested"],
    ["clkgen", "v3d", "pwr", "PLL + 0xf04e04c8 = 1f", "tested"],
    ["clkgen", "usb", "pwr", "0xf04e049c–04bc", "tested"],
    ["clkgen", "genet", "pwr", "0xf04e03e0", "tested"],
    ["clkgen", "pcie", "pwr", "0xf04e0450 / 0458", "tested"],
    ["v3dpwr", "v3d", "pwr", "power island 1d00 up / b00 down", "tested"],
    ["suntop", "genet", "pwr", "reset bits 26, 27 (0xf0404318 / 31c)", "bolt"],
    ["suntop", "cores", "pwr", "software master reset 0xf0404308", "tested"],
    ["wdt", "aonctrl", "pwr", "reset → history bit 6 (RR:00000040)", "tested"],
    // board wiring
    ["sdhci1", "emmc", "ext", "8-bit · 50 MHz · SDMA", "tested"],
    ["pcie", "wifi", "ext", "PCIe ×1 · 2.5 Gbps", "tested"],
    ["aongpio", "wifi", "ext", "power: AON GPIO 21, 26", "dtb"],
    ["usb", "bt", "ext", "EHCI0 + OHCI0 · full speed", "tested"],
    ["usb", "usba", "ext", "EHCI1 + OHCI1", "tested"],
    ["genet", "rj45", "ext", "internal PHY · MDIO 1 · 100 full", "tested"],
    ["hdmi", "hdmiconn", "ext", "TMDS · 1080p59.94", "tested"],
    ["bsc0", "hdmiconn", "ext", "DDC: EDID 0x50 · HDCP 0x3a", "tested"],
    ["aout", "spdif", "ext", "lights with audio · port unknown", "inferred"],
    ["uart0", "uarthdr", "ext", "TX / RX · 115200 8N1", "tested"],
    ["aongpio", "led1", "ext", "GPIO 18 green · 17 red", "tested"],
    ["aongpio", "led3", "ext", "GPIO 16 blue", "tested"],
    ["sw1", "aongpio", "ext", "GPIO 14 · active-low", "tested"],
    ["sw4", "aongpio", "ext", "GPIO 7 · active-low", "tested"],
    ["ir1", "ir", "ext", "IR signal → kbd1", "tested"],
    ["bsc123", "demod", "ext", "I2C ch3 · address 0x67", "tested"],
    ["tuner", "demod", "ext", "RF front end", "inferred"]
  ]
};
