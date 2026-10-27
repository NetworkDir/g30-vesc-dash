/*
 * Dashboard logic: levers -> VESC, telemetry -> display, button actions,
 * riding modes, lock and soft power.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DASH_H_
#define DASH_H_

#include <stdint.h>
#include <stdbool.h>

// Call after the SoftDevice is enabled (settings live in flash).
void dash_init(void);

// Call from the main loop as often as possible.
void dash_task(uint32_t now_ms);

// Every packet from the VESC goes through here first. Returns true if it
// answered a dashboard request and must not be forwarded to BLE.
bool dash_on_vesc_packet(unsigned char *data, unsigned int len);

void dash_set_ble_connected(bool connected);

#endif /* DASH_H_ */
