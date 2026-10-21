# G30 VESC Dash

Replacement firmware for the stock Segway-Ninebot Max G30 BLE dashboard
(nRF51822) that makes it a VESC dashboard:

- **VESC Tool over BLE** through the dashboard (Nordic UART service, works
  with the desktop and mobile VESC Tool like any VESC BLE module)
- **Throttle and brake** levers are read by the dashboard and sent to the
  VESC as VESC Remote (nunchuk) commands, so throttle curve, ramping and
  timeout are configured in VESC Tool
- **Display**: speed, battery bars, riding mode, headlight, Bluetooth,
  fault code with wrench icon, temperature warning, lock
- **Button**: headlight, riding modes (ECO / D / S), lock, soft power off
- **Riding modes** set current scale and speed limit on the VESC at run
  time (`COMM_SET_MCCONF_TEMP_SETUP`, nothing is written to flash)
- Headlight and buzzer driven like the stock firmware

> **Status:** builds cleanly and passes a host simulation of both link
> modes (see [Tests](#tests)), but it has **not been run on a real
> dashboard yet**. Do the first test with the wheel off the ground.

## How it works

The stock dashboard has a single nRF51822 that drives the LED display,
reads the levers and talks to the controller over one half duplex wire.
The power button is wired to the controller (green wire), not to the nRF.
Details are in [docs/hardware.md](docs/hardware.md).

Two ways to connect the dashboard to the VESC:

| | Single wire (default) | Two wires |
|---|---|---|
| Dash modification | none | one extra wire soldered to the nRF |
| VESC side | Lisp bridge script (FW **6.06+**) | stock UART app, Lisp only for the button |
| Button | green wire to VESC RX, reported by the script | VESC GPIO + script, or wired to the nRF |
| Build | `make` | `make LINK=fd` |

In single wire mode the dashboard is the bus master: it sends one VESC
frame, then listens until the answer has arrived (or a per-command
timeout), the same pin swap the stock firmware uses. The Lisp script on the
VESC passes every frame to the VESC command interface (`cmds-proc`) and
writes the answers back, so VESC Tool commands from the phone travel the
same wire as the dashboard's own throttle and telemetry frames.

## Wiring

### Single wire

| Dash wire | VESC COMM port |
|---|---|
| Red | 5V |
| Black | GND |
| Yellow (data) | TX |
| Green (button) | RX, and a 1 kΩ resistor from RX to 3.3V |

The headlight is powered from the dashboard, so it draws from the VESC 5V
rail.

### Two wires

| Dash | VESC COMM port |
|---|---|
| Red | 5V |
| Black | GND |
| Yellow (data, dash TX) | RX |
| Extra wire on nRF `LINK_FD_RX_PIN` (default P0.20) | TX |
| Green (button) | a free GPIO (e.g. servo/PPM pin, 1 kΩ pull-up to 3.3V) |

P0.20 is only a parking pin in the stock firmware. Pick a pin that you can
actually solder to and set `LINK_FD_RX_PIN`. With `BUTTON_LOCAL=1` the
button can also be wired to a free nRF pin (`BUTTON_LOCAL_PIN`, pressed =
GND) instead of the VESC.

## VESC setup

1. **Motor Settings → Additional Info → Setup**: wheel diameter, motor
   poles and gear ratio (speed on the display and the mode speed limits use
   these), battery type and cell count (battery bars).
2. **App Settings → VESC Remote**: Control Type *Current No Reverse*,
   Deadband about 0.02 (the dash already has its own), ramp times and
   throttle curve as you like.
3. **App Settings → General**: Timeout about 500 ms. Two wire mode: App to
   Use *UART*, baud rate 115200.
4. **Lisp** ([lisp/g30dash_bridge.lisp](lisp/g30dash_bridge.lisp)): over
   USB, open the Lisp tab, load the script, upload with *Run on boot*.
   Two wire mode: set `bridge` to 0 and `btn-pin` to the pin the button
   is on (or skip the script if the button is wired to the nRF).

Stopping or re-uploading the script over the dashboard BLE cuts the link in
single wire mode, edit it over USB.

## Flashing

The dashboard is flashed over SWD. The SWD pads (3.3V, SWDIO, SWCLK, GND)
are next to the nRF on the dashboard board; SWCLK is the one with the
pull-down resistor. Any ST-Link V2 or J-Link works. Power the dashboard
from the programmer's 3.3V or through the red wire.

Prebuilt images are in [release/](release/). The `_full.hex` files
contain the S130 SoftDevice and the application and need a full chip
erase, which also removes the stock SoftDevice, bootloader and firmware.

**Back up first.** The stock firmware cannot be reinstalled without a full
flash dump:

```bash
openocd -f interface/stlink.cfg -f target/nrf51.cfg -c "init" -c "halt" -c "dump_image stock_flash.bin 0x0 0x40000" -c "dump_image stock_uicr.bin 0x10001000 0x400" -c "exit"
```

If the dump is all zeros the chip is read protected; the mass erase below
still works.

**OpenOCD + ST-Link** (e.g. xPack OpenOCD on Windows):

```bash
openocd -f interface/stlink.cfg -f target/nrf51.cfg -c "init" -c "halt" -c "nrf5 mass_erase" -c "program release/g30_vesc_dash_single_wire_full.hex verify reset exit"
```

**J-Link** (`JLink.exe -device nRF51822_xxAA -if SWD -speed 1000`):

```
erase
loadfile release/g30_vesc_dash_single_wire_full.hex
r
g
exit
```

Later updates only need the `_app.hex` (no erase).

## Using it

| Action | Result |
|---|---|
| Single press | Headlight on/off |
| Double press | Mode ECO → D → S (1, 2, 3 beeps), remembered across restarts |
| Brake held + double press | Lock on/off. Locked: no throttle, brakes and beeps when pushed |
| Long press (2 s), standing still | Soft off: display off, no throttle, brake still works |
| Press while off | On again |
| Throttle + brake held at power-up | Segment test: shows "grid bit" then lights that single segment |

Display:

- `--` : no answer from the VESC (check wiring, script, baud rate)
- Wrench + number, alternating with the speed: VESC fault code, or 14 / 15
  for a disconnected throttle / brake sensor
- Blinking thermometer: MOSFET or motor temperature above the warning level
- Battery: one bar per 20 %, the top bar blinks in its lower half

The throttle is ignored until it has been released once after power-up,
and the levers' released position is calibrated at every power-up, so keep
your hands off them while the dash boots.

### Riding modes and VESC Tool

A mode change sets *Current max scale* and the ERPM limits on the VESC. VESC
Tool reads these current values, so if you write the motor configuration
while ECO or D is active, the limited values get stored. Switch to S (no
speed limit, full current by default) or check these fields before writing.
Duty, power and battery current limits are read from the VESC and kept.

## Configuration

All options are in [firmware/src/dash_config.h](firmware/src/dash_config.h):
link mode and pins, mode current scales and speed limits, lever
thresholds, km/h or mph, warning temperatures, start in the off state,
auto off, BLE name.

## Building

Needs GNU make, `arm-none-eabi-gcc` and the
[nRF5 SDK 12.3.0](https://developer.nordicsemi.com/nRF5_SDK/nRF5_SDK_v12.x.x/)
unpacked to `../../sdk/SDK12` relative to `firmware/` (or pass
`SDK_ROOT=`). On Windows with MSYS2:

```bash
pacman -S make mingw-w64-ucrt-x86_64-arm-none-eabi-gcc mingw-w64-ucrt-x86_64-arm-none-eabi-newlib
```

```bash
cd firmware && ./build.sh
```

`./build.sh LINK=fd` builds the two wire version, `./build.sh CFG="-DSTART_OFF=1"`
overrides any option. Output goes to `firmware/build/<hd|fd>/`.

## Tests

```bash
cd firmware && ./build.sh test
```

Builds the link layer, dashboard logic and display code for the host with
mocked hardware and runs them against a simulated VESC in both link modes:
boot and calibration, mode application, display content, throttle and
brake output, every button action, lock, soft off, throttle arming, sensor
faults, VESC Tool traffic while riding (including 450 byte answers and a
slow, jittery Lisp side), link loss and recovery. It also checks that the
single wire bus never has a collision or a byte sent to a side that is not
listening.

## Credits and license

- BLE bridge based on [nrf51_vesc](https://github.com/vedderb/nrf51_vesc)
  by Benjamin Vedder, VESC packet code from the
  [VESC firmware](https://github.com/vedderb/bldc)
- Wiring of the single wire link follows
  [vesc_g30_dash](https://github.com/Sharkboy-j/vesc_g30_dash)

GPL-3.0-or-later, see [LICENSE](LICENSE).
