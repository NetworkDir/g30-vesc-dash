/*
 * Host mocks for the hardware facing modules: levers, buzzer, light,
 * TM1637 output and flash settings.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <string.h>

#include "nrf.h"
#include "board_io.h"
#include "tm1637.h"
#include "settings.h"
#include "mock_hw.h"

NRF_UART_Type mock_uart0;
volatile uint32_t g_systime_ms;

uint16_t mock_thr_raw = 170;
uint16_t mock_brk_raw = 170;
bool mock_light;
bool mock_buzzer;
int mock_buzzer_edges;
uint8_t mock_grids[TM1637_GRIDS];
bool mock_display_on;

void io_init(void) {}
bool io_is_g30(void) { return true; }
uint16_t io_read_throttle(void) { return mock_thr_raw; }
uint16_t io_read_brake(void) { return mock_brk_raw; }
void io_set_light(bool on) { mock_light = on; }

void io_set_buzzer(bool on) {
	if (on && !mock_buzzer) {
		mock_buzzer_edges++;
	}
	mock_buzzer = on;
}

bool io_button_pressed(void) { return false; }

void tm1637_init(void) {}

void tm1637_write(const uint8_t grids[TM1637_GRIDS], uint8_t brightness) {
	(void)brightness;
	memcpy(mock_grids, grids, TM1637_GRIDS);
	mock_display_on = true;
}

void tm1637_off(void) {
	mock_display_on = false;
}

static settings_t m_settings;
int mock_settings_writes;

void settings_init(const settings_t *defaults) { m_settings = *defaults; }
const settings_t *settings_get(void) { return &m_settings; }

void settings_set(const settings_t *s) {
	m_settings = *s;
	mock_settings_writes++;
}

void settings_task(void) {}
