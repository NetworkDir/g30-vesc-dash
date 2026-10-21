/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "display.h"

#include <string.h>

#include "tm1637.h"
#include "dash_config.h"

// Same digit font as the stock firmware
static const uint8_t m_font[10] = {
	0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F
};

#define SEG_DASH        0x40

#define G4_TEMP         0x20
#define G4_FAULT        0x40
#define G4_BATT_EMPTY   0x80

#define G5_KMH          0x01
#define G5_MPH          0x02
#define G5_ECO          0x04
#define G5_D            0x08
#define G5_S            0x10
#define G5_LIGHT        0x20
#define G5_BLE          0x40
#define G5_LOCK         0x80

static bool m_is_off = true;

void display_init(void) {
	tm1637_init();
	m_is_off = true;
}

void display_raw(const uint8_t grids[6]) {
	tm1637_write(grids, DISPLAY_BRIGHTNESS);
	m_is_off = false;
}

void display_off(void) {
	if (!m_is_off) {
		tm1637_off();
		m_is_off = true;
	}
}

static void put_number(uint8_t *tens, uint8_t *ones, uint8_t n) {
	if (n > 99) {
		n = 99;
	}
	*tens = n >= 10 ? m_font[n / 10] : 0;
	*ones = m_font[n % 10];
}

void display_number(uint8_t n) {
	uint8_t g[6] = {0};
	put_number(&g[0], &g[2], n);
	display_raw(g);
}

// Battery bars like the stock firmware: one bar per 20 %, the top bar
// blinks while it is in its lower half, the empty LED blinks below 10 %.
static uint8_t battery_bits(uint8_t pct, bool blink_on) {
	uint8_t level = pct == 0 ? 0 : (pct + 9) / 10; // 1..10
	if (level > 10) {
		level = 10;
	}

	if (level == 0) {
		return 0;
	}

	if (level == 1) {
		return blink_on ? G4_BATT_EMPTY : 0;
	}

	uint8_t full = level / 2;
	uint8_t bits = (uint8_t)((1u << full) - 1u);
	if ((level & 1) && blink_on) {
		bits |= (uint8_t)(1u << full);
	}
	return bits;
}

void display_show(const display_view_t *v, uint32_t now_ms) {
	uint8_t g[6] = {0};
	bool blink_on = (now_ms / 500) & 1;
	// Faults alternate between the speed and the code, 1 s each
	bool fault_phase = v->fault != 0 && ((now_ms / 1000) & 1);

	if (v->ble) {
		g[5] |= G5_BLE;
	}

	if (!v->link_ok) {
		g[0] = SEG_DASH;
		g[2] = SEG_DASH;
		display_raw(g);
		return;
	}

	if (fault_phase) {
		put_number(&g[1], &g[3], v->fault);
		g[4] |= G4_FAULT;
	} else {
		put_number(&g[0], &g[2], v->number);
		g[5] |= v->mph ? G5_MPH : G5_KMH;
	}

	g[4] |= battery_bits(v->batt_pct, blink_on);

	if (v->temp_warn && blink_on) {
		g[4] |= G4_TEMP;
	}

	if (v->locked) {
		g[5] |= G5_LOCK;
	} else {
		switch (v->mode) {
		case DISP_MODE_ECO: g[5] |= G5_ECO; break;
		case DISP_MODE_D: g[5] |= G5_D; break;
		case DISP_MODE_S: g[5] |= G5_S; break;
		}
	}

	if (v->light) {
		g[5] |= G5_LIGHT;
	}

	display_raw(g);
}
