# GPIO: LEDs, buttons, pin mux

The DTB does not describe the LEDs, buttons or IR receiver: on Broadcom
set-top boxes those belong to the proprietary Nexus drivers. The pin map
below was found by reading and writing the GPIO registers from BOLT with
`d -w` / `e -w`, one bit at a time.

## Summary

| | |
|---|---|
| Controllers | AON GPIO `0xf0419c80` (banks of 28 and 6 pins), main GPIO `0xf040a500` (32, 32, 19, 2) |
| Register layout | Linux `gpio-brcmstb`, banks `0x20` bytes apart |
| Mapped pins | LED1 green/red (AON 18/17), LED3 blue (AON 16), SW1 standby (AON 14), SW4 recovery/BT pairing (AON 7); Wi-Fi power AON 21/26 from the DTB |
| State after BOLT | only AON 17 and 18 are outputs; every other GPIO is an input |
| Tested | every pin in the map, from `BOLT>` with `d -w` / `e -w` |

## Controllers

Compatible `brcm,brcmstb-gpio`. The register layout matches Linux
`drivers/gpio/gpio-brcmstb.c`. Banks are 0x20 bytes apart:

| Offset | Register |
|---|---|
| `+0x00` | ODEN: open-drain enable |
| `+0x04` | DATA: pin level (read), output value (write) |
| `+0x08` | IODIR: 1 = input, 0 = output |
| `+0x0c` / `+0x10` / `+0x18` | EC / EI / LEVEL: interrupt trigger type |
| `+0x14` | MASK: interrupt enable |
| `+0x1c` | STAT: interrupt status, latches changes even while masked, write-1-to-clear |

| Controller | Base | Banks (pins) |
|---|---|---|
| AON GPIO (always-on) | `0xf0419c80` | 28, 6 |
| Main GPIO | `0xf040a500` | 32, 32, 19, 2 |

## Pin map

| Function | Pin | Electrical |
|---|---|---|
| LED1 green (power) | AON bank 0 bit 18 | open-drain, active-low; output after BOLT |
| LED1 red | AON bank 0 bit 17 | open-drain, active-low; output after BOLT. Green + red = orange |
| LED3 blue | AON bank 0 bit 16 | active-low; **input after BOLT**. To use: set ODEN, set DATA (off), then clear IODIR |
| SW1 (front standby button) | AON bank 0 bit 14 | input, active-low (reads 1 at rest). Stock Android: an interrupt on both edges (`nexus gpio`), standby/wake (`interrupts.md`) |
| SW4 (recovery / Bluetooth-pairing button) | AON bank 0 bit 7 | input, active-low (0 = pressed). Held at power-on, the stock BSU boots recovery. The stock DTB maps it as `gpio_keys_polled` `BT_PAIR` (key `0x18f`), and BOLT's env has `BT_PAIR upg_gio_aon 7`. Linux polls it: pressing it fires no interrupt (tested) |
| unknown (claimed by Nexus) | AON bank 0 bits 4, 5 | stock `/proc/interrupts` lists them as `nexus gpio`; bit 4 fired 6 times around boot. Not HDMI hot-plug, USB or the buttons (tested) |
| Wi-Fi power (`vreg-wifi-pwr`) | AON bank 0 bit 21 | from the DTB; input after BOLT |
| WLAN power (`vreg-wlan-pwr`) | AON bank 0 bit 26 | from the DTB; input after BOLT |

State after BOLT boots: AON bank 0 ODEN `00060000`, DATA `001340b8`, IODIR
`0ff9ffff`. Only bits 17 and 18 are outputs; every other GPIO on the chip is
an input.

Rules:
- Change registers with read-modify-write.
- Don't turn unknown inputs into outputs: another chip may be driving the pin.

## Mapping an unknown input (STAT method)

1. Clear STAT: `e -w 0xf0419c9c ffffffff` (write-1-to-clear).
2. Trigger the input once (press the button, plug the cable).
3. `d -w 0xf0419c80 0x40`: the newly set STAT bit (`+0x1c`) is the pin.

Main GPIO bank 1 STAT bits 8/9 (`0xf040a53c` = `00000300`) stay set and don't
clear.

## Pin mux

AON pin mux at `0xf0410700` (4-bit fields, 8 pins per register) reads
`00000000 00000000 00112222 00000100` under BOLT. The GPIO function code
differs per pin (LED pins 16–18 are `2`, Wi-Fi pins 21/26 are `1`), so the
fields can't be decoded without Broadcom documentation. Pins 0–15 are all
`0`, and SW1/SW4 (pins 14/7) work as GPIO there.

## IR receiver

`IR1` on the PCB is an IR **receiver**. The board has no IR transmitter,
although the SoC contains an IR blaster block (`irb`).

The receiver is not on a GPIO: remote-control presses change no GPIO DATA or
STAT bit. It feeds the SoC's IR receiver block, channel kbd1 at
`0xf0419900`, which decodes the remote's NEC codes once it is enabled. BOLT
doesn't enable it. Details and the remote's key codes: `ir.md`.

## Open questions

Not mapped yet:

| Item | Status |
|---|---|
| SW2 | probably the hard power switch (cuts power; not software-visible) |
| SW3 | footprint not fitted |
| HDMI hot-plug, Ethernet/USB detect lines | not yet tested with the STAT method |
