/*
 * G30 dashboard LED layout. Six TM1637 grids, from the stock firmware:
 *
 *   G0 bit0-6  speed, tens digit      G1 bit0-6  error code, tens digit
 *   G2 bit0-6  speed, ones digit      G3 bit0-6  error code, ones digit
 *   G4 bit0-4  battery bars 1..5      G4 bit5    temperature warning
 *   G4 bit6    fault (wrench)         G4 bit7    battery empty
 *   G5 bit0    km/h                   G5 bit1    mph
 *   G5 bit2    ECO                    G5 bit3    D
 *   G5 bit4    S                      G5 bit5    headlight
 *   G5 bit6    Bluetooth              G5 bit7    lock
 *
 * Digits use the usual a..g = bit0..bit6 encoding. Bit 7 of G0..G3 is not
 * driven by the stock firmware (it is lit only in the power-on test).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DISPLAY_H_
#define DISPLAY_H_

#include <stdint.h>
#include <stdbool.h>

typedef enum {
	DISP_MODE_ECO = 0,
	DISP_MODE_D,
	DISP_MODE_S
} disp_mode_t;

typedef struct {
	bool link_ok;       // false: "--" and no battery
	uint8_t number;     // 0..99, speed or battery %
	uint8_t batt_pct;   // 0..100
	disp_mode_t mode;
	bool locked;
	bool light;
	bool ble;
	bool mph;
	bool temp_warn;
	uint8_t fault;      // 0: none
} display_view_t;

void display_init(void);

// Render a view. Call periodically (blinking is time based).
void display_show(const display_view_t *v, uint32_t now_ms);

// Raw grid bytes, for the power-on test and the segment test.
void display_raw(const uint8_t grids[6]);

// Shows a two digit number with nothing else lit.
void display_number(uint8_t n);

void display_off(void);

#endif /* DISPLAY_H_ */
