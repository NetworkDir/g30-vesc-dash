# BLE updates (Nordic secure DFU)

After a one-time ST-Link install of the *factory* image, the firmware can be
updated over Bluetooth with a standard Nordic DFU app. No cable, no
ScooterHacking. Updates are signed; the dashboard only accepts packages
signed with the matching private key.

## Chip requirement (important)

The BLE DFU bootloader runs the S130 SoftDevice while it updates, which needs
**32 KB of RAM**. That means the **nRF51822-QFAC** (32 KB) variant.

- The **application** itself fits in 16 KB and runs on either variant, so the
  plain `*_full.hex` install (no OTA) works on any G30 dash.
- The **bootloader / OTA** only works on a 32 KB (QFAC) dash. On a 16 KB
  (QFAA) part the bootloader hard-faults at boot.

Check the RAM over SWD before flashing the factory image. nRF51 FICR holds
the block count at 0x10000034 and block size at 0x10000038:

```bash
openocd -f interface/stlink.cfg -f target/nrf51.cfg -c "init" -c "mdw 0x10000034 2" -c "exit"
```

Multiply the two words. `0x00000001 0x00008000` = 32 KB (QFAC, OTA works).
`0x00000001 0x00004000` = 16 KB (QFAA, use the plain `_full.hex`, no OTA).

## What is what

| File | Contents | Use |
|---|---|---|
| `*_factory.hex` | SoftDevice + bootloader + app + settings | one-time ST-Link install for OTA |
| `*_dfu.zip` | signed app only | later BLE updates |
| `*_full.hex` | SoftDevice + app (no bootloader) | install without OTA (any chip) |

## First install (ST-Link, once)

Back up the stock flash first (see the main README). Then:

```bash
openocd -f interface/stlink.cfg -f target/nrf51.cfg -c "init" -c "halt" -c "nrf5 mass_erase" -c "program release/g30_vesc_dash_single_wire_factory.hex verify reset exit"
```

The dashboard boots normally. The bootloader only takes over when an update
is requested or when the app is missing/corrupt.

## Updating over Bluetooth

1. Put the dashboard into update mode: **power it on while holding both the
   throttle and the brake fully pressed.** The bootloader then advertises as
   `DfuTarg` instead of running the dashboard. (Releasing the levers and
   power-cycling returns to normal operation.)
2. On a phone, open **nRF Connect for Mobile** or **nRF Toolbox** (Nordic,
   free), connect to `DfuTarg`, choose DFU / the update icon, and select the
   `*_dfu.zip` for your variant.
3. When it finishes, the dashboard reboots into the new firmware.

Any DFU client that speaks Nordic's secure DFU works; nRF Connect is the
easy one.

## Signing key

`firmware/dfu_key/dfu_private.pem` signs the DFU packages, and its public key
is baked into the bootloader (`firmware/bootloader/dfu_public_key.c`). It is
**not** committed, keep a backup. The bootloader only accepts packages
signed with this key, so:

- To publish a new OTA build, sign it with the same key (`package_dfu.sh`
  uses it automatically).
- If the key is lost, OTA for an already-flashed dash stops working, but you
  can always re-flash over ST-Link with a factory image built from a new key.

## Rebuilding the DFU artifacts

One-time, build the micro-ecc crypto library the bootloader needs:

```bash
cd ../sdk/SDK12/external/micro-ecc
git clone https://github.com/kmackay/micro-ecc micro-ecc
cd nrf51_armgcc/armgcc && make   # arm-none-eabi toolchain on PATH
```

Generate a signing key once (keep it):

```bash
cd firmware && python -m nordicsemi keys generate dfu_key/dfu_private.pem
python -m nordicsemi keys display --key pk --format code dfu_key/dfu_private.pem --out_file bootloader/dfu_public_key.c
```

Then build and package:

```bash
cd firmware
./build.sh LINK=hd && ./build.sh LINK=fd
./bootloader/build_bootloader.sh
./package_dfu.sh hd 2 && ./package_dfu.sh fd 2
```

The last argument is the application version. The bootloader refuses a
package older than the app it is running, so raise it with every release
(v0.1.0 was 1, v0.2.0 is 2).

`nrfutil` (the Nordic Python tool, `pip install nrfutil`) provides
`nordicsemi`. Outputs land in `firmware/build/<hd|fd>/`.
