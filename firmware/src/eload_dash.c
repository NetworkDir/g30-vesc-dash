/*
 * Dashboard for the m365-eload firmware (Xiaomi M365 ESC used as a 3-phase
 * electronic load), built with `make APP=eload`. Replaces dash.c.
 *
 * - Potentiometer on the throttle input -> current setpoint on the ESC
 * - Display: bus voltage / DC current / both alternating, fault code
 * - Display mode changes with the ESC button (short press)
 * - Bluetooth (Nordic UART): the ESC terminal, status lines and commands
 *
 * Protocol in eload_proto.h, shared with the ESC firmware.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <string.h>
#include "dash.h"
#include "dash_config.h"
#include "display.h"
#include "board_io.h"
#include "vesc_link.h"
#include "settings.h"
#include "eload_proto.h"

#define REQUEST_PERIOD_MS   30
#define REQUEST_RESP_MS     60
#define LINK_LOST_MS        500
#define BOOT_TEST_MS        800
#define COMBINED_SWAP_MS    2000
#define BLE_IN_SIZE         128     // power of two

enum { VIEW_VOLT = 0, VIEW_AMP, VIEW_BOTH, VIEW_COUNT };

static const uint8_t m_font[10] = {
	0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F
};

#define SEG_DASH        0x40
#define G4_TEMP         0x20
#define G4_FAULT        0x40
#define G5_KMH          0x01    // "V" indicator
#define G5_MPH          0x02    // "A" indicator
#define G5_ECO          0x04    // voltage view
#define G5_D            0x08    // current view
#define G5_S            0x10    // combined view
#define G5_LIGHT        0x20    // fan running
#define G5_BLE          0x40
#define G5_LOCK         0x80    // output not armed (turn the pot to zero)

static struct {
	uint32_t now;
	uint32_t boot_ms;
	uint32_t t_req;
	uint32_t t_status;
	bool have_status;
	bool ble;

	uint16_t pot_raw;

	uint8_t view;
	uint8_t last_button;
	uint8_t last_fault;

	// Last status from the ESC
	uint8_t state;
	uint8_t fault;
	uint8_t flags;
	float vbus;
	float idc;
	float iset;
	float imax;

	// Bluetooth -> ESC terminal input
	uint8_t ble_in[BLE_IN_SIZE];
	uint32_t ble_in_head;
	uint32_t ble_in_tail;

	uint8_t beeps_left;
	bool beep_on;
	uint32_t beep_t;
} s;

/* ------------------------------------------------------------------ */

static void beep(uint8_t count) {
	s.beeps_left = count;
	s.beep_on = false;
	s.beep_t = s.now - 80;
}

static void beep_task(void) {
	if (s.beeps_left == 0 || s.now - s.beep_t < 80) {
		return;
	}
	s.beep_t = s.now;
	s.beep_on = !s.beep_on;
	io_set_buzzer(s.beep_on);
	if (!s.beep_on) {
		s.beeps_left--;
	}
}

static uint16_t get16(const uint8_t *p) {
	return (uint16_t)((p[0] << 8) | p[1]);
}

static uint16_t pot_permille(void) {
	int32_t span = POT_RAW_MAX - POT_RAW_MIN;
	int32_t v = ((int32_t)s.pot_raw - POT_RAW_MIN) * 1000 / span;
	if (v < 0) v = 0;
	if (v > 1000) v = 1000;
	return (uint16_t)v;
}

static void send_request(void) {
	uint8_t pl[4 + EP_TEXT_IN_MAX];
	uint16_t pot = pot_permille();
	pl[0] = EP_CMD_REQUEST;
	pl[1] = (uint8_t)(pot >> 8);
	pl[2] = (uint8_t)pot;
	pl[3] = s.ble ? EP_REQ_BLE : 0;
	unsigned int len = 4;
	while (len < sizeof(pl) && s.ble_in_tail != s.ble_in_head) {
		pl[len++] = s.ble_in[s.ble_in_tail & (BLE_IN_SIZE - 1)];
		s.ble_in_tail++;
	}
	(void)link_send(pl, len, LINK_SRC_LOCAL, REQUEST_RESP_MS);
}

/* ------------------------------------------------------------------ */
/* Display                                                             */
/* ------------------------------------------------------------------ */

static void put_number(uint8_t *tens, uint8_t *ones, int32_t n) {
	if (n < 0) n = 0;
	if (n > 99) n = 99;
	*tens = n >= 10 ? m_font[n / 10] : 0;
	*ones = m_font[n % 10];
}

static int32_t round_i(float v) {
	return (int32_t)(v + (v < 0 ? -0.5f : 0.5f));
}

static void show(void) {
	uint8_t g[6] = {0};

	if (s.ble) {
		g[5] |= G5_BLE;
	}

	if (!s.have_status || s.now - s.t_status > LINK_LOST_MS) {
		g[0] = SEG_DASH;
		g[2] = SEG_DASH;
		display_raw(g);
		return;
	}

	bool show_amps;
	switch (s.view) {
	case VIEW_VOLT: show_amps = false; g[5] |= G5_ECO; break;
	case VIEW_AMP:  show_amps = true;  g[5] |= G5_D; break;
	default:        show_amps = ((s.now / COMBINED_SWAP_MS) & 1) != 0; g[5] |= G5_S; break;
	}

	// A fault alternates between the value and the red code, 1 s each
	bool fault_phase = s.fault != 0 && ((s.now / 1000) & 1);
	if (s.fault) {
		g[4] |= G4_FAULT;
	}
	if (fault_phase) {
		put_number(&g[1], &g[3], s.fault);
	} else {
		put_number(&g[0], &g[2], round_i(show_amps ? s.idc : s.vbus));
		g[5] |= show_amps ? G5_MPH : G5_KMH;
	}

	// Bars: current setpoint in steps of 20 % of the maximum
	if (s.imax > 0.0f && s.iset > 0.05f) {
		int32_t bars = (int32_t)(s.iset / s.imax * 5.0f + 0.99f);
		if (bars > 5) bars = 5;
		g[4] |= (uint8_t)((1u << bars) - 1u);
	}

	bool blink_on = (s.now / 500) & 1;
	if ((s.flags & EP_ST_DERATING) && blink_on) {
		g[4] |= G4_TEMP;
	}
	if (s.flags & EP_ST_FAN) {
		g[5] |= G5_LIGHT;
	}
	if (!(s.flags & EP_ST_ARMED)) {
		g[5] |= G5_LOCK;
	}

	display_raw(g);
}

/* ------------------------------------------------------------------ */
/* Link                                                                */
/* ------------------------------------------------------------------ */

bool dash_on_vesc_packet(unsigned char *data, unsigned int len) {
	if (len < EP_STATUS_LEN || data[0] != EP_CMD_STATUS) {
		return true;
	}

	s.state = data[1];
	s.fault = data[2];
	s.flags = data[3];
	s.vbus = get16(&data[5]) * 0.01f;
	s.idc = (int16_t)get16(&data[7]) * 0.01f;
	s.iset = get16(&data[9]) * 0.1f;
	s.imax = get16(&data[11]) * 0.1f;

	// ESC button: next display view
	uint8_t button = data[4];
	if (s.have_status && button != s.last_button) {
		s.view = (uint8_t)((s.view + 1) % VIEW_COUNT);
		settings_t st = *settings_get();
		st.mode = s.view;
		settings_set(&st);
		beep(1);
	}
	s.last_button = button;

	if (s.fault != 0 && s.fault != s.last_fault) {
		beep(3);
	}
	s.last_fault = s.fault;

	s.have_status = true;
	s.t_status = s.now;

	// Terminal output for the phone
	if (len > EP_STATUS_LEN && s.ble) {
		main_ble_write(&data[EP_STATUS_LEN], len - EP_STATUS_LEN);
	}
	return true;
}

void dash_ble_rx(uint8_t b) {
	if (s.ble_in_head - s.ble_in_tail < BLE_IN_SIZE) {
		s.ble_in[s.ble_in_head & (BLE_IN_SIZE - 1)] = b;
		s.ble_in_head++;
	}
}

/* ------------------------------------------------------------------ */
/* Public                                                              */
/* ------------------------------------------------------------------ */

void dash_init(void) {
	memset(&s, 0, sizeof(s));

	settings_t def = {.mode = VIEW_BOTH, .flags = 0};
	settings_init(&def);
	s.view = settings_get()->mode % VIEW_COUNT;
	s.pot_raw = io_read_throttle();

	display_init();
	static const uint8_t all_on[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
	display_raw(all_on);
	beep(1);
}

void dash_set_ble_connected(bool connected) {
	s.ble = connected;
	if (!connected) {
		s.ble_in_tail = s.ble_in_head;
	}
}

void dash_task(uint32_t now) {
	s.now = now;
	if (s.boot_ms == 0) {
		s.boot_ms = now ? now : 1;
	}

	beep_task();
	settings_task();

	if (now - s.t_req >= REQUEST_PERIOD_MS && link_local_queued() == 0) {
		s.t_req = now;
		uint16_t raw = io_read_throttle();
		s.pot_raw = (uint16_t)((s.pot_raw * 3u + raw + 2u) / 4u);
		send_request();
	}

	if (now - s.boot_ms >= BOOT_TEST_MS) {
		show();
	}
}
