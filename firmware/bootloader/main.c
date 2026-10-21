/*
 * Secure DFU bootloader entry for the G30 dashboard.
 *
 * Replaces the stock SDK example main.c: it does not touch the pca10028
 * LED/button pins (which are wired to other things on the G30), and it
 * enters DFU mode when both levers are held fully pressed at power-up.
 * That gesture needs no button and no wire, so it works on every board.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "nrf.h"
#include "nrf_bootloader.h"
#include "nrf_bootloader_app_start.h"
#include "nrf_bootloader_info.h"
#include "nrf_dfu.h"
#include "nrf_mbr.h"
#include "boards.h"

// Lever ADC inputs, same as the application (AIN1 = P0.27 throttle,
// AIN0 = P0.26 brake). "Fully pressed" is well above the ~760 full scale
// idle/full span used by the app.
#define DFU_LEVER_THRESHOLD     600

static uint16_t adc_read(uint32_t psel) {
	NRF_ADC->ENABLE = ADC_ENABLE_ENABLE_Enabled;
	NRF_ADC->CONFIG =
			(ADC_CONFIG_RES_10bit << ADC_CONFIG_RES_Pos) |
			(ADC_CONFIG_INPSEL_AnalogInputOneThirdPrescaling << ADC_CONFIG_INPSEL_Pos) |
			(ADC_CONFIG_REFSEL_SupplyOneThirdPrescaling << ADC_CONFIG_REFSEL_Pos) |
			(psel << ADC_CONFIG_PSEL_Pos) |
			(ADC_CONFIG_EXTREFSEL_None << ADC_CONFIG_EXTREFSEL_Pos);

	NRF_ADC->EVENTS_END = 0;
	NRF_ADC->TASKS_START = 1;
	while (!NRF_ADC->EVENTS_END) {
		// 10 bit conversion, ~68 us
	}
	NRF_ADC->EVENTS_END = 0;
	uint16_t r = (uint16_t)NRF_ADC->RESULT;
	NRF_ADC->ENABLE = ADC_ENABLE_ENABLE_Disabled;
	return r;
}

// Strong override of the weak default (which reads BOOTLOADER_BUTTON).
bool nrf_dfu_enter_check(void) {
	uint16_t thr = adc_read(ADC_CONFIG_PSEL_AnalogInput1);
	uint16_t brk = adc_read(ADC_CONFIG_PSEL_AnalogInput0);
	return thr > DFU_LEVER_THRESHOLD && brk > DFU_LEVER_THRESHOLD;
}

void app_error_fault_handler(uint32_t id, uint32_t pc, uint32_t info) {
	(void)id; (void)pc; (void)info;
	NVIC_SystemReset();
}

void app_error_handler_bare(uint32_t error_code) {
	(void)error_code;
	NVIC_SystemReset();
}

int main(void) {
	uint32_t ret_val = nrf_bootloader_init();
	APP_ERROR_CHECK(ret_val);

	// No DFU requested and a valid app is present: start it.
	nrf_bootloader_app_start(MAIN_APPLICATION_START_ADDR);

	for (;;) {
		// Never reached.
	}
}
