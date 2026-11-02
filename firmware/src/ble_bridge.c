/*
 * BLE byte buffers, see ble_bridge.h.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "ble_bridge.h"

#include "packet.h"

#define PACKET_BLE          1

#define RX_SIZE             512     // power of two
#define TX_SIZE             1024    // power of two
#define FRAME_OVERHEAD      6       // start, len16, crc16, stop

// BLE -> main (written in the SoftDevice event handler)
static uint8_t m_rx[RX_SIZE];
static volatile uint32_t m_rx_head;
static volatile uint32_t m_rx_tail;

// main -> BLE
static uint8_t m_tx[TX_SIZE];
static uint32_t m_tx_head;
static uint32_t m_tx_tail;

static bool m_connected;
static ble_send_res_t (*m_send_chunk)(uint8_t *data, uint16_t len);

static void tx_write(unsigned char *data, unsigned int len) {
	if (!m_connected || ble_bridge_tx_free() < len) {
		return;
	}

	for (unsigned int i = 0;i < len;i++) {
		m_tx[m_tx_head & (TX_SIZE - 1)] = data[i];
		m_tx_head++;
	}
}

static void tx_drain(void) {
	while (m_connected && m_tx_head != m_tx_tail) {
		uint8_t chunk[BLE_BRIDGE_CHUNK];
		uint32_t n = m_tx_head - m_tx_tail;
		if (n > BLE_BRIDGE_CHUNK) {
			n = BLE_BRIDGE_CHUNK;
		}

		for (uint32_t i = 0;i < n;i++) {
			chunk[i] = m_tx[(m_tx_tail + i) & (TX_SIZE - 1)];
		}

		ble_send_res_t res = m_send_chunk(chunk, (uint16_t)n);
		if (res == BLE_SEND_OK) {
			m_tx_tail += n;
		} else if (res == BLE_SEND_BUSY) {
			// Continue on the next TX complete event
			break;
		} else {
			m_tx_tail = m_tx_head;
			break;
		}
	}
}

void ble_bridge_init(void (*on_packet)(unsigned char *data, unsigned int len),
		ble_send_res_t (*send_chunk)(uint8_t *data, uint16_t len)) {
	m_send_chunk = send_chunk;
	m_connected = false;
	m_rx_head = m_rx_tail = 0;
	m_tx_head = m_tx_tail = 0;
	packet_init(tx_write, on_packet, PACKET_BLE);
}

void ble_bridge_rx(const uint8_t *data, uint16_t len) {
	for (uint16_t i = 0;i < len;i++) {
		uint32_t head = m_rx_head;
		if ((head - m_rx_tail) >= RX_SIZE) {
			break;
		}
		m_rx[head & (RX_SIZE - 1)] = data[i];
		m_rx_head = head + 1;
	}
}

void ble_bridge_task(bool connected) {
	if (connected != m_connected) {
		// Nothing from an old connection goes out on a new one
		m_connected = connected;
		m_tx_tail = m_tx_head;
		if (!connected) {
			m_rx_tail = m_rx_head;
		}
		packet_reset(PACKET_BLE);
	}

	while (m_rx_tail != m_rx_head) {
		uint8_t b = m_rx[m_rx_tail & (RX_SIZE - 1)];
		m_rx_tail++;
		packet_process_byte(b, PACKET_BLE);
	}

	tx_drain();
}

unsigned int ble_bridge_tx_free(void) {
	if (!m_connected) {
		return 0;
	}
	return TX_SIZE - (m_tx_head - m_tx_tail);
}

bool ble_bridge_send_packet(unsigned char *data, unsigned int len) {
	if (ble_bridge_tx_free() < len + FRAME_OVERHEAD) {
		return false;
	}
	packet_send_packet(data, len, PACKET_BLE);
	return true;
}
