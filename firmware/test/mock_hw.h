/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef MOCK_HW_H_
#define MOCK_HW_H_

#include <stdint.h>
#include <stdbool.h>

extern uint16_t mock_thr_raw;
extern uint16_t mock_brk_raw;
extern bool mock_light;
extern bool mock_buzzer;
extern int mock_buzzer_edges;
extern uint8_t mock_grids[6];
extern bool mock_display_on;
extern int mock_settings_writes;

#endif /* MOCK_HW_H_ */
