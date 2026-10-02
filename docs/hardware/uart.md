# UARTs

The BCM7268 has three 16550-compatible UARTs. UART0 is the console; UART1
and UART2 are working but unused by BOLT. Sources: the original DTB and
register dumps and a loopback test from BOLT (`../bolt/raw/uart_probe.txt`).

## Instances

| | Base | DTB alias | GIC interrupt | Clock gate bit | Clock mux | Used by BOLT |
|---|---|---|---|---|---|---|
| UART0 | `0xf040c000` | `serial0` | SPI `0x46` = ID 102 | `0xf04e0488` bit 4 | `0xf04e051c` | **console**, 115200 8N1 |
| UART1 | `0xf040d000` | `serial1` | SPI `0x47` = ID 103 | `0xf04e0488` bit 5 | `0xf04e0520` | no |
| UART2 | `0xf040e000` | `serial2` | SPI `0x48` = ID 104 | `0xf04e0488` bit 6 | `0xf04e0524` | no |

DTB: compatible `brcm,bcm7268-uart`, `brcm,bcm7271-uart`, `ns16550a`;
`reg-shift = 2`, `reg-io-width = 4`, `fifo-size = 32`, `auto-flow-control`.

## Clock

- Each UART has an **81 MHz** baud clock (`clock-frequency = 0x4d3f640`).
- The clock gate at `0xf04e0488` is `set-bit-to-disable`. It reads
  `00000000`, so all three clocks are on.
- The clock muxes (`0xf04e051c/20/24`, bits 1:0) all read `0`.

Divisor for a baud rate: `81,000,000 / (16 × baud)`.

| Baud | Divisor | Actual | Error |
|---|---|---|---|
| 115200 | 44 (`0x2c`) | 115057 | −0.12 % |
| 9600 | 527 (`0x20f`) | 9606 | +0.06 % |

## Registers (16550, 4-byte stride)

| Offset | Read | Write |
|---|---|---|
| `+0x00` | RBR: received byte (DLAB = 0) / DLL (DLAB = 1) | THR: byte to send / DLL |
| `+0x04` | IER / DLM (DLAB = 1) | same |
| `+0x08` | IIR: interrupt ID, FIFO state in bits 7:6 | FCR: FIFO control |
| `+0x0c` | LCR: bit 7 = DLAB, bits 1:0 = word length (`3` = 8 bits) | same |
| `+0x10` | MCR: bit 4 = internal loopback | same |
| `+0x14` | LSR: bit 0 data ready, bit 5 THR empty, bit 6 transmitter empty, bit 7 RX FIFO error | |
| `+0x18` | MSR: bit 4 = CTS | |
| `+0x1c` | SCR: scratch | same |

Reading RBR removes a byte from the receive FIFO; reading LSR clears its
error bits. When the FIFO is empty, RBR keeps returning the last byte.

## State after BOLT

| Register | UART0 | UART1 | UART2 |
|---|---|---|---|
| IER | `0` (polled) | `0` | `0` |
| IIR | `c1`: FIFOs enabled, no interrupt | `01`: FIFOs off | `01` |
| LCR | `03`: 8N1 | `00`: reset state | `00` |
| divisor | 44 (inferred from 115200) | `0` | not read |
| LSR | busy while BOLT prints | `60`: idle | `60` |
| MSR | `10`: CTS asserted | `10` | `10` |

UART0's divisor wasn't read: setting DLAB on the live console would garble it.

## Setting up UART1 or UART2 (tested on both)

```
e -w 0xf040d00c 00000083     LCR: DLAB = 1, 8N1
e -w 0xf040d000 0000002c     DLL = 44  → 115200 baud
e -w 0xf040d004 00000000     DLM = 0
e -w 0xf040d00c 00000003     LCR: DLAB = 0, 8N1
e -w 0xf040d008 00000007     FCR: enable and reset FIFOs
```
(For UART2, use `0xf040e0xx`.)

## Loopback test (both work)

With MCR bit 4 set, the transmitter feeds the receiver internally, with no
pins involved:

```
e -w 0xf040d010 00000010     MCR: loopback on
e -w 0xf040d000 00000055     send 0x55
d -w 0xf040d014 4            LSR = 61  (data ready)
d -w 0xf040d000 4            RBR = 55  (received)
e -w 0xf040d010 00000000     loopback off
```

UART1 returned `55` and `a3`; UART2 returned `55`. Both blocks are clocked
and functional.

## Stock kernel

The stock Linux kernel registers all three: `ttyS0` (IRQ 102), `ttyS1`
(IRQ 103) and `ttyS2` (IRQ 104), each with `base_baud = 5062500` = 81 MHz / 16.
That confirms the clock (`../stock-firmware.md`).

## Pins: unknown

Which package pins UART1/UART2 are routed to, if any, is unknown.

- With loopback off, UART1's LSR showed `e0` (RX FIFO error). That is
  typical of an RX input that isn't connected to anything, a hint that
  UART1 isn't muxed to a pin (⚠️ not proven).
- UART2 showed no errors and never received data.
- `tz console on uart1` (BOLT's secure-console setup) is refused with
  `TZ not initialized`. It would need `tz init` first, which was not run.
- Pin-mux registers under BOLT, for later comparison:
  - SUN_TOP_CTRL `0xf0404100`: `00000000 00000000 00000000 66660000 00066066 14407700 00000001 00000000 …`
  - pad control `0xf040413c` onwards: `15500090 00005555 15566aa8 1555595a 15555555 15555555 2aaaaa95 02aaa182`
  - AON: `0xf0410700` `00000000 00000000 00112222 00000100`, and pad control `0xf0410714` `155a5544 15654001 00000255`

  (Full dumps are in `../bolt/raw/uart_probe.txt`.)

To find a TX pin physically: make the UART transmit continuously, for
example with BOLT's `loop "e -w 0xf040d000 00000055" -count=100000`, and
probe test pads with a USB-UART adapter's RX (115200 8N1) or a scope.
