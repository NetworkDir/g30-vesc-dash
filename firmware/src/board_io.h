/*
 * Levers, headlight, buzzer and the optional local button.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef BOARD_IO_H_
#define BOARD_IO_H_

#include <stdint.h>
#include <stdbool.h>

void io_init(void);

// True on the G30 board variant (strap on P0.08 reads low).
bool io_is_g30(void);

// 10 bit raw lever readings, ratiometric to VDD.
uint16_t io_read_throttle(void);
uint16_t io_read_brake(void);

void io_set_light(bool on);
void io_set_buzzer(bool on);

// Local button (BUTTON_LOCAL builds only), true while pressed.
bool io_button_pressed(void);

#endif /* BOARD_IO_H_ */
