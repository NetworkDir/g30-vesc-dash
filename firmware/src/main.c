/*
 * G30 VESC dashboard firmware for the Segway-Ninebot Max G30 BLE board
 * (nRF51822 + S130).
 *
 * - BLE Nordic UART service <-> VESC packets (VESC Tool over BLE)
 * - Levers -> COMM_SET_CHUCK_DATA, telemetry -> LED display
 * - Button actions, riding modes, headlight, buzzer
 *
 * BLE part based on nrf51_vesc by Benjamin Vedder.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdint.h>
#include <string.h>

#include "nordic_common.h"
#include "nrf.h"
#include "ble_hci.h"
#include "ble_advdata.h"
#include "ble_advertising.h"
#include "ble_conn_params.h"
#include "softdevice_handler.h"
#include "app_timer.h"
#include "ble_nus.h"
#include "app_util_platform.h"
#include "fstorage.h"

#include "dash_config.h"
#include "packet.h"
#include "ble_bridge.h"
#include "vesc_link.h"
#include "dash.h"
#include "board_io.h"
#include "systime.h"

// No 32 kHz crystal on the dashboard, the stock firmware uses the
// synthesized LFCLK as well.
#define LFCLK_CONFIG {.source        = NRF_CLOCK_LF_SRC_SYNTH,          \
                      .rc_ctiv       = 0,                               \
                      .rc_temp_ctiv  = 0,                               \
                      .xtal_accuracy = NRF_CLOCK_LF_XTAL_ACCURACY_20_PPM}

#define CENTRAL_LINK_COUNT              0
#define PERIPHERAL_LINK_COUNT           1

#define NUS_SERVICE_UUID_TYPE           BLE_UUID_TYPE_VENDOR_BEGIN
#define APP_FEATURE_NOT_SUPPORTED       BLE_GATT_STATUS_ATTERR_APP_BEGIN + 2

#define APP_ADV_INTERVAL                64      // 40 ms
#define APP_ADV_TIMEOUT_IN_SECONDS      180

#define APP_TIMER_PRESCALER             0
#define APP_TIMER_OP_QUEUE_SIZE         4

#define MIN_CONN_INTERVAL               6       // 7.5 ms
#define MAX_CONN_INTERVAL               25      // 31.25 ms
#define SLAVE_LATENCY                   0
#define CONN_SUP_TIMEOUT                MSEC_TO_UNITS(4000, UNIT_10_MS)
#define FIRST_CONN_PARAMS_UPDATE_DELAY  APP_TIMER_TICKS(5000, APP_TIMER_PRESCALER)
#define NEXT_CONN_PARAMS_UPDATE_DELAY   APP_TIMER_TICKS(30000, APP_TIMER_PRESCALER)
#define MAX_CONN_PARAMS_UPDATE_COUNT    3

#define WDT_TIMEOUT_S                   2

APP_TIMER_DEF(m_tick_timer);

volatile uint32_t g_systime_ms;

static ble_nus_t m_nus;
static volatile uint16_t m_conn_handle = BLE_CONN_HANDLE_INVALID;
static ble_uuid_t m_adv_uuids[] = {{BLE_UUID_NUS_SERVICE, NUS_SERVICE_UUID_TYPE}};

/* ------------------------------------------------------------------ */
/* BLE bridge glue                                                     */
/* ------------------------------------------------------------------ */

static ble_send_res_t nus_send(uint8_t *data, uint16_t len) {
	uint32_t err = ble_nus_string_send(&m_nus, data, len);
	if (err == NRF_SUCCESS) {
		return BLE_SEND_OK;
	} else if (err == BLE_ERROR_NO_TX_PACKETS) {
		return BLE_SEND_BUSY;
	}
	// Notifications not enabled yet or link gone
	return BLE_SEND_FAIL;
}

/* ------------------------------------------------------------------ */
/* Packet routing                                                      */
/* ------------------------------------------------------------------ */

// Complete packet from VESC Tool
static void process_packet_ble(unsigned char *data, unsigned int len) {
	(void)link_send(data, len, LINK_SRC_BLE, link_resp_ms_for_cmd(data[0]));
}

// Complete packet from the VESC
static void process_packet_vesc(unsigned char *data, unsigned int len) {
	if (dash_on_vesc_packet(data, len)) {
		return;
	}

	(void)ble_bridge_send_packet(data, len);
}

/* ------------------------------------------------------------------ */
/* BLE stack                                                           */
/* ------------------------------------------------------------------ */

void assert_nrf_callback(uint16_t line_num, const uint8_t * p_file_name) {
	app_error_handler(0xDEADBEEF, line_num, p_file_name);
}

static void gap_params_init(void) {
	uint32_t err_code;
	ble_gap_conn_params_t gap_conn_params;
	ble_gap_conn_sec_mode_t sec_mode;

	BLE_GAP_CONN_SEC_MODE_SET_OPEN(&sec_mode);

	// Name plus the last two address bytes, e.g. "VESC G30 A1:B2"
	static const char hex[] = "0123456789ABCDEF";
	char name[sizeof(BLE_DEVICE_NAME) + 6];
	uint32_t addr = NRF_FICR->DEVICEADDR[0];
	unsigned int n = sizeof(BLE_DEVICE_NAME) - 1;
	memcpy(name, BLE_DEVICE_NAME, n);
	name[n++] = ' ';
	name[n++] = hex[(addr >> 12) & 0xF];
	name[n++] = hex[(addr >> 8) & 0xF];
	name[n++] = ':';
	name[n++] = hex[(addr >> 4) & 0xF];
	name[n++] = hex[addr & 0xF];
	err_code = sd_ble_gap_device_name_set(&sec_mode, (const uint8_t *)name, n);
	APP_ERROR_CHECK(err_code);

	memset(&gap_conn_params, 0, sizeof(gap_conn_params));
	gap_conn_params.min_conn_interval = MIN_CONN_INTERVAL;
	gap_conn_params.max_conn_interval = MAX_CONN_INTERVAL;
	gap_conn_params.slave_latency     = SLAVE_LATENCY;
	gap_conn_params.conn_sup_timeout  = CONN_SUP_TIMEOUT;

	err_code = sd_ble_gap_ppcp_set(&gap_conn_params);
	APP_ERROR_CHECK(err_code);
}

static void nus_data_handler(ble_nus_t * p_nus, uint8_t * p_data, uint16_t length) {
	(void)p_nus;
	ble_bridge_rx(p_data, length);
}

static void services_init(void) {
	ble_nus_init_t nus_init;
	memset(&nus_init, 0, sizeof(nus_init));
	nus_init.data_handler = nus_data_handler;
	APP_ERROR_CHECK(ble_nus_init(&m_nus, &nus_init));
}

// Many phones keep their own connection interval and turn the update
// request down. That is fine for VESC Tool, so stay connected.
static void on_conn_params_evt(ble_conn_params_evt_t * p_evt) {
	(void)p_evt;
}

static void conn_params_error_handler(uint32_t nrf_error) {
	(void)nrf_error;
}

static void conn_params_init(void) {
	ble_conn_params_init_t cp_init;
	memset(&cp_init, 0, sizeof(cp_init));

	cp_init.p_conn_params                  = NULL;
	cp_init.first_conn_params_update_delay = FIRST_CONN_PARAMS_UPDATE_DELAY;
	cp_init.next_conn_params_update_delay  = NEXT_CONN_PARAMS_UPDATE_DELAY;
	cp_init.max_conn_params_update_count   = MAX_CONN_PARAMS_UPDATE_COUNT;
	cp_init.start_on_notify_cccd_handle    = BLE_GATT_HANDLE_INVALID;
	cp_init.disconnect_on_fail             = false;
	cp_init.evt_handler                    = on_conn_params_evt;
	cp_init.error_handler                  = conn_params_error_handler;

	APP_ERROR_CHECK(ble_conn_params_init(&cp_init));
}

static void on_adv_evt(ble_adv_evt_t ble_adv_evt) {
	if (ble_adv_evt == BLE_ADV_EVT_IDLE) {
		(void)ble_advertising_start(BLE_ADV_MODE_FAST);
	}
}

static void on_ble_evt(ble_evt_t * p_ble_evt) {
	switch (p_ble_evt->header.evt_id) {
	case BLE_GAP_EVT_CONNECTED:
		m_conn_handle = p_ble_evt->evt.gap_evt.conn_handle;
		dash_set_ble_connected(true);
		break;

	case BLE_GAP_EVT_DISCONNECTED:
		m_conn_handle = BLE_CONN_HANDLE_INVALID;
		dash_set_ble_connected(false);
		break;

	case BLE_GAP_EVT_SEC_PARAMS_REQUEST:
		(void)sd_ble_gap_sec_params_reply(m_conn_handle,
				BLE_GAP_SEC_STATUS_PAIRING_NOT_SUPP, NULL, NULL);
		break;

	case BLE_GATTS_EVT_SYS_ATTR_MISSING:
		(void)sd_ble_gatts_sys_attr_set(m_conn_handle, NULL, 0, 0);
		break;

	case BLE_GATTC_EVT_TIMEOUT:
		(void)sd_ble_gap_disconnect(p_ble_evt->evt.gattc_evt.conn_handle,
				BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
		break;

	case BLE_GATTS_EVT_TIMEOUT:
		(void)sd_ble_gap_disconnect(p_ble_evt->evt.gatts_evt.conn_handle,
				BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
		break;

	case BLE_EVT_USER_MEM_REQUEST:
		(void)sd_ble_user_mem_reply(p_ble_evt->evt.gattc_evt.conn_handle, NULL);
		break;

	case BLE_GATTS_EVT_RW_AUTHORIZE_REQUEST: {
		ble_gatts_evt_rw_authorize_request_t req;
		ble_gatts_rw_authorize_reply_params_t auth_reply;

		req = p_ble_evt->evt.gatts_evt.params.authorize_request;

		if (req.type != BLE_GATTS_AUTHORIZE_TYPE_INVALID) {
			if ((req.request.write.op == BLE_GATTS_OP_PREP_WRITE_REQ) ||
					(req.request.write.op == BLE_GATTS_OP_EXEC_WRITE_REQ_NOW) ||
					(req.request.write.op == BLE_GATTS_OP_EXEC_WRITE_REQ_CANCEL)) {
				if (req.type == BLE_GATTS_AUTHORIZE_TYPE_WRITE) {
					auth_reply.type = BLE_GATTS_AUTHORIZE_TYPE_WRITE;
				} else {
					auth_reply.type = BLE_GATTS_AUTHORIZE_TYPE_READ;
				}
				auth_reply.params.write.gatt_status = APP_FEATURE_NOT_SUPPORTED;
				(void)sd_ble_gatts_rw_authorize_reply(p_ble_evt->evt.gatts_evt.conn_handle,
						&auth_reply);
			}
		}
	} break;

	default:
		break;
	}
}

static void ble_evt_dispatch(ble_evt_t * p_ble_evt) {
	ble_conn_params_on_ble_evt(p_ble_evt);
	ble_nus_on_ble_evt(&m_nus, p_ble_evt);
	on_ble_evt(p_ble_evt);
	ble_advertising_on_ble_evt(p_ble_evt);
}

static void sys_evt_dispatch(uint32_t sys_evt) {
	fs_sys_event_handler(sys_evt);
	ble_advertising_on_sys_evt(sys_evt);
}

static void ble_stack_init(void) {
	uint32_t err_code;
	nrf_clock_lf_cfg_t clock_lf_cfg = LFCLK_CONFIG;

	SOFTDEVICE_HANDLER_INIT(&clock_lf_cfg, NULL);

	ble_enable_params_t ble_enable_params;
	err_code = softdevice_enable_get_default_config(CENTRAL_LINK_COUNT,
			PERIPHERAL_LINK_COUNT, &ble_enable_params);
	APP_ERROR_CHECK(err_code);

	CHECK_RAM_START_ADDR(CENTRAL_LINK_COUNT, PERIPHERAL_LINK_COUNT);

	err_code = softdevice_enable(&ble_enable_params);
	APP_ERROR_CHECK(err_code);

	APP_ERROR_CHECK(softdevice_ble_evt_handler_set(ble_evt_dispatch));
	APP_ERROR_CHECK(softdevice_sys_evt_handler_set(sys_evt_dispatch));
}

static void advertising_init(void) {
	ble_advdata_t advdata;
	ble_advdata_t scanrsp;
	ble_adv_modes_config_t options;

	memset(&advdata, 0, sizeof(advdata));
	advdata.name_type          = BLE_ADVDATA_FULL_NAME;
	advdata.include_appearance = false;
	advdata.flags              = BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE;

	memset(&scanrsp, 0, sizeof(scanrsp));
	scanrsp.uuids_complete.uuid_cnt = sizeof(m_adv_uuids) / sizeof(m_adv_uuids[0]);
	scanrsp.uuids_complete.p_uuids  = m_adv_uuids;

	memset(&options, 0, sizeof(options));
	options.ble_adv_fast_enabled  = true;
	options.ble_adv_fast_interval = APP_ADV_INTERVAL;
	options.ble_adv_fast_timeout  = APP_ADV_TIMEOUT_IN_SECONDS;

	APP_ERROR_CHECK(ble_advertising_init(&advdata, &scanrsp, &options, on_adv_evt, NULL));
}

/* ------------------------------------------------------------------ */
/* Time base and watchdog                                              */
/* ------------------------------------------------------------------ */

static void tick_handler(void *p_context) {
	(void)p_context;
	g_systime_ms++;
}

static void wdt_init(void) {
	NRF_WDT->CONFIG = (WDT_CONFIG_HALT_Pause << WDT_CONFIG_HALT_Pos) |
			(WDT_CONFIG_SLEEP_Run << WDT_CONFIG_SLEEP_Pos);
	NRF_WDT->CRV = WDT_TIMEOUT_S * 32768;
	NRF_WDT->RREN = WDT_RREN_RR0_Msk;
	NRF_WDT->TASKS_START = 1;
}

static void wdt_feed(void) {
	NRF_WDT->RR[0] = WDT_RR_RR_Reload;
}

/* ------------------------------------------------------------------ */

int main(void) {
	// Headlight and buzzer off before anything else
	io_init();

	APP_TIMER_INIT(APP_TIMER_PRESCALER, APP_TIMER_OP_QUEUE_SIZE, false);

	ble_stack_init();
	(void)sd_clock_hfclk_request(); // crystal clock for an accurate baudrate

	gap_params_init();
	services_init();
	advertising_init();
	conn_params_init();

	ble_bridge_init(process_packet_ble, nus_send);
	link_init(process_packet_vesc);
	dash_init();

	APP_ERROR_CHECK(app_timer_create(&m_tick_timer, APP_TIMER_MODE_REPEATED, tick_handler));
	APP_ERROR_CHECK(app_timer_start(m_tick_timer, APP_TIMER_TICKS(1, APP_TIMER_PRESCALER), NULL));

	APP_ERROR_CHECK(ble_advertising_start(BLE_ADV_MODE_FAST));
	wdt_init();

	uint32_t packet_tick = systime_ms();

	for (;;) {
		uint32_t now = systime_ms();

		// Packet decoder timeouts, 1 ms steps
		while (packet_tick != now) {
			packet_tick++;
			packet_timerfunc();
		}

		ble_bridge_task(m_conn_handle != BLE_CONN_HANDLE_INVALID);
		link_task(now, ble_bridge_tx_free());
		dash_task(now);
		ble_bridge_task(m_conn_handle != BLE_CONN_HANDLE_INVALID);
		wdt_feed();

		(void)sd_app_evt_wait();
	}
}
