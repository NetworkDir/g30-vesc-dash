/*
 * Millisecond time base, driven by a 1 kHz app_timer in main.c.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef SYSTIME_H_
#define SYSTIME_H_

#include <stdint.h>

extern volatile uint32_t g_systime_ms;

static inline uint32_t systime_ms(void) {
	return g_systime_ms;
}

static inline uint32_t systime_since(uint32_t t) {
	return g_systime_ms - t;
}

#endif /* SYSTIME_H_ */
