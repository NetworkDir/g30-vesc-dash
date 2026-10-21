/*
 * Pin map of the Segway-Ninebot Max G30 BLE dashboard (nRF51822, QFN48).
 *
 * Derived from the stock BLE 1.1.7 application image. The stock firmware
 * supports two board variants selected by a strap on P0.08 at boot:
 *   P0.08 high -> variant 1 (discrete LEDs, not a G30)
 *   P0.08 low  -> variant 2 (TM1637 LED driver, the G30 dashboard)
 * Only variant 2 is supported here.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef BOARD_G30_H_
#define BOARD_G30_H_

#include "nrf.h"

// TM1637-compatible LED driver, bit-banged two-wire bus, LSB first.
#define BOARD_TM1637_CLK_PIN        5
#define BOARD_TM1637_DIO_PIN        4

// Hall sensors of the levers. ADC is ratiometric to VDD (1/3 input
// prescaling, VDD/3 reference), 10 bit.
#define BOARD_THROTTLE_AIN          ADC_CONFIG_PSEL_AnalogInput1   // P0.27
#define BOARD_BRAKE_AIN             ADC_CONFIG_PSEL_AnalogInput0   // P0.26

// Headlight driver (the light sits in the dashboard housing), active high.
#define BOARD_HEADLIGHT_PIN         3

// Active buzzer, active high.
#define BOARD_BUZZER_PIN            25

// Board variant strap, reads low on the G30.
#define BOARD_VARIANT_PIN           8

// Single-wire bus to the controller (yellow wire). The stock firmware
// swaps TXD/RXD between these two pins: the bus pin is RX while listening
// and TX while sending, the other pin is a parking spot so the UART never
// hears its own echo.
#define BOARD_BUS_PIN               15
#define BOARD_BUS_PARK_PIN          20

#endif /* BOARD_G30_H_ */
