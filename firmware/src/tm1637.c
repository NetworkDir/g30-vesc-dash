/*
 * TM1637-compatible LED driver on the G30 dashboard. Same bus sequence as
 * the stock firmware: start, LSB-first bytes with an ACK clock, stop.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "tm1637.h"

#include "nrf_gpio.h"
#include "nrf_delay.h"

#include "board_g30.h"

#define CLK     BOARD_TM1637_CLK_PIN
#define DIO     BOARD_TM1637_DIO_PIN
#define T_US    3

static void dio_out(void) {
	nrf_gpio_cfg_output(DIO);
}

static void dio_in(void) {
	nrf_gpio_cfg_input(DIO, NRF_GPIO_PIN_PULLUP);
}

static void bus_start(void) {
	dio_out();
	nrf_gpio_pin_set(CLK);
	nrf_gpio_pin_set(DIO);
	nrf_delay_us(T_US);
	nrf_gpio_pin_clear(DIO);
	nrf_delay_us(T_US);
	nrf_gpio_pin_clear(CLK);
}

static void bus_stop(void) {
	dio_out();
	nrf_gpio_pin_clear(CLK);
	nrf_gpio_pin_clear(DIO);
	nrf_delay_us(T_US);
	nrf_gpio_pin_set(CLK);
	nrf_delay_us(T_US);
	nrf_gpio_pin_set(DIO);
}

static void bus_write(uint8_t b) {
	dio_out();
	for (int i = 0;i < 8;i++) {
		nrf_gpio_pin_clear(CLK);
		if (b & 1) {
			nrf_gpio_pin_set(DIO);
		} else {
			nrf_gpio_pin_clear(DIO);
		}
		b >>= 1;
		nrf_delay_us(T_US);
		nrf_gpio_pin_set(CLK);
		nrf_delay_us(T_US);
	}

	// ACK clock. The chip pulls DIO low, a missing ACK is not fatal.
	nrf_gpio_pin_clear(CLK);
	dio_in();
	nrf_delay_us(T_US);
	for (int i = 0;i < 50 && nrf_gpio_pin_read(DIO);i++) {
		nrf_delay_us(1);
	}
	nrf_gpio_pin_set(CLK);
	nrf_delay_us(T_US);
	nrf_gpio_pin_clear(CLK);
	dio_out();
}

void tm1637_init(void) {
	nrf_gpio_pin_set(CLK);
	nrf_gpio_pin_set(DIO);
	nrf_gpio_cfg_output(CLK);
	nrf_gpio_cfg_output(DIO);
}

void tm1637_write(const uint8_t grids[TM1637_GRIDS], uint8_t brightness) {
	bus_start();
	bus_write(0x40); // write data, auto increment
	bus_stop();

	bus_start();
	bus_write(0xC0); // address 0
	for (int i = 0;i < TM1637_GRIDS;i++) {
		bus_write(grids[i]);
	}
	bus_stop();

	bus_start();
	bus_write(0x88 | (brightness & 0x07)); // display on
	bus_stop();
}

void tm1637_off(void) {
	bus_start();
	bus_write(0x80);
	bus_stop();
}
