/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "board_io.h"

#include "nrf.h"
#include "nrf_gpio.h"
#include "nrf_delay.h"

#include "board_g30.h"
#include "dash_config.h"

static bool m_is_g30;

void io_init(void) {
	nrf_gpio_pin_clear(BOARD_HEADLIGHT_PIN);
	nrf_gpio_cfg_output(BOARD_HEADLIGHT_PIN);
	nrf_gpio_pin_clear(BOARD_BUZZER_PIN);
	nrf_gpio_cfg_output(BOARD_BUZZER_PIN);

	nrf_gpio_cfg_input(BOARD_VARIANT_PIN, NRF_GPIO_PIN_PULLUP);
	nrf_delay_us(50);
	m_is_g30 = nrf_gpio_pin_read(BOARD_VARIANT_PIN) == 0;
	nrf_gpio_cfg_default(BOARD_VARIANT_PIN);

#if BUTTON_LOCAL
	nrf_gpio_cfg_input(BUTTON_LOCAL_PIN, NRF_GPIO_PIN_PULLUP);
#endif

	NRF_ADC->ENABLE = ADC_ENABLE_ENABLE_Enabled;
}

bool io_is_g30(void) {
	return m_is_g30;
}

static uint16_t adc_read(uint32_t psel) {
	NRF_ADC->CONFIG =
			(ADC_CONFIG_RES_10bit << ADC_CONFIG_RES_Pos) |
			(ADC_CONFIG_INPSEL_AnalogInputOneThirdPrescaling << ADC_CONFIG_INPSEL_Pos) |
			(ADC_CONFIG_REFSEL_SupplyOneThirdPrescaling << ADC_CONFIG_REFSEL_Pos) |
			(psel << ADC_CONFIG_PSEL_Pos) |
			(ADC_CONFIG_EXTREFSEL_None << ADC_CONFIG_EXTREFSEL_Pos);

	NRF_ADC->EVENTS_END = 0;
	NRF_ADC->TASKS_START = 1;

	// A 10 bit conversion takes 68 us
	for (int i = 0;i < 200 && !NRF_ADC->EVENTS_END;i++) {
		nrf_delay_us(1);
	}

	NRF_ADC->EVENTS_END = 0;
	return (uint16_t)NRF_ADC->RESULT;
}

uint16_t io_read_throttle(void) {
	return adc_read(BOARD_THROTTLE_AIN);
}

uint16_t io_read_brake(void) {
	return adc_read(BOARD_BRAKE_AIN);
}

void io_set_light(bool on) {
	if (on) {
		nrf_gpio_pin_set(BOARD_HEADLIGHT_PIN);
	} else {
		nrf_gpio_pin_clear(BOARD_HEADLIGHT_PIN);
	}
}

void io_set_buzzer(bool on) {
	if (on) {
		nrf_gpio_pin_set(BOARD_BUZZER_PIN);
	} else {
		nrf_gpio_pin_clear(BOARD_BUZZER_PIN);
	}
}

bool io_button_pressed(void) {
#if BUTTON_LOCAL
	return nrf_gpio_pin_read(BUTTON_LOCAL_PIN) == 0;
#else
	return false;
#endif
}
