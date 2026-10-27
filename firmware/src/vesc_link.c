/*
 * UART link to the VESC, see vesc_link.h.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "vesc_link.h"

#include <string.h>

#include "nrf.h"
#include "nrf_gpio.h"
#include "nrf_delay.h"
#include "app_util_platform.h"

#include "board_g30.h"
#include "dash_config.h"
#include "crc.h"
#include "datatypes.h"
#include "packet.h"
#include "systime.h"

#define TXQ_SIZE            1024    // power of two
#define RXQ_SIZE            256     // power of two
#define TXQ_HDR_LEN         5       // len16, src, resp16

// Single wire timing
#define QUIET_MS            6       // line idle time that ends a response
#define GAP_NORESP_MS       3       // pause after a frame without response
#define BLE_RESERVE         600     // BLE buffer space needed for a response

typedef enum {
	ST_IDLE = 0,
	ST_TX,
	ST_WAIT
} link_state_t;

static uint8_t m_txq[TXQ_SIZE];
static volatile uint32_t m_txq_head;    // written by main
static volatile uint32_t m_txq_tail;    // written by main (header) and ISR (bytes)

static uint8_t m_rxq[RXQ_SIZE];
static volatile uint32_t m_rxq_head;    // written by ISR
static volatile uint32_t m_rxq_tail;    // written by main

static volatile uint32_t m_tx_remaining;
static volatile bool m_tx_active;
static volatile bool m_tx_done;
static volatile uint32_t m_tx_done_ms;
static volatile uint32_t m_last_rx_ms;
static volatile uint32_t m_rx_bytes;

static link_state_t m_state = ST_IDLE;
static uint32_t m_next_tx_ms;
static uint32_t m_tx_start_ms;
static uint32_t m_tx_len;
static uint32_t m_wait_start_ms;
static uint32_t m_rx_bytes_at_wait;
static uint32_t m_packets;
static uint32_t m_packets_at_wait;
static uint16_t m_cur_resp_ms;
static unsigned int m_local_queued;

static void (*m_on_packet)(unsigned char *data, unsigned int len);

#define PACKET_VESC         0

/* ------------------------------------------------------------------ */
/* UART pin handling                                                  */
/* ------------------------------------------------------------------ */

#if !LINK_FULL_DUPLEX
// Listen on the bus pin, TX parked. Same pin setup as the stock firmware.
static void bus_dir_rx(void) {
	NRF_UART0->TASKS_STOPTX = 1;
	NRF_UART0->ENABLE = UART_ENABLE_ENABLE_Disabled;

	nrf_gpio_pin_set(BOARD_BUS_PARK_PIN);
	nrf_gpio_cfg_output(BOARD_BUS_PARK_PIN);
	nrf_gpio_cfg_input(BOARD_BUS_PIN, LINK_LISTEN_PULL);

	NRF_UART0->PSELTXD = BOARD_BUS_PARK_PIN;
	NRF_UART0->PSELRXD = BOARD_BUS_PIN;

	NRF_UART0->EVENTS_RXDRDY = 0;
	NRF_UART0->EVENTS_ERROR = 0;
	NRF_UART0->ERRORSRC = NRF_UART0->ERRORSRC;
	NRF_UART0->ENABLE = UART_ENABLE_ENABLE_Enabled;
	NRF_UART0->TASKS_STARTRX = 1;
}

// Drive the bus pin, receiver off.
static void bus_dir_tx(void) {
	NRF_UART0->TASKS_STOPRX = 1;
	NRF_UART0->ENABLE = UART_ENABLE_ENABLE_Disabled;

	nrf_gpio_pin_set(BOARD_BUS_PIN);
	nrf_gpio_cfg_output(BOARD_BUS_PIN);
	nrf_gpio_cfg_input(BOARD_BUS_PARK_PIN, NRF_GPIO_PIN_PULLDOWN);

	NRF_UART0->PSELTXD = BOARD_BUS_PIN;
	NRF_UART0->PSELRXD = BOARD_BUS_PARK_PIN;

	NRF_UART0->EVENTS_TXDRDY = 0;
	NRF_UART0->ENABLE = UART_ENABLE_ENABLE_Enabled;
	NRF_UART0->TASKS_STARTTX = 1;
}
#endif

static void uart_hw_init(void) {
	NRF_UART0->ENABLE = UART_ENABLE_ENABLE_Disabled;
	NRF_UART0->BAUDRATE = LINK_BAUDRATE << UART_BAUDRATE_BAUDRATE_Pos;
	NRF_UART0->CONFIG = 0; // 8N1, no flow control
	NRF_UART0->PSELRTS = 0xFFFFFFFF;
	NRF_UART0->PSELCTS = 0xFFFFFFFF;

	NRF_UART0->INTENCLR = 0xFFFFFFFF;
	NRF_UART0->INTENSET = UART_INTENSET_RXDRDY_Msk |
			UART_INTENSET_TXDRDY_Msk | UART_INTENSET_ERROR_Msk;

	NVIC_ClearPendingIRQ(UART0_IRQn);
	NVIC_SetPriority(UART0_IRQn, APP_IRQ_PRIORITY_HIGH);
	NVIC_EnableIRQ(UART0_IRQn);

#if LINK_FULL_DUPLEX
	nrf_gpio_pin_set(BOARD_BUS_PIN);
	nrf_gpio_cfg_output(BOARD_BUS_PIN);
	nrf_gpio_cfg_input(LINK_FD_RX_PIN, NRF_GPIO_PIN_PULLUP);
	NRF_UART0->PSELTXD = BOARD_BUS_PIN;
	NRF_UART0->PSELRXD = LINK_FD_RX_PIN;
	NRF_UART0->ENABLE = UART_ENABLE_ENABLE_Enabled;
	NRF_UART0->TASKS_STARTRX = 1;
	NRF_UART0->TASKS_STARTTX = 1;
#else
	bus_dir_rx();
#endif
}

void UART0_IRQHandler(void) {
	if (NRF_UART0->EVENTS_RXDRDY) {
		NRF_UART0->EVENTS_RXDRDY = 0;
		uint8_t b = (uint8_t)NRF_UART0->RXD;
		uint32_t head = m_rxq_head;
		if ((head - m_rxq_tail) < RXQ_SIZE) {
			m_rxq[head & (RXQ_SIZE - 1)] = b;
			m_rxq_head = head + 1;
		}
		m_last_rx_ms = systime_ms();
		m_rx_bytes++;
	}

	if (NRF_UART0->EVENTS_ERROR) {
		NRF_UART0->EVENTS_ERROR = 0;
		NRF_UART0->ERRORSRC = NRF_UART0->ERRORSRC;
	}

	if (NRF_UART0->EVENTS_TXDRDY) {
		NRF_UART0->EVENTS_TXDRDY = 0;

		if (m_tx_remaining > 0) {
			uint32_t tail = m_txq_tail;
			NRF_UART0->TXD = m_txq[tail & (TXQ_SIZE - 1)];
			m_txq_tail = tail + 1;
			m_tx_remaining--;
		} else if (m_tx_active) {
			m_tx_active = false;
#if !LINK_FULL_DUPLEX
			// Make sure the last byte including its stop bit is out
			// (one byte is 87 us), then release the bus right away.
			nrf_delay_us(100);
			bus_dir_rx();
#endif
			m_tx_done_ms = systime_ms();
			m_tx_done = true;
		}
	}
}

/* ------------------------------------------------------------------ */
/* Queue                                                               */
/* ------------------------------------------------------------------ */

static inline uint32_t txq_used(void) {
	return m_txq_head - m_txq_tail;
}

unsigned int link_tx_free(void) {
	return TXQ_SIZE - txq_used();
}

unsigned int link_local_queued(void) {
	return m_local_queued;
}

static inline void txq_put(uint32_t *head, uint8_t b) {
	m_txq[*head & (TXQ_SIZE - 1)] = b;
	(*head)++;
}

static inline uint8_t txq_peek(uint32_t ofs) {
	return m_txq[(m_txq_tail + ofs) & (TXQ_SIZE - 1)];
}

bool link_send(const uint8_t *data, unsigned int len, link_src_t src, uint16_t resp_ms) {
	if (len == 0 || len > PACKET_MAX_PL_LEN) {
		return false;
	}

	unsigned int frame_len = len + (len <= 255 ? 5 : 6);
	if (link_tx_free() < frame_len + TXQ_HDR_LEN) {
		return false;
	}

	uint32_t head = m_txq_head;
	txq_put(&head, frame_len & 0xFF);
	txq_put(&head, frame_len >> 8);
	txq_put(&head, (uint8_t)src);
	txq_put(&head, resp_ms & 0xFF);
	txq_put(&head, resp_ms >> 8);

	if (len <= 255) {
		txq_put(&head, 2);
		txq_put(&head, len);
	} else {
		txq_put(&head, 3);
		txq_put(&head, len >> 8);
		txq_put(&head, len & 0xFF);
	}

	for (unsigned int i = 0;i < len;i++) {
		txq_put(&head, data[i]);
	}

	unsigned short crc = crc16((unsigned char*)data, len);
	txq_put(&head, crc >> 8);
	txq_put(&head, crc & 0xFF);
	txq_put(&head, 3);

	// Publish the whole record at once
	m_txq_head = head;
	if (src == LINK_SRC_LOCAL) {
		m_local_queued++;
	}
	return true;
}

/* ------------------------------------------------------------------ */
/* Response timing for VESC Tool commands (single wire mode)          */
/* ------------------------------------------------------------------ */

uint16_t link_resp_ms_for_cmd(uint8_t cmd) {
	switch (cmd) {
	// No response
	case COMM_JUMP_TO_BOOTLOADER:
	case COMM_SET_DUTY:
	case COMM_SET_CURRENT:
	case COMM_SET_CURRENT_BRAKE:
	case COMM_SET_RPM:
	case COMM_SET_POS:
	case COMM_SET_HANDBRAKE:
	case COMM_SET_DETECT:
	case COMM_SET_SERVO_POS:
	case COMM_REBOOT:
	case COMM_ALIVE:
	case COMM_SET_CHUCK_DATA:
	case COMM_JUMP_TO_BOOTLOADER_ALL_CAN:
	case COMM_SET_CURRENT_REL:
	case COMM_JUMP_TO_BOOTLOADER_HW:
	case COMM_JUMP_TO_BOOTLOADER_ALL_CAN_HW:
		return LINK_RESP_NONE;

	// Flash erase
	case COMM_ERASE_NEW_APP:
	case COMM_ERASE_NEW_APP_ALL_CAN:
	case COMM_ERASE_BOOTLOADER:
	case COMM_ERASE_BOOTLOADER_ALL_CAN:
	case COMM_ERASE_NEW_APP_HW:
	case COMM_ERASE_NEW_APP_ALL_CAN_HW:
	case COMM_ERASE_BOOTLOADER_HW:
	case COMM_ERASE_BOOTLOADER_ALL_CAN_HW:
		return 20000;

	// Motor detection
	case COMM_DETECT_MOTOR_PARAM:
	case COMM_DETECT_MOTOR_R_L:
	case COMM_DETECT_MOTOR_FLUX_LINKAGE:
	case COMM_DETECT_ENCODER:
	case COMM_DETECT_HALL_FOC:
	case COMM_DETECT_MOTOR_FLUX_LINKAGE_OPENLOOP:
	case COMM_DETECT_APPLY_ALL_FOC:
		return 60000;

	// Configuration and code writes
	case COMM_SET_MCCONF:
	case COMM_SET_APPCONF:
	case COMM_SET_APPCONF_NO_STORE:
	case COMM_SET_CUSTOM_CONFIG:
	case COMM_LISP_ERASE_CODE:
	case COMM_QMLUI_ERASE:
		return 5000;

	case COMM_WRITE_NEW_APP_DATA:
	case COMM_WRITE_NEW_APP_DATA_ALL_CAN:
	case COMM_WRITE_NEW_APP_DATA_LZO:
	case COMM_WRITE_NEW_APP_DATA_ALL_CAN_LZO:
	case COMM_WRITE_NEW_APP_DATA_HW:
	case COMM_WRITE_NEW_APP_DATA_ALL_CAN_HW:
	case COMM_LISP_WRITE_CODE:
	case COMM_QMLUI_WRITE:
	case COMM_TERMINAL_CMD:
	case COMM_TERMINAL_CMD_SYNC:
		return 1000;

	default:
		return 300;
	}
}

/* ------------------------------------------------------------------ */
/* Main loop                                                           */
/* ------------------------------------------------------------------ */

static void process_packet(unsigned char *data, unsigned int len) {
	m_packets++;
	if (m_on_packet) {
		m_on_packet(data, len);
	}
}

void link_init(void (*on_packet)(unsigned char *data, unsigned int len)) {
	m_on_packet = on_packet;
	packet_init(0, process_packet, PACKET_VESC);
	uart_hw_init();
}

uint32_t link_last_rx_ms(void) {
	return m_last_rx_ms;
}

static void start_frame(uint32_t now) {
	uint32_t len = txq_peek(0) | ((uint32_t)txq_peek(1) << 8);
	if ((link_src_t)txq_peek(2) == LINK_SRC_LOCAL && m_local_queued > 0) {
		m_local_queued--;
	}
	m_cur_resp_ms = txq_peek(3) | ((uint16_t)txq_peek(4) << 8);
	m_txq_tail += TXQ_HDR_LEN;

#if !LINK_FULL_DUPLEX
	bus_dir_tx();
#endif

	m_tx_len = len;
	m_tx_start_ms = now;
	m_tx_done = false;
	m_tx_remaining = len - 1;
	m_tx_active = true;

	uint32_t tail = m_txq_tail;
	uint8_t first = m_txq[tail & (TXQ_SIZE - 1)];
	m_txq_tail = tail + 1;
	NRF_UART0->TXD = first;
	m_state = ST_TX;
}

static void abort_tx(void) {
	CRITICAL_REGION_ENTER();
	// Drop what is left of the frame
	m_txq_tail += m_tx_remaining;
	m_tx_remaining = 0;
	m_tx_active = false;
	m_tx_done = false;
	CRITICAL_REGION_EXIT();
	uart_hw_init();
	m_state = ST_IDLE;
}

void link_task(uint32_t now, unsigned int ble_tx_free) {
	while (m_rxq_tail != m_rxq_head) {
		uint32_t tail = m_rxq_tail;
		uint8_t b = m_rxq[tail & (RXQ_SIZE - 1)];
		m_rxq_tail = tail + 1;
		packet_process_byte(b, PACKET_VESC);
	}

	switch (m_state) {
	case ST_IDLE: {
		if ((int32_t)(now - m_next_tx_ms) < 0 || txq_used() == 0) {
			break;
		}

#if !LINK_FULL_DUPLEX
		link_src_t src = (link_src_t)txq_peek(2);
		if (src == LINK_SRC_BLE && ble_tx_free < BLE_RESERVE) {
			break;
		}
#else
		(void)ble_tx_free;
#endif
		start_frame(now);
	} break;

	case ST_TX:
		if (m_tx_done) {
			m_tx_done = false;
#if LINK_FULL_DUPLEX
			m_state = ST_IDLE;
			m_next_tx_ms = now;
#else
			m_wait_start_ms = m_tx_done_ms;
			m_rx_bytes_at_wait = m_rx_bytes;
			m_packets_at_wait = m_packets;
			m_state = ST_WAIT;
#endif
		} else if ((now - m_tx_start_ms) > (m_tx_len / 8 + 50)) {
			// ~11.5 bytes/ms at 115200, this should never trigger
			abort_tx();
		}
		break;

	case ST_WAIT: {
		bool rx_activity = m_rx_bytes != m_rx_bytes_at_wait;
		bool got_packet = m_packets != m_packets_at_wait;
		uint32_t quiet = now - (rx_activity ? m_last_rx_ms : m_wait_start_ms);
		uint32_t waited = now - m_wait_start_ms;
		bool done;

		if (m_cur_resp_ms == LINK_RESP_NONE) {
			done = quiet >= GAP_NORESP_MS;
		} else {
			done = quiet >= QUIET_MS && (got_packet || waited >= m_cur_resp_ms);
		}

		if (done) {
			m_state = ST_IDLE;
			m_next_tx_ms = now;
		}
	} break;
	}
}
