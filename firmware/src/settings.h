/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef SETTINGS_H_
#define SETTINGS_H_

#include <stdint.h>
#include <stdbool.h>

#define SETTINGS_FLAG_MPH   0x01

typedef struct {
	uint8_t mode;
	uint8_t flags;
} settings_t;

// Call after the SoftDevice is enabled.
void settings_init(const settings_t *defaults);
const settings_t *settings_get(void);
// Saved a few seconds after the last change.
void settings_set(const settings_t *s);
void settings_task(void);

#endif /* SETTINGS_H_ */
