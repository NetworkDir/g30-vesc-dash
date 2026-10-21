/*
 * UART link to the VESC.
 *
 * Frames from BLE (VESC Tool) and from the dashboard share one transmit
 * queue and are sent whole, one after the other. In single wire mode the
 * link works as a master: after each frame it listens until the response
 * has arrived and the line is quiet, or until a per-command timeout.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef VESC_LINK_H_
#define VESC_LINK_H_

#include <stdint.h>
#include <stdbool.h>

typedef enum {
	LINK_SRC_LOCAL = 0,
	LINK_SRC_BLE
} link_src_t;

// Response wait for a frame, in ms. LINK_RESP_NONE: nothing comes back.
#define LINK_RESP_NONE      0

void link_init(void (*on_packet)(unsigned char *data, unsigned int len));

// Queue a payload (without framing). resp_ms is how long to listen for an
// answer in single wire mode. Returns false if the queue is full.
bool link_send(const uint8_t *data, unsigned int len, link_src_t src, uint16_t resp_ms);

// Bytes free in the transmit queue.
unsigned int link_tx_free(void);

// Local frames still waiting in the queue.
unsigned int link_local_queued(void);

// Default response wait for a command from VESC Tool.
uint16_t link_resp_ms_for_cmd(uint8_t cmd);

// Call from the main loop. ble_tx_free: free space for VESC -> BLE data,
// a BLE frame is only started when a full response fits.
void link_task(uint32_t now_ms, unsigned int ble_tx_free);

// Time of the last byte received from the VESC.
uint32_t link_last_rx_ms(void);

#endif /* VESC_LINK_H_ */
