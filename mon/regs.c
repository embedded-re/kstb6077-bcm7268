// regs.c - names for known addresses, danger zones and remote key names.
// Everything here is from docs/hardware/registers.md (tested on the board
// unless the sheet says otherwise).

#include <stddef.h>
#include <stdint.h>
#include "mon.h"

const struct reg regs[] = {
    // RAM areas (docs/hardware/memory-map.md)
    { "prog",        0x01000000, "where programs run (go/load default; doom.bin)" },
    { "wad",         0x10000000, "WAD's KWAD image, loaded by BOLT" },
    { "fb",          0x7db0b700, "framebuffer: RGB565, pitch 3840, 1920x1080" },
    { "fb2",         0x7d600000, "second framebuffer (double buffering)" },
    { "audioring",   0x7dada100, "audio ring buffer (boot with pcm0)" },
    { "boltrr",      0x07069844, "BOLT's copy of the reset history (RAM, until overwritten)" },
    { "psci",        0x06400000, "PSCI monitor smm64 (EL3): don't overwrite" },
    // UART0 (docs/hardware/uart.md)
    { "uart0",       0xf040c000, "UART0 RBR/THR (console)" },
    { "uart0_lsr",   0xf040c014, "UART0 LSR: bit0 rx ready, bit5 tx room" },
    { "uart1",       0xf040d000, "UART1 (pins unknown)" },
    { "uart2",       0xf040e000, "UART2 (pins unknown)" },
    // AON GPIO (docs/hardware/gpio.md)
    { "gpio_oden",   0xf0419c80, "AON GPIO ODEN: 1 = open-drain" },
    { "gpio_data",   0xf0419c84, "AON GPIO DATA: b18 green, b17 red, b16 blue (0 = on), b14 SW1, b7 SW4" },
    { "led",         0xf0419c84, "= gpio_data" },
    { "gpio_iodir",  0xf0419c88, "AON GPIO IODIR: 1 = input" },
    { "gpio_mask",   0xf0419c94, "AON GPIO interrupt MASK" },
    { "gpio_stat",   0xf0419c9c, "AON GPIO STAT: latched changes, write 1 to clear" },
    // System (docs/hardware/system-blocks.md)
    { "chipid",      0xf0404000, "chip family ID: 72680010 = BCM7268 B0" },
    { "productid",   0xf0404004, "chip product ID" },
    { "straps",      0xf040401c, "straps: bits 0-4 boot device (1e = eMMC)" },
    { "resethist",   0xf041006c, "reset history: b0 power-on, b6 watchdog, b9 software (BOLT clears it)" },
    { "wdt_timeout", 0xf040a6a8, "watchdog timeout, 27 MHz ticks" },
    { "wdt_cmd",     0xf040a6ac, "watchdog CMD: ff00,00ff start; ee00,00ee stop; read = ticks left" },
    { "wake_event",  0xf041a080, "wake timer EVENT" },
    { "seconds",     0xf041a084, "wake timer COUNTER: seconds since power-on" },
    { "wake_alarm",  0xf041a088, "wake timer ALARM" },
    { "temp",        0xf04d1500, "temperature: b11 valid, b10:1 code; milli-C = 410040 - code*487" },
    { "aonsram",     0xf0410200, "AON SRAM 1 KB (survives soft/watchdog reset)" },
    { "gisb_addr",   0xf04007ec, "GISB error capture: address of the last failed access" },
    { "gisb_status", 0xf04007f4, "GISB error capture status" },
    { "gisb_master", 0xf04007f8, "GISB error capture master (40 = CPU)" },
    { "v3d_power",   0xf041d020, "V3D power island control" },
    // IR (docs/hardware/ir.md)
    { "ir_status",   0xf0419900, "IR kbd1 STATUS: b0 code ready" },
    { "ir_data",     0xf0419910, "IR kbd1 DATA0: received code" },
    { "ir_cmd",      0xf0419914, "IR kbd1 CMD: b4 CIR on, b5 irq enable" },
    // Interrupts (docs/hardware/interrupts.md)
    { "l2_status",   0xf0419c00, "AON L2 STATUS: b0 IR kbd1, b3 AON GPIO" },
    { "l2_mask",     0xf0419c04, "AON L2 MASK_STATUS" },
    { "gicd",        0xffd01000, "GIC-400 distributor" },
    { "gicc",        0xffd02000, "GIC-400 CPU interface" },
    // Display (docs/hardware/display.md)
    { "gfd_width",   0xf0641044, "GFD source width" },
    { "gfd_surface", 0xf0641048, "GFD surface address (visible picture)" },
    { "gfd_pitch",   0xf0641058, "GFD pitch" },
    { "gfd_height",  0xf0641174, "GFD height" },
    { "frame",       0xf0603484, "frame counter, 59.94 Hz" },
    { "flip0",       0xf0603488, "flip: write a buffer address here and to flip1" },
    { "flip1",       0xf060348c, "flip, second word" },
    { "rdc_desc0",   0xf0604000, "RDC descriptor 0 list address" },
    { "bgcolor",     0xf0645810, "CMP0 background colour (00 Y Cb Cr; rewritten every frame)" },
    // Audio (docs/hardware/audio.md)
    { "aud_dma",     0xf0ca00c0, "audio DMA run: 1 play, 0 stop" },
    { "ring_rd",     0xf0ca0800, "audio ring 0 read pointer (b31 flips per lap)" },
    { "ring_wr",     0xf0ca0804, "audio ring 0 write pointer" },
    { "ring_start",  0xf0ca0808, "audio ring 0 start" },
    { "ring_end",    0xf0ca080c, "audio ring 0 end (last byte)" },
    { "hdmi_n",      0xf06fa0c8, "HDMI audio N (040016c0 = 5824)" },
    { "hdmi_cts",    0xf06fa0cc, "HDMI audio CTS (00022551 = 140625), also at +4" },
    // USB, blitter (docs/hardware/usb.md, 2d-blitter.md)
    { "ehci0",       0xf0b00300, "EHCI0 (Bluetooth)" },
    { "ohci0",       0xf0b00400, "OHCI0 (Bluetooth)" },
    { "ehci1",       0xf0b00500, "EHCI1 (USB-A port)" },
    { "ohci1",       0xf0b00600, "OHCI1 (USB-A port)" },
    { "xhci",        0xf0b01000, "xHCI" },
    { "m2mc",        0xf09b0000, "M2MC 2D blitter" },
    { NULL, 0, NULL },
};

// "Do not touch" on registers.md. R = reading aborts or is unsafe,
// W = writing breaks something. Commands refuse these unless forced with '!'.
const struct danger dangers[] = {
    { 0xf041002c, 0xf041002f, DANGER_R | DANGER_W, "AON control: external abort on read" },
    { 0xf0404030, 0xf0404037, DANGER_W, "OTP fuses: never write" },
    { 0xf0404520, 0xf0404523, DANGER_W, "OTP fuses: never write" },
    { 0xf064105c, 0xf064105f, DANGER_R | DANGER_W, "display GFD: external abort on read" },
    { 0x7db08000, 0x7db0b6ff, DANGER_W, "display lists: writing blanks the screen until reboot" },
    { 0xf06fa828, 0xf06fa82b, DANGER_W, "HDMI audio packet set-up: breaks the picture" },
    { 0xf06fa884, 0xf06fa88b, DANGER_W, "HDMI audio packet set-up: breaks the picture" },
    { 0xf06fa898, 0xf06fa89b, DANGER_W, "HDMI audio packet set-up: breaks the picture" },
    { 0x7df00000, 0x7fffffff, DANGER_R | DANGER_W, "BL31 / SRR secure memory" },
    { 0x06400000, 0x0640ffff, DANGER_W, "EL3 monitor (smm64, or kboot's)" },
    { 0x00100000, 0x0013ffff, DANGER_W, "the monitor itself" },
    { 0x00600000, 0x0082ffff, DANGER_W, "the monitor's USB DMA, console scrollback and FAT buffers" },
    { 0, 0, 0, NULL },
};

// Kaon remote, custom code 0x0820: command byte -> key (docs/hardware/ir.md)
const struct irkey irkeys[] = {
    { 0x00, "POWER" }, { 0x05, "TV" }, { 0x04, "MUTE" }, { 0x10, "BACK" },
    { 0x11, "MENU" }, { 0x5a, "HOMEPAGE" }, { 0x18, "RED" }, { 0x19, "GREEN" },
    { 0x1a, "YELLOW" }, { 0x1b, "BLUE" }, { 0x0c, "EPG" }, { 0x50, "INFO" },
    { 0x0f, "SUBTITLE" }, { 0x0b, "SOUND" }, { 0x12, "UP" }, { 0x13, "DOWN" },
    { 0x14, "LEFT" }, { 0x16, "RIGHT" }, { 0x15, "OK" }, { 0x02, "VOLUMEUP" },
    { 0x03, "VOLUMEDOWN" }, { 0x06, "CHANNELUP" }, { 0x07, "CHANNELDOWN" },
    { 0x4c, "RECORD" }, { 0x43, "PLAYPAUSE" }, { 0x4d, "STOP" },
    { 0x4a, "FASTFORWARD" }, { 0x49, "REWIND" }, { 0x1c, "1" }, { 0x1d, "2" },
    { 0x1e, "3" }, { 0x40, "4" }, { 0x41, "5" }, { 0x42, "6" }, { 0x44, "7" },
    { 0x45, "8" }, { 0x46, "9" }, { 0x47, "0" }, { 0x57, "LAST" },
    { 0x0e, "SEARCH" }, { 0x1f, "SLEEP" }, { 0x4b, "NEXTSONG" },
    { 0x0d, "TUNER" }, { 0x4e, "PLAY" }, { 0x48, "PREVIOUSSONG" },
    { 0, NULL },
};
