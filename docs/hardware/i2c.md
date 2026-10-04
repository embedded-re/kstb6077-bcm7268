# I2C (BSC controllers)

The SoC has five I2C masters (Broadcom "BSC"). Two of them have something on
the bus: **ch0 is the HDMI DDC** (EDID, HDCP) and **ch3 carries a device at
`0x67`**, the stock firmware's Si2168C demodulator.

Tested on the stock box on 2026-10-04, from `go -64` (EL2, MMU off), after a
power-on and BOLT's splash. HDMI was connected to a TV. Probes and outputs
are in [`../evidence/i2c/`](../evidence/i2c/).

## Summary

| | |
|---|---|
| Controllers | 5 BSC masters: `0xf040a300`, `0xf0419a80`, `0xf0419b00`, `0xf0419b80`, `0xf040a400` |
| Devices found | ch0: HDMI DDC (`0x30`, `0x3a`, `0x50`); ch3: `0x67` (Si2168C demodulator per Nexus) |
| State after BOLT | idle; ch3 clock set by BOLT's splash script; all BSC interrupts masked |
| Register names | Linux `i2c-brcmstb` |
| Tested | one-byte reads, a scan of every address on all five channels |

## Channels

| Ch | Base | L2 interrupt bit | Interrupt name | Bus scan (tested) |
|---|---|---|---|---|
| 0 | `0xf040a300` | UPG BSC `0xf040a640` bit 0 | `iica` | ACK at **`0x30`, `0x3a`, `0x50`**: HDMI DDC |
| 1 | `0xf0419a80` | AON BSC `0xf0419c40` bit 0 | `iicb` | every address **times out** |
| 2 | `0xf0419b00` | AON BSC `0xf0419c40` bit 1 | `iicc` | nothing answers (all no-ACK) |
| 3 | `0xf0419b80` | AON BSC `0xf0419c40` bit 2 | `iicd` | ACK at **`0x67`** only |
| 4 | `0xf040a400` | UPG BSC `0xf040a640` bit 1 | `iice` | every address **times out** |

- The bases come from Nexus (`BI2C_OpenChannel`: BSC_A `0x2040a300` plus a
  per-channel offset table `{0, 0xf780, 0xf800, 0xf880, 0x100}` at `.rodata
  0xbb454`). All five answer reads (tested).
- The L2 bits come from Nexus's interrupt-ID table next to it (`.rodata
  0xbb468`; an ID is `(L2 address >> 5) << 8 | bit`). They match the
  interrupt names the stock kernel attaches to those bits
  ([`interrupts.md`](interrupts.md)). The channel ↔ bit pairing itself is from Nexus, not
  tested.
- On ch0, `0x50` is the EDID EEPROM, `0x30` the E-DDC segment pointer and
  `0x3a` the HDCP receiver port. Nexus gives the HDMI output I2C handle 0,
  which opens BSC channel 0, so the two agree.
- On ch3, Nexus (`NEXUS_Platform_InitFrontend`) opens a **Silicon Labs
  Si2168C** (DVB-T/T2/C demodulator) at I2C address 103 = `0x67`, using I2C
  handle 3. Under stock Android, `iicd` counts about 10 interrupts per second
  ([`interrupts.md`](interrupts.md)), presumably the demodulator being polled. The ACK at
  `0x67` is tested; that the chip is a Si2168C is from Nexus only.
- "Times out" means the transfer never completed (no done bit within
  ~11 ms). Either the bus isn't wired or its pins aren't muxed to the
  controller. Which one is unknown.
- The stock kernel registers **no** Linux I2C adapters (`/sys/bus/i2c/devices`
  is empty): Nexus drives all five.

## Registers

Each block is 0x58 bytes: offsets `+0x58` and `+0x5c` abort on all five
(tested). Names are Linux `i2c-brcmstb` ones. The scan used the ones marked
"used" and got consistent results, so they are tested in that sense:

| Offset | Name | Used by the scan |
|---|---|---|
| `+0x00` | CHIP_ADDRESS (7-bit address << 1, bit 0 = read) | yes |
| `+0x04…+0x20` | DATA_IN0–7 | `DATA_IN0` (received byte, bits 7:0) |
| `+0x24` | CNT_REG (byte count) | yes, `1` |
| `+0x28` | CTL_REG: bits 1:0 transfer type (1 = read), 5:4 SCL select, 6 interrupt enable, 7 clock divide | yes, `0x91` |
| `+0x2c` | IIC_ENABLE: bit 0 start, bit 1 done, bit 2 no ACK | yes |
| `+0x30…+0x4c` | DATA_OUT0–7 | |
| `+0x50` | CTLHI_REG | yes, `0` |
| `+0x54` | SCL_PARAM | |

A one-byte read: `+0x2c = 0`, `+0x00 = addr<<1 | 1`, `+0x24 = 1`,
`+0x28 = 0x91`, `+0x2c = 1`, then poll `+0x2c` until bit 1 is set. Bit 2 set
means no ACK.

## State after power-on and BOLT

All registers read `0` except:

| Ch | `+0x28` CTL | `+0x50` CTLHI |
|---|---|---|
| 0 | `0` | `000000c0` |
| 3 | `000000d3` | `00000040` |

The ch3 values come from BOLT's splash script, the register-write list it
replays to set up the display ([`display.md`](display.md), SplashData at `0x07056f30`:
1381 `{register, value}` entries from `0x07054318`). Entries 776–779 write
ch3 CTL `0x93`, CTLHI `0x40`, CTL `0xd3`, CTLHI `0x40`, just before the
HDMI set-up. That is the sequence of Nexus's `BI2C_OpenChannel` (set the
clock, then OR in interrupt enable `0x40`). The script was read from the
modified box's BOLT dump; the stock box's values match it. Where ch0's
CTLHI `0xc0` comes from is unknown.

Both BSC L2 controllers have every bit masked after BOLT: `0xf040a640` MASK
= `7`, `0xf0419c40` MASK = `f`.

## Open questions

- Whether ch1 and ch4 time out because they aren't wired or because their
  pins aren't muxed.
- Where ch0's CTLHI `0xc0` comes from.
- Whether the channel ↔ L2 bit pairing (from Nexus) is right.
