/*
 * Host simulation of the dashboard firmware against a simulated VESC.
 *
 * Runs the real link layer, dashboard logic and display code with mocked
 * hardware. The simulated VESC models the Lisp bridge in single wire mode:
 * responses come a few ms after a request, its receiver is off while it
 * sends and for 1 ms after, and both sides sending at once is a collision.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdio.h>
#include <string.h>
#include <math.h>

#include "nrf.h"
#include "board_g30.h"
#include "dash_config.h"
#include "datatypes.h"
#include "buffer.h"
#include "packet.h"
#include "vesc_link.h"
#include "dash.h"
#include "mock_hw.h"
#include "systime.h"

void UART0_IRQHandler(void);

#define TXD_IDLE        0xFFFFFFFFu
#define BYTES_PER_MS    11
#define VESC_HANDLER    1       // packet.c slot 1 is free on the host

static int m_fail;
static int m_checks;

#define CHECK(cond, ...) do { \
	m_checks++; \
	if (!(cond)) { \
		m_fail++; \
		printf("FAIL %s:%d: ", __FILE__, __LINE__); \
		printf(__VA_ARGS__); \
		printf("\n"); \
	} \
} while (0)

/* ------------------------------------------------------------------ */
/* Simulated VESC                                                      */
/* ------------------------------------------------------------------ */

typedef struct {
	uint32_t due;
	unsigned int len;
	uint8_t data[600];
} sched_t;

static struct {
	bool online;

	// State reported to the dash
	float speed_ms;
	float batt;
	uint8_t fault;
	uint8_t btn_level;
	uint8_t btn_count;

	// What the dash sent
	int chuck_count;
	uint8_t last_js_y;
	int temp_setup_count;
	float last_scale;
	float last_speed_max;
	float last_cur_min_scale;
	float last_duty_max;
	float last_watt_max;

	// Line
	uint8_t out[8192];
	unsigned int out_head;
	unsigned int out_tail;
	sched_t sched[16];
	uint32_t last_tx_ms;
	bool tx_this_ms;

	// Errors
	int collisions;
	int lost_at_dash;
	int lost_at_vesc;
	int ble_fw_responses;
	int ble_mcconf_responses;
	int ble_other;
	uint32_t jitter;    // extra random response latency, ms
} v;

static uint32_t m_rand = 12345;
static uint32_t rnd(uint32_t n) {
	m_rand = m_rand * 1103515245u + 12345u;
	return n ? (m_rand >> 16) % n : 0;
}

static uint32_t now_ms(void) {
	return g_systime_ms;
}

static void vesc_line_put(unsigned char *data, unsigned int len) {
	for (unsigned int i = 0;i < len;i++) {
		v.out[v.out_head++ % sizeof(v.out)] = data[i];
	}
}

static void vesc_schedule(const uint8_t *data, unsigned int len, uint32_t delay) {
	for (int i = 0;i < 16;i++) {
		if (v.sched[i].len == 0) {
			v.sched[i].due = now_ms() + delay;
			v.sched[i].len = len;
			memcpy(v.sched[i].data, data, len);
			return;
		}
	}
	printf("sched full\n");
}

static void vesc_process(unsigned char *data, unsigned int len) {
	if (!v.online) {
		return;
	}

	uint8_t resp[600];
	int32_t ind = 0;
	uint32_t latency = (LINK_FULL_DUPLEX ? 1 : 3) + rnd(v.jitter);

	switch (data[0]) {
	case COMM_FW_VERSION:
		resp[ind++] = COMM_FW_VERSION;
		resp[ind++] = 6;
		resp[ind++] = 6;
		memcpy(resp + ind, "SIM", 4);
		ind += 4;
		vesc_schedule(resp, ind, latency);
		break;

	case COMM_GET_MCCONF:
		resp[ind++] = COMM_GET_MCCONF;
		for (int i = 0;i < 449;i++) {
			resp[ind++] = (uint8_t)i;
		}
		vesc_schedule(resp, ind, latency);
		break;

	case COMM_GET_VALUES_SETUP_SELECTIVE: {
		int32_t i2 = 1;
		uint32_t mask = buffer_get_uint32(data, &i2);
		resp[ind++] = COMM_GET_VALUES_SETUP_SELECTIVE;
		buffer_append_uint32(resp, mask, &ind);
		if (mask & (1u << 0)) buffer_append_float16(resp, 35.0f, 1e1f, &ind);
		if (mask & (1u << 1)) buffer_append_float16(resp, 40.0f, 1e1f, &ind);
		if (mask & (1u << 6)) buffer_append_float32(resp, v.speed_ms, 1e3f, &ind);
		if (mask & (1u << 7)) buffer_append_float16(resp, 50.4f, 1e1f, &ind);
		if (mask & (1u << 8)) buffer_append_float16(resp, v.batt, 1e3f, &ind);
		if (mask & (1u << 16)) resp[ind++] = v.fault;
		vesc_schedule(resp, ind, latency);
	} break;

	case COMM_GET_MCCONF_TEMP:
		resp[ind++] = COMM_GET_MCCONF_TEMP;
		buffer_append_float32_auto(resp, 1.0f, &ind);
		buffer_append_float32_auto(resp, v.last_scale > 0 ? v.last_scale : 1.0f, &ind);
		buffer_append_float32_auto(resp, -100000.0f, &ind);
		buffer_append_float32_auto(resp, 100000.0f, &ind);
		buffer_append_float32_auto(resp, 0.005f, &ind);
		buffer_append_float32_auto(resp, 0.9f, &ind);
		buffer_append_float32_auto(resp, -1500000.0f, &ind);
		buffer_append_float32_auto(resp, 1200.0f, &ind);
		buffer_append_float32_auto(resp, -20.0f, &ind);
		buffer_append_float32_auto(resp, 30.0f, &ind);
		resp[ind++] = 30;
		buffer_append_float32_auto(resp, 1.0f, &ind);
		buffer_append_float32_auto(resp, 0.254f, &ind);
		vesc_schedule(resp, ind, latency);
		break;

	case COMM_SET_MCCONF_TEMP_SETUP: {
		int32_t i2 = 5;
		v.last_cur_min_scale = buffer_get_float32_auto(data, &i2);
		v.last_scale = buffer_get_float32_auto(data, &i2);
		(void)buffer_get_float32_auto(data, &i2);
		v.last_speed_max = buffer_get_float32_auto(data, &i2);
		(void)buffer_get_float32_auto(data, &i2);
		v.last_duty_max = buffer_get_float32_auto(data, &i2);
		(void)buffer_get_float32_auto(data, &i2);
		v.last_watt_max = buffer_get_float32_auto(data, &i2);
		v.temp_setup_count++;
	} break;

	case COMM_SET_CHUCK_DATA:
		v.chuck_count++;
		v.last_js_y = data[2];
		break;

	case COMM_CUSTOM_APP_DATA:
		if (len >= 4 && data[1] == 'G' && data[2] == 'D' && data[3] == 0x01) {
			uint8_t r[6] = {COMM_CUSTOM_APP_DATA, 'G', 'D', 0x81, v.btn_level, v.btn_count};
			// The Lisp event path is slower than the C command handlers
			vesc_schedule(r, sizeof(r), (LINK_FULL_DUPLEX ? 2 : 8) + rnd(v.jitter));
		}
		break;

	default:
		break;
	}
}

/* ------------------------------------------------------------------ */
/* Dash side glue (what main.c does on the target)                    */
/* ------------------------------------------------------------------ */

static void process_packet_vesc(unsigned char *data, unsigned int len) {
	if (dash_on_vesc_packet(data, len)) {
		return;
	}

	if (data[0] == COMM_FW_VERSION) {
		v.ble_fw_responses++;
	} else if (data[0] == COMM_GET_MCCONF && len == 450) {
		v.ble_mcconf_responses++;
	} else {
		v.ble_other++;
	}
}

/* ------------------------------------------------------------------ */
/* Line simulation, 1 ms steps                                        */
/* ------------------------------------------------------------------ */

static bool dash_listening(void) {
#if LINK_FULL_DUPLEX
	return true;
#else
	return mock_uart0.ENABLE == UART_ENABLE_ENABLE_Enabled &&
			mock_uart0.PSELRXD == BOARD_BUS_PIN;
#endif
}

static void step(void) {
	g_systime_ms++;
	uint32_t t = g_systime_ms;
	bool dash_tx = false;

	// Dash -> VESC
	for (int i = 0;i < BYTES_PER_MS;i++) {
		if (mock_uart0.TXD == TXD_IDLE) {
			break;
		}

		uint8_t b = (uint8_t)mock_uart0.TXD;
		mock_uart0.TXD = TXD_IDLE;
		dash_tx = true;

#if !LINK_FULL_DUPLEX
		if (mock_uart0.PSELTXD != BOARD_BUS_PIN) {
			printf("tx while the bus pin is parked\n");
			m_fail++;
		}
#endif

		bool vesc_rx_on = LINK_FULL_DUPLEX || (!v.tx_this_ms && t - v.last_tx_ms > 1);
		if (vesc_rx_on) {
			packet_process_byte(b, VESC_HANDLER);
		} else {
			v.lost_at_vesc++;
		}

		mock_uart0.EVENTS_TXDRDY = 1;
		UART0_IRQHandler();
	}

	// Scheduled VESC responses go on the line when it is free
	v.tx_this_ms = false;
	for (int i = 0;i < 16;i++) {
		if (v.sched[i].len && (int32_t)(t - v.sched[i].due) >= 0) {
			packet_send_packet(v.sched[i].data, v.sched[i].len, VESC_HANDLER);
			v.sched[i].len = 0;
		}
	}

	// VESC -> dash
	for (int i = 0;i < BYTES_PER_MS && v.out_tail != v.out_head;i++) {
		uint8_t b = v.out[v.out_tail++ % sizeof(v.out)];
		v.tx_this_ms = true;
		v.last_tx_ms = t;

		if (!LINK_FULL_DUPLEX && dash_tx) {
			v.collisions++;
			continue;
		}

		if (dash_listening()) {
			mock_uart0.RXD = b;
			mock_uart0.EVENTS_RXDRDY = 1;
			UART0_IRQHandler();
		} else {
			v.lost_at_dash++;
		}
	}

	link_task(t, 1024);
	dash_task(t);
}

static void run(uint32_t ms) {
	for (uint32_t i = 0;i < ms;i++) {
		step();
	}
}

static void ble_request(uint8_t cmd) {
	(void)link_send(&cmd, 1, LINK_SRC_BLE, link_resp_ms_for_cmd(cmd));
}

static void press(uint32_t hold_ms) {
	v.btn_level = 1;
	v.btn_count++;
	run(hold_ms);
	v.btn_level = 0;
}

/* ------------------------------------------------------------------ */

static const uint8_t font[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};

int main(void) {
	memset(&v, 0, sizeof(v));
	v.online = true;
	v.batt = 0.73f;
	mock_uart0.TXD = TXD_IDLE;

	packet_init(vesc_line_put, vesc_process, VESC_HANDLER);
	link_init(process_packet_vesc);
	dash_init();

	printf("== %s link ==\n", LINK_FULL_DUPLEX ? "full duplex" : "single wire");

	// Boot, link up, mode applied
	run(1500);
	CHECK(mock_display_on, "display on after boot");
	CHECK(mock_grids[0] != 0x40, "link should be up, display shows --");
	CHECK(v.temp_setup_count >= 1, "mode not applied");
	CHECK(fabsf(v.last_scale - MODE_D_CURRENT_SCALE) < 0.01f, "D scale %f", v.last_scale);
	CHECK(fabsf(v.last_speed_max - MODE_D_SPEED_KMH / 3.6f) < 0.05f, "D speed %f", v.last_speed_max);
	CHECK(fabsf(v.last_duty_max - 0.9f) < 0.001f, "duty kept from VESC %f", v.last_duty_max);
	CHECK(fabsf(v.last_watt_max - 1200.0f) < 1.0f, "watt kept from VESC %f", v.last_watt_max);
	CHECK(mock_grids[5] & 0x08, "D LED");
	CHECK(mock_grids[5] & 0x01, "km/h LED");
	CHECK((mock_grids[4] & 0x1F) == 0x0F, "battery bars for 73%% (4 of 5) grids4=%02X", mock_grids[4]);

	// Speed on the display
	v.speed_ms = 23.0f / 3.6f;
	run(400);
	CHECK(mock_grids[0] == font[2] && mock_grids[2] == font[3],
			"speed 23 shown as %02X %02X", mock_grids[0], mock_grids[2]);

	// Throttle and brake
	int chuck0 = v.chuck_count;
	mock_thr_raw = 465;
	run(300);
	CHECK(v.last_js_y > 175 && v.last_js_y < 200, "half throttle js_y=%d", v.last_js_y);
	int rate = (v.chuck_count - chuck0) * 1000 / 300;
	CHECK(rate >= 25, "control rate %d Hz", rate);

	mock_brk_raw = 600;
	run(100);
	CHECK(v.last_js_y < 100, "brake wins js_y=%d", v.last_js_y);
	mock_thr_raw = 170;
	mock_brk_raw = 170;
	run(100);
	CHECK(v.last_js_y == 128, "neutral js_y=%d", v.last_js_y);

	// Single press: light
	v.speed_ms = 0.0f;
	press(150);
	run(900);
	CHECK(mock_light, "light on after single press");
	CHECK(mock_grids[5] & 0x20, "light icon");

	// Double press: D -> S
	int ts0 = v.temp_setup_count;
	press(120);
	run(150);
	press(120);
	run(900);
	CHECK(v.temp_setup_count > ts0, "mode change applied");
	CHECK(fabsf(v.last_scale - MODE_S_CURRENT_SCALE) < 0.01f, "S scale %f", v.last_scale);
	CHECK(v.last_speed_max > 80.0f, "S has no speed limit %f", v.last_speed_max);
	CHECK(mock_grids[5] & 0x10, "S LED");
	CHECK(mock_light, "light unchanged by double press");
	CHECK(mock_settings_writes >= 1, "mode saved");

	// Brake + double press: lock
	mock_brk_raw = 600;
	press(120);
	run(150);
	press(120);
	run(900);
	mock_brk_raw = 170;
	run(100);
	CHECK(mock_grids[5] & 0x80, "lock icon");
	CHECK(!(mock_grids[5] & 0x1C), "mode LEDs off when locked");
	mock_thr_raw = 700;
	run(200);
	CHECK(v.last_js_y == 128, "no throttle when locked js_y=%d", v.last_js_y);
	mock_thr_raw = 170;
	mock_brk_raw = 600;
	press(120);
	run(150);
	press(120);
	run(900);
	mock_brk_raw = 170;
	run(100);
	CHECK(!(mock_grids[5] & 0x80), "unlocked");

	// VESC Tool over BLE while riding
	int fw0 = v.ble_fw_responses;
	mock_thr_raw = 465;
	for (int i = 0;i < 40;i++) {
		ble_request(COMM_FW_VERSION);
		run(50);
	}
	run(200);
	CHECK(v.ble_fw_responses - fw0 == 40, "BLE responses %d/40", v.ble_fw_responses - fw0);
	CHECK(v.ble_other == 0, "dash responses leaked to BLE: %d", v.ble_other);
	CHECK(v.last_js_y > 175, "throttle still sent during BLE traffic js_y=%d", v.last_js_y);
	mock_thr_raw = 170;
	run(100);

	// Fault code
	v.fault = 5;
	bool saw_code = false;
	for (int i = 0;i < 30;i++) {
		run(100);
		if ((mock_grids[4] & 0x40) && mock_grids[3] == font[5] && mock_grids[1] == 0) {
			saw_code = true;
		}
	}
	CHECK(saw_code, "fault 5 shown with wrench");
	v.fault = 0;

	// Long press: off, single press: on
	press(2500);
	run(300);
	CHECK(!mock_display_on, "display off after long press");
	CHECK(!mock_light, "light off when off");
	mock_thr_raw = 700;
	run(200);
	CHECK(v.last_js_y == 128, "no throttle when off js_y=%d", v.last_js_y);
	mock_thr_raw = 170;
	press(150);
	run(900);
	CHECK(mock_display_on, "display on again");

	// Throttle held while turning on must not start the motor
	press(2500);
	run(300);
	mock_thr_raw = 700;
	press(150);
	run(900);
	CHECK(v.last_js_y == 128, "throttle needs release after power on js_y=%d", v.last_js_y);
	mock_thr_raw = 170;
	run(200);
	mock_thr_raw = 700;
	run(200);
	CHECK(v.last_js_y > 200, "throttle works after release js_y=%d", v.last_js_y);
	mock_thr_raw = 170;

	// Lever sensor unplugged
	mock_thr_raw = 5;
	run(500);
	CHECK(v.last_js_y == 128, "faulty throttle ignored js_y=%d", v.last_js_y);
	mock_thr_raw = 170;
	run(200);

	// Link loss and recovery (VESC reboot): mode is applied again
	v.online = false;
	run(2000);
	CHECK(mock_grids[0] == 0x40 && mock_grids[2] == 0x40, "-- when the VESC is gone");
	int ts1 = v.temp_setup_count;
	v.online = true;
	run(2500);
	CHECK(mock_grids[0] != 0x40, "link back");
	CHECK(v.temp_setup_count > ts1, "mode re-applied after reconnect");

	// Stress: slow and jittery Lisp, big VESC Tool transfers, while riding
	v.jitter = 22;
	mock_thr_raw = 465;
	int mc0 = v.ble_mcconf_responses;
	int fw1 = v.ble_fw_responses;
	int chuck1 = v.chuck_count;
	for (int i = 0;i < 20;i++) {
		ble_request(COMM_GET_MCCONF);
		ble_request(COMM_FW_VERSION);
		ble_request(COMM_ALIVE);
		run(250);
	}
	run(500);
	CHECK(v.ble_mcconf_responses - mc0 == 20, "big BLE responses %d/20", v.ble_mcconf_responses - mc0);
	CHECK(v.ble_fw_responses - fw1 == 20, "BLE responses under load %d/20", v.ble_fw_responses - fw1);
	rate = (v.chuck_count - chuck1) * 1000 / 5500;
	CHECK(rate >= 20, "control rate under load %d Hz", rate);
	CHECK(mock_grids[0] != 0x40, "link stays up under load");
	mock_thr_raw = 170;
	v.jitter = 0;
	run(300);

	printf("collisions %d, lost at dash %d, lost at vesc %d\n",
			v.collisions, v.lost_at_dash, v.lost_at_vesc);
	CHECK(v.collisions == 0, "line collisions");
	CHECK(v.lost_at_dash == 0, "bytes sent while the dash was not listening");
	CHECK(v.lost_at_vesc == 0, "bytes sent while the VESC receiver was off");

	printf("%d checks, %d failed\n", m_checks, m_fail);
	return m_fail ? 1 : 0;
}
