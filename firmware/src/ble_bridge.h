/*
 * Byte buffers between the Nordic UART service and the VESC link.
 *
 * VESC Tool writes arrive in 20 byte pieces from the SoftDevice event
 * handler, get reassembled into VESC packets and go to the link. Packets
 * from the VESC are framed again and sent back as notifications. No SDK
 * calls in here so the host simulation can run it.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef BLE_BRIDGE_H_
#define BLE_BRIDGE_H_

#include <stdint.h>
#include <stdbool.h>

#define BLE_BRIDGE_CHUNK        20      // NUS payload with the default ATT MTU

typedef enum {
	BLE_SEND_OK = 0,
	BLE_SEND_BUSY,      // no free TX buffers, try again after TX complete
	BLE_SEND_FAIL       // notifications off or link gone, drop the data
} ble_send_res_t;

void ble_bridge_init(void (*on_packet)(unsigned char *data, unsigned int len),
		ble_send_res_t (*send_chunk)(uint8_t *data, uint16_t len));

// NUS data handler, may run in interrupt context.
void ble_bridge_rx(const uint8_t *data, uint16_t len);

// Call from the main loop: handles connect/disconnect, decodes received
// bytes and sends what is queued.
void ble_bridge_task(bool connected);

// Free TX buffer space in bytes, 0 while not connected.
unsigned int ble_bridge_tx_free(void);

// Send a whole packet from the VESC to VESC Tool. Dropped when it does not
// fit, VESC Tool resends.
bool ble_bridge_send_packet(unsigned char *data, unsigned int len);

#endif /* BLE_BRIDGE_H_ */
