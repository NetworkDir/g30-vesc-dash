/*
 * User configuration. Every value can also be overridden from the make
 * command line, e.g.  make CFG="-DLINK_FULL_DUPLEX=1 -DLINK_FD_RX_PIN=2"
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DASH_CONFIG_H_
#define DASH_CONFIG_H_

/* ------------------------------------------------------------------ */
/* Link to the VESC                                                    */
/* ------------------------------------------------------------------ */

// 0: single wire (stock yellow wire, half duplex). Needs the Lisp bridge
//    script on the VESC (FW 6.06+), see lisp/g30dash_bridge.lisp.
// 1: two wires (full duplex). Yellow wire is TX to VESC RX, an extra wire
//    from VESC TX goes to LINK_FD_RX_PIN. The VESC runs its normal UART app.
#ifndef LINK_FULL_DUPLEX
#define LINK_FULL_DUPLEX            0
#endif

// nRF pin used as RX in full duplex mode (extra wire from VESC TX).
#ifndef LINK_FD_RX_PIN
#define LINK_FD_RX_PIN              20
#endif

// Pull on the bus pin while listening. The stock firmware pulls down; the
// m365-eload ESC drives the line open drain, so that build pulls up.
#ifndef LINK_LISTEN_PULL
#if APP_ELOAD
#define LINK_LISTEN_PULL            NRF_GPIO_PIN_PULLUP
#else
#define LINK_LISTEN_PULL            NRF_GPIO_PIN_PULLDOWN
#endif
#endif

#ifndef LINK_BAUDRATE
#define LINK_BAUDRATE               UART_BAUDRATE_BAUDRATE_Baud115200
#endif

/* ------------------------------------------------------------------ */
/* Button                                                              */
/* ------------------------------------------------------------------ */

// The stock power button is wired to the controller (green wire), not to
// the nRF. Two ways to get it:
// 0: the VESC reads it and the dash polls it (Lisp script, green wire to
//    the VESC RX pin in single wire mode).
// 1: a wire from the button to a free nRF pin (BUTTON_LOCAL_PIN), button
//    pulls the pin to GND.
#ifndef BUTTON_LOCAL
#define BUTTON_LOCAL                0
#endif

#ifndef BUTTON_LOCAL_PIN
#define BUTTON_LOCAL_PIN            2
#endif

#define BUTTON_DOUBLE_PRESS_MS      350     // max gap between two presses
#define BUTTON_LONG_PRESS_MS        2000    // power off / on

/* ------------------------------------------------------------------ */
/* Riding modes (applied with COMM_SET_MCCONF_TEMP_SETUP, not stored)   */
/* ------------------------------------------------------------------ */

// Motor current scale (0..1) and speed limit in km/h (0 = no limit) per
// mode. Duty, power and battery current limits are read from the VESC and
// left untouched.
#define MODE_ECO_CURRENT_SCALE      0.5f
#define MODE_ECO_SPEED_KMH          15
#define MODE_D_CURRENT_SCALE        0.8f
#define MODE_D_SPEED_KMH            25
#define MODE_S_CURRENT_SCALE        1.0f
#define MODE_S_SPEED_KMH            0

// 0 = ECO, 1 = D, 2 = S
#define MODE_DEFAULT                1

// Forward the mode limits to other controllers on the CAN bus.
#define MODE_FORWARD_CAN            1

/* ------------------------------------------------------------------ */
/* Throttle and brake                                                  */
/* ------------------------------------------------------------------ */

// Raw 10 bit ADC counts. The released position is sampled at boot; these
// are the fallbacks and the plausibility window.
#define LEVER_IDLE_DEFAULT          170
#define LEVER_IDLE_MAX              330     // higher at boot = lever pressed
#define LEVER_FULL_DEFAULT          760     // grows if a larger value is seen
#define LEVER_FAULT_LOW             40      // below: sensor disconnected
#define LEVER_FAULT_HIGH            1000    // above: sensor shorted
#define LEVER_DEADBAND              0.06f   // fraction of travel

// Command rate to the VESC (COMM_SET_CHUCK_DATA). The VESC Remote app
// timeout should be a few times this period.
#define CONTROL_PERIOD_MS           30

/* ------------------------------------------------------------------ */
/* Display                                                             */
/* ------------------------------------------------------------------ */

#define DISPLAY_MPH                 0
#define DISPLAY_BRIGHTNESS          7       // 0..7
#define DISPLAY_BATT_WHEN_STOPPED   0       // show battery % instead of 0
#define TEMP_WARN_MOS_C             80
#define TEMP_WARN_MOTOR_C           100
#define TELEMETRY_PERIOD_MS         100

/* ------------------------------------------------------------------ */
/* Power                                                               */
/* ------------------------------------------------------------------ */

// 1: start in the off state and wait for a button press (stock behaviour).
// Keep 0 until the button path is confirmed to work.
#define START_OFF                   0

// Turn off after this many minutes without throttle, brake or movement
// (0 = never). Only used when the button can turn the dash back on.
#define AUTO_OFF_MIN                0

/* ------------------------------------------------------------------ */
/* BLE                                                                 */
/* ------------------------------------------------------------------ */

// VESC Tool lists any device advertising the Nordic UART service, the
// "VESC" prefix just makes it easy to spot.
#if APP_ELOAD
#define BLE_DEVICE_NAME             "M365 ELOAD"
#else
#define BLE_DEVICE_NAME             "VESC G30"
#endif

/* ------------------------------------------------------------------ */
/* m365-eload build (make APP=eload)                                   */
/* ------------------------------------------------------------------ */

// Potentiometer on the throttle input, raw 10 bit counts (ratiometric to
// VDD) for 0 % and 100 %. A pot between VDD and GND gives about 0..1023;
// for the stock hall lever use about 170 / 760.
#ifndef POT_RAW_MIN
#define POT_RAW_MIN                 20
#endif
#ifndef POT_RAW_MAX
#define POT_RAW_MAX                 1000
#endif

#endif /* DASH_CONFIG_H_ */
