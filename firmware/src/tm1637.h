/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef TM1637_H_
#define TM1637_H_

#include <stdint.h>

#define TM1637_GRIDS    6

void tm1637_init(void);
void tm1637_write(const uint8_t grids[TM1637_GRIDS], uint8_t brightness);
void tm1637_off(void);

#endif /* TM1637_H_ */
