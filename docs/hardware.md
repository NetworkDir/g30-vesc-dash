# G30 BLE dashboard hardware notes

Reverse engineered from the stock BLE 1.1.7 application image (34252 bytes,
loads at `0x18000`).

## Chip and memory

- nRF51822 (Cortex-M0, marked N51822 / N51802), 16 kB RAM is enough
- Stock: S110 v8 SoftDevice `0x00000–0x17FFF`, application at `0x18000`,
  DFU bank 1 at `0x29800`, bootloader at `0x3C000` (classic Nordic dual
  bank layout), persistent storage pages just below the bootloader
- No 32 kHz crystal: LFCLK is synthesized from the 16 MHz crystal
- This firmware: S130 2.0.1 SoftDevice, application at `0x1B000`, settings
  in the last flash page

## Pins (board variant 2 = G30)

The stock firmware reads P0.08 with a pull-up at boot. High selects board
variant 1 (discrete LEDs on P0.00/14/21/29/30, levers on AIN3/AIN4, buzzer
P0.09, light P0.10), low selects variant 2, the G30 with the TM1637
display. Only variant 2 is used here.

| Function | Pin | Notes |
|---|---|---|
| LED driver CLK | P0.05 | TM1637 compatible, bit-banged |
| LED driver DIO | P0.04 | LSB first, ACK clock after each byte |
| Throttle | P0.27 / AIN1 | 10 bit, input 1/3, reference VDD/3 (ratiometric) |
| Brake | P0.26 / AIN0 | same |
| Headlight | P0.03 | active high |
| Buzzer | P0.25 | active buzzer, active high |
| Variant strap | P0.08 | low on the G30 |
| Bus (yellow wire) | P0.15 | see below |
| Bus parking pin | P0.20 | see below |
| Power button | – | not connected to the nRF, goes to the controller (green wire) |

The stock firmware also samples AIN3 (P0.02) and AIN2 (P0.01) on the G30
but never uses the results.

### Single wire bus

115200 8N1. The nRF51 UART has no half duplex mode, so the stock firmware
swaps the UART pins for every frame:

| State | TXD | RXD | P0.15 | P0.20 |
|---|---|---|---|---|
| Listening | P0.20 | P0.15 | input, pull-down | output, idle high |
| Sending | P0.15 | P0.20 | output | input, pull-down |

P0.15 is the bus. The other pin only keeps the UART from hearing its own
transmission. This firmware does exactly the same.

## Display

TM1637 compatible driver, six grids. Command sequence: `0x40` (write,
auto increment), `0xC0` + 6 data bytes, `0x88 | brightness`; `0x80`
turns the display off.

| Grid | Bits | Content |
|---|---|---|
| G0 | 0–6 | speed, tens digit (blank when 0) |
| G1 | 0–6 | fault code, tens digit |
| G2 | 0–6 | speed, ones digit |
| G3 | 0–6 | fault code, ones digit |
| G4 | 0–4 | battery bars 1–5 |
| G4 | 5 | temperature warning (blinks) |
| G4 | 6 | fault (wrench) |
| G4 | 7 | battery empty (blinks) |
| G5 | 0 / 1 | km/h / mph |
| G5 | 2 / 3 / 4 | ECO / D / S |
| G5 | 5 | headlight |
| G5 | 6 | Bluetooth |
| G5 | 7 | lock |

Digits use a..g = bit 0..6, font `3F 06 5B 4F 66 6D 7D 07 7F 6F`. Bit 7 of
G0–G3 is only lit in the power-on test (`FF 00 FF 00 7F FF`). The physical
position of each icon is inferred from the code; the segment test mode
(throttle + brake held at power-up) shows them one by one.

Battery: level = ceil(battery % / 10). Level 1 blinks the empty LED, even
levels show level/2 bars, odd levels add a blinking bar on top.

A fault alternates between the speed and the code, with the wrench lit and
the unit LEDs off.

## Stock protocol (Ninebot, for reference)

Frames `5A A5 len src dst cmd arg payload crc16`, checksum is the inverted
16 bit sum. The dashboard (0x21) sends the levers to the controller (0x20)
with command 0x65 (`04 throttle brake flags beep`, raw 8 bit ADC values)
and 0x64. The controller answers 0x64 with six bytes:

| Byte | Meaning |
|---|---|
| 0 | status: bit1 ECO, bit2 S, bit3 charging, bit4 off, bit5 lock, bit6 mph, bit7 temperature warning, none = D |
| 1 | battery % |
| 2 | headlight |
| 3 | beep request |
| 4 | speed |
| 5 | fault code (254 = display test) |

This firmware does not use the Ninebot protocol, it speaks VESC packets.
