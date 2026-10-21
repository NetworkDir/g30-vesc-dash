/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "dash.h"

#include <string.h>
#include <math.h>

#include "dash_config.h"
#include "datatypes.h"
#include "buffer.h"
#include "vesc_link.h"
#include "board_io.h"
#include "display.h"
#include "settings.h"

// COMM_GET_VALUES_SETUP_SELECTIVE fields: temp FET, temp motor, speed,
// input voltage, battery level, fault
#define VALUES_MASK         ((1u << 0) | (1u << 1) | (1u << 6) | (1u << 7) | (1u << 8) | (1u << 16))
#define VALUES_LEN          (1 + 4 + 2 + 2 + 4 + 2 + 2 + 1)

// Custom app data exchanged with the Lisp script
#define APP_MAGIC0          'G'
#define APP_MAGIC1          'D'
#define APP_BTN_POLL        0x01
#define APP_BTN_STATE       0x81

#define LINK_TIMEOUT_MS     1500
#define BOOT_TIME_MS        700
#define CAL_START_MS        150
#define LEVER_PERIOD_MS     10
#define DISPLAY_PERIOD_MS   100
#define BTN_POLL_MS         100
#define BASE_RETRY_MS       1000
#define BASE_MAX_TRIES      5
#define MODE_REFRESH_MS     10000

// Dashboard fault codes, shown when the VESC reports none (Ninebot numbers)
#define DASH_FAULT_THROTTLE 14
#define DASH_FAULT_BRAKE    15

#define MODE_COUNT          3

typedef enum {
	PWR_BOOT = 0,
	PWR_ON,
	PWR_OFF,
	PWR_TEST
} pwr_t;

typedef enum {
	BTN_SINGLE = 0,
	BTN_DOUBLE,
	BTN_LONG
} btn_event_t;

typedef struct {
	uint16_t raw;       // filtered
	uint16_t idle;
	uint16_t full;
	float value;        // 0..1
	bool fault;
	uint32_t bad_since;
	uint32_t cal_sum;
} lever_t;

static const float m_mode_scale[MODE_COUNT] = {
	MODE_ECO_CURRENT_SCALE, MODE_D_CURRENT_SCALE, MODE_S_CURRENT_SCALE
};

static const uint16_t m_mode_kmh[MODE_COUNT] = {
	MODE_ECO_SPEED_KMH, MODE_D_SPEED_KMH, MODE_S_SPEED_KMH
};

static struct {
	pwr_t pwr;
	bool started;
	uint32_t boot_ms;
	uint32_t cal_n;

	lever_t thr;
	lever_t brk;
	bool thr_armed;

	// Telemetry
	bool have_values;
	bool values_pending;
	uint32_t values_req_ms;
	uint32_t values_rx_ms;
	float speed_kmh;
	float temp_fet;
	float temp_motor;
	float batt;
	float batt_disp;
	uint8_t fault;
	bool link_ok;

	// Limits read from the VESC, kept when a mode is applied
	bool have_base;
	bool base_pending;
	uint8_t base_tries;
	uint32_t base_req_ms;
	float cur_min_scale;
	float duty_min;
	float duty_max;
	float watt_min;
	float watt_max;
	float erpm_per_ms;  // 0: unknown

	uint8_t mode;
	bool mode_dirty;
	uint32_t mode_sent_ms;
	bool light;
	bool locked;
	bool ble;

	// Button
	bool btn_holding;
	bool btn_long_fired;
	uint8_t btn_clicks;
	uint32_t btn_press_ms;
	uint32_t btn_release_ms;
	uint16_t btn_double_ms;
	bool btn_have_remote;
	uint8_t btn_remote_cnt;
	bool btn_local_level;
	uint8_t btn_local_stable;

	// Buzzer
	uint8_t beeps_left;
	uint16_t beep_len;
	bool beep_on;
	uint32_t beep_t;

	uint32_t last_activity_ms;
	uint32_t t_lever;
	uint32_t t_ctrl;
	uint32_t t_values;
	uint32_t t_btn;
	uint32_t t_disp;

	uint8_t test_step;
	uint32_t test_t;
	uint32_t now;
} s;

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static void beep(uint8_t count, uint16_t len_ms) {
	s.beeps_left = count;
	s.beep_len = len_ms;
	s.beep_on = false;
	s.beep_t = s.now - len_ms;
}

static void beep_task(void) {
	if (s.beeps_left == 0) {
		return;
	}

	if (s.beep_on) {
		if (s.now - s.beep_t >= s.beep_len) {
			io_set_buzzer(false);
			s.beep_on = false;
			s.beep_t = s.now;
			s.beeps_left--;
		}
	} else if (s.now - s.beep_t >= s.beep_len) {
		io_set_buzzer(true);
		s.beep_on = true;
		s.beep_t = s.now;
	}
}

static bool send_local(const uint8_t *data, unsigned int len, uint16_t resp_ms) {
	return link_send(data, len, LINK_SRC_LOCAL, resp_ms);
}

static bool can_send(void) {
	return link_local_queued() < 2;
}

static float speed_abs_kmh(void) {
	return fabsf(s.speed_kmh);
}

/* ------------------------------------------------------------------ */
/* Levers                                                              */
/* ------------------------------------------------------------------ */

static void lever_update(lever_t *l, uint16_t raw) {
	// Light IIR filter
	l->raw = (uint16_t)((l->raw + raw + 1) / 2);

	if (l->raw < LEVER_FAULT_LOW || l->raw > LEVER_FAULT_HIGH) {
		if (!l->fault && s.now - l->bad_since > 100) {
			l->fault = true;
			beep(3, 60);
		}
		l->value = 0.0f;
		return;
	}

	l->bad_since = s.now;
	l->fault = false;

	if (l->raw > l->full) {
		l->full = l->raw;
	}

	float range = (float)l->full - (float)l->idle;
	if (range < 100.0f) {
		range = 100.0f;
	}

	float x = ((float)l->raw - (float)l->idle) / range;
	if (x <= LEVER_DEADBAND) {
		x = 0.0f;
	} else {
		x = (x - LEVER_DEADBAND) / (1.0f - LEVER_DEADBAND);
	}

	if (x > 1.0f) {
		x = 1.0f;
	}

	l->value = x;
}

static void lever_calibrate(lever_t *l) {
	uint16_t avg = s.cal_n ? (uint16_t)(l->cal_sum / s.cal_n) : 0;
	if (avg >= LEVER_FAULT_LOW && avg <= LEVER_IDLE_MAX) {
		l->idle = avg;
	} else {
		l->idle = LEVER_IDLE_DEFAULT;
	}
	l->full = LEVER_FULL_DEFAULT;
	if (l->full < l->idle + 200) {
		l->full = l->idle + 200;
	}
}

/* ------------------------------------------------------------------ */
/* Modes                                                               */
/* ------------------------------------------------------------------ */

static void apply_mode(void) {
	uint8_t buf[48];
	int32_t ind = 0;

	float speed_ms;
	if (m_mode_kmh[s.mode] > 0) {
		speed_ms = (float)m_mode_kmh[s.mode] / 3.6f;
	} else if (s.erpm_per_ms > 0.0f) {
		speed_ms = 100000.0f / s.erpm_per_ms; // VESC default ERPM limit
	} else {
		speed_ms = 100.0f;
	}

	buf[ind++] = COMM_SET_MCCONF_TEMP_SETUP;
	buf[ind++] = 0; // store
	buf[ind++] = MODE_FORWARD_CAN;
	buf[ind++] = 0; // ack
	buf[ind++] = 0; // divide by controllers
	buffer_append_float32_auto(buf, s.cur_min_scale, &ind);
	buffer_append_float32_auto(buf, m_mode_scale[s.mode], &ind);
	buffer_append_float32_auto(buf, -speed_ms, &ind);
	buffer_append_float32_auto(buf, speed_ms, &ind);
	buffer_append_float32_auto(buf, s.duty_min, &ind);
	buffer_append_float32_auto(buf, s.duty_max, &ind);
	buffer_append_float32_auto(buf, s.watt_min, &ind);
	buffer_append_float32_auto(buf, s.watt_max, &ind);

	if (send_local(buf, ind, LINK_RESP_NONE)) {
		s.mode_dirty = false;
		s.mode_sent_ms = s.now;
	}
}

static void base_defaults(void) {
	s.cur_min_scale = 1.0f;
	s.duty_min = 0.005f;
	s.duty_max = 0.95f;
	s.watt_min = -1500000.0f;
	s.watt_max = 1500000.0f;
	s.erpm_per_ms = 0.0f;
}

static void parse_base(unsigned char *data, unsigned int len) {
	// COMM_GET_MCCONF_TEMP: 10 floats, poles, gear ratio, wheel diameter
	if (len < 1 + 10 * 4 + 1 + 2 * 4) {
		return;
	}

	int32_t ind = 1;
	s.cur_min_scale = buffer_get_float32_auto(data, &ind);
	(void)buffer_get_float32_auto(data, &ind); // current max scale
	(void)buffer_get_float32_auto(data, &ind); // min erpm
	(void)buffer_get_float32_auto(data, &ind); // max erpm
	s.duty_min = buffer_get_float32_auto(data, &ind);
	s.duty_max = buffer_get_float32_auto(data, &ind);
	s.watt_min = buffer_get_float32_auto(data, &ind);
	s.watt_max = buffer_get_float32_auto(data, &ind);
	(void)buffer_get_float32_auto(data, &ind); // battery current min
	(void)buffer_get_float32_auto(data, &ind); // battery current max
	uint8_t poles = data[ind++];
	float gear = buffer_get_float32_auto(data, &ind);
	float wheel = buffer_get_float32_auto(data, &ind);

	if (wheel > 0.01f && gear > 0.0f && poles > 0) {
		s.erpm_per_ms = ((float)poles / 2.0f) * 60.0f * gear / (wheel * (float)M_PI);
	}

	if (s.cur_min_scale <= 0.0f || s.cur_min_scale > 1.0f) {
		s.cur_min_scale = 1.0f;
	}

	s.have_base = true;
	s.mode_dirty = true;
}

static void set_mode(uint8_t mode) {
	s.mode = mode % MODE_COUNT;
	s.mode_dirty = true;
	settings_t st = *settings_get();
	st.mode = s.mode;
	settings_set(&st);
}

/* ------------------------------------------------------------------ */
/* Power                                                               */
/* ------------------------------------------------------------------ */

static void power_on(void) {
	s.pwr = PWR_ON;
	s.thr_armed = false;
	s.last_activity_ms = s.now;
	beep(1, 120);
}

static void power_off(void) {
	s.pwr = PWR_OFF;
	s.locked = false;
	s.light = false;
	beep(1, 400);
}

/* ------------------------------------------------------------------ */
/* Button                                                              */
/* ------------------------------------------------------------------ */

static void on_button(btn_event_t ev) {
	s.last_activity_ms = s.now;

	switch (s.pwr) {
	case PWR_OFF:
		if (ev == BTN_SINGLE || ev == BTN_LONG) {
			power_on();
		}
		break;

	case PWR_ON:
		if (ev == BTN_SINGLE) {
			if (!s.locked) {
				s.light = !s.light;
				beep(1, 40);
			}
		} else if (ev == BTN_DOUBLE) {
			if (s.brk.value > 0.3f) {
				s.locked = !s.locked;
				beep(s.locked ? 2 : 1, 150);
			} else if (!s.locked) {
				set_mode(s.mode + 1);
				beep(s.mode + 1, 60);
			}
		} else if (ev == BTN_LONG) {
			if (speed_abs_kmh() < 3.0f) {
				power_off();
			}
		}
		break;

	default:
		break;
	}
}

static void btn_press(void) {
	s.btn_holding = true;
	s.btn_press_ms = s.now;
	s.btn_clicks++;
}

static void btn_release(void) {
	if (!s.btn_holding) {
		return;
	}
	s.btn_holding = false;
	s.btn_release_ms = s.now;
	if (s.btn_long_fired) {
		s.btn_long_fired = false;
		s.btn_clicks = 0;
	}
}

static void btn_task(void) {
	if (s.btn_holding && !s.btn_long_fired &&
			s.now - s.btn_press_ms >= BUTTON_LONG_PRESS_MS) {
		s.btn_long_fired = true;
		s.btn_clicks = 0;
		on_button(BTN_LONG);
	}

	if (!s.btn_holding && s.btn_clicks > 0 &&
			s.now - s.btn_release_ms >= s.btn_double_ms) {
		btn_event_t ev = s.btn_clicks >= 2 ? BTN_DOUBLE : BTN_SINGLE;
		s.btn_clicks = 0;
		on_button(ev);
	}
}

static void btn_remote_update(bool level, uint8_t cnt) {
	if (!s.btn_have_remote) {
		s.btn_have_remote = true;
		s.btn_remote_cnt = cnt;
		return;
	}

	uint8_t presses = (uint8_t)(cnt - s.btn_remote_cnt);
	s.btn_remote_cnt = cnt;
	if (presses > 4) {
		presses = 4;
	}

	for (uint8_t i = 0;i < presses;i++) {
		btn_press();
		if (i + 1 < presses || !level) {
			btn_release();
		}
	}

	if (presses == 0) {
		if (!level) {
			btn_release();
		} else if (!s.btn_holding) {
			btn_press();
		}
	}
}

static void btn_local_sample(void) {
	bool level = io_button_pressed();
	if (level == s.btn_local_level) {
		s.btn_local_stable = 0;
		return;
	}

	// 3 equal samples (30 ms) to accept a change
	if (++s.btn_local_stable >= 3) {
		s.btn_local_stable = 0;
		s.btn_local_level = level;
		if (level) {
			btn_press();
		} else {
			btn_release();
		}
	}
}

/* ------------------------------------------------------------------ */
/* VESC packets                                                        */
/* ------------------------------------------------------------------ */

bool dash_on_vesc_packet(unsigned char *data, unsigned int len) {
	if (len < 1) {
		return false;
	}

	switch (data[0]) {
	case COMM_GET_VALUES_SETUP_SELECTIVE: {
		if (len < VALUES_LEN || !s.values_pending) {
			return false;
		}

		int32_t ind = 1;
		if (buffer_get_uint32(data, &ind) != VALUES_MASK) {
			return false;
		}

		s.temp_fet = buffer_get_float16(data, 1e1f, &ind);
		s.temp_motor = buffer_get_float16(data, 1e1f, &ind);
		s.speed_kmh = buffer_get_float32(data, 1e3f, &ind) * 3.6f;
		(void)buffer_get_float16(data, 1e1f, &ind); // input voltage
		s.batt = buffer_get_float16(data, 1e3f, &ind);
		s.fault = data[ind++];

		if (s.batt < 0.0f) {
			s.batt = 0.0f;
		} else if (s.batt > 1.0f) {
			s.batt = 1.0f;
		}

		if (!s.have_values) {
			s.batt_disp = s.batt;
		}

		s.have_values = true;
		s.values_pending = false;
		s.values_rx_ms = s.now;
		return true;
	}

	case COMM_GET_MCCONF_TEMP:
		if (!s.base_pending) {
			return false;
		}
		s.base_pending = false;
		parse_base(data, len);
		return true;

	case COMM_CUSTOM_APP_DATA:
		if (len >= 4 && data[1] == APP_MAGIC0 && data[2] == APP_MAGIC1) {
			if (data[3] == APP_BTN_STATE && len >= 6) {
				btn_remote_update(data[4] != 0, data[5]);
			}
			return true;
		}
		return false;

	default:
		return false;
	}
}

/* ------------------------------------------------------------------ */
/* Periodic work                                                       */
/* ------------------------------------------------------------------ */

static void request_values(void) {
	uint8_t buf[5];
	int32_t ind = 0;
	buf[ind++] = COMM_GET_VALUES_SETUP_SELECTIVE;
	buffer_append_uint32(buf, VALUES_MASK, &ind);
	if (send_local(buf, ind, 60)) {
		s.values_pending = true;
		s.values_req_ms = s.now;
	}
}

static void request_base(void) {
	uint8_t cmd = COMM_GET_MCCONF_TEMP;
	if (send_local(&cmd, 1, 80)) {
		s.base_pending = true;
		s.base_req_ms = s.now;
		s.base_tries++;
	}
}

static void poll_button(void) {
	uint8_t buf[4] = {COMM_CUSTOM_APP_DATA, APP_MAGIC0, APP_MAGIC1, APP_BTN_POLL};
	(void)send_local(buf, sizeof(buf), 80);
}

static void send_control(void) {
	float brk = s.brk.fault ? 0.0f : s.brk.value;
	float thr = s.thr.fault ? 0.0f : s.thr.value;

	if (thr <= 0.0f) {
		s.thr_armed = true;
	}

	if (s.pwr != PWR_ON || s.locked || !s.thr_armed) {
		thr = 0.0f;
	}

	if (s.locked && speed_abs_kmh() > 3.0f) {
		if (brk < 0.5f) {
			brk = 0.5f;
		}
		if (s.beeps_left == 0) {
			beep(1, 100);
		}
	}

	int js = 128;
	if (brk > 0.0f) {
		js = 128 - (int)(brk * 128.0f + 0.5f);
	} else if (thr > 0.0f) {
		js = 128 + (int)(thr * 127.0f + 0.5f);
	}

	if (js < 0) {
		js = 0;
	} else if (js > 255) {
		js = 255;
	}

	uint8_t buf[13];
	int32_t ind = 0;
	buf[ind++] = COMM_SET_CHUCK_DATA;
	buf[ind++] = 128;          // js_x
	buf[ind++] = (uint8_t)js;  // js_y
	buf[ind++] = 0;            // bt_c
	buf[ind++] = 0;            // bt_z
	buffer_append_int16(buf, 0, &ind);
	buffer_append_int16(buf, 0, &ind);
	buffer_append_int16(buf, 0, &ind);
	buf[ind++] = 1;            // reverse state is explicit
	buf[ind++] = 0;            // not reversed
	(void)send_local(buf, ind, LINK_RESP_NONE);

	if (thr > 0.0f || brk > 0.0f || speed_abs_kmh() > 1.0f) {
		s.last_activity_ms = s.now;
	}
}

static void update_battery_display(void) {
	// Hide voltage sag: fall slowly, rise only when standing still
	if (s.batt < s.batt_disp) {
		float d = s.batt_disp - s.batt;
		s.batt_disp -= d < 0.0005f ? d : 0.0005f;
	} else if (speed_abs_kmh() < 1.0f) {
		float d = s.batt - s.batt_disp;
		s.batt_disp += d < 0.001f ? d : 0.001f;
	}
}

static void show_display(void) {
	display_view_t v;
	memset(&v, 0, sizeof(v));

	float speed = speed_abs_kmh();
	if (DISPLAY_MPH) {
		speed /= 1.609344f;
	}

	v.link_ok = s.link_ok;
	v.batt_pct = (uint8_t)(s.batt_disp * 100.0f + 0.5f);
	v.number = (uint8_t)(speed > 99.0f ? 99 : (int)(speed + 0.5f));
	if (DISPLAY_BATT_WHEN_STOPPED && speed < 1.0f) {
		v.number = v.batt_pct > 99 ? 99 : v.batt_pct;
	}
	v.mode = (disp_mode_t)s.mode;
	v.locked = s.locked;
	v.light = s.light;
	v.ble = s.ble;
	v.mph = DISPLAY_MPH;
	v.temp_warn = s.temp_fet > TEMP_WARN_MOS_C || s.temp_motor > TEMP_WARN_MOTOR_C;

	if (s.fault) {
		v.fault = s.fault;
	} else if (s.thr.fault) {
		v.fault = DASH_FAULT_THROTTLE;
	} else if (s.brk.fault) {
		v.fault = DASH_FAULT_BRAKE;
	}

	display_show(&v, s.now);
}

// Segment test: shows "grid bit" as two digits, then lights only that
// segment. Entered by holding throttle and brake while powering up.
static void test_task(void) {
	if (s.now - s.test_t < 1500) {
		uint8_t grid = s.test_step / 8;
		uint8_t bit = s.test_step % 8;
		if (s.now - s.test_t < 600) {
			display_number(grid * 10 + bit);
		} else {
			uint8_t g[6] = {0};
			g[grid] = (uint8_t)(1u << bit);
			display_raw(g);
		}
		return;
	}

	s.test_t = s.now;
	s.test_step = (s.test_step + 1) % 48;
}

static void boot_task(void) {
	if (s.now - s.boot_ms < BOOT_TIME_MS) {
		return;
	}

	uint16_t thr_avg = s.cal_n ? s.thr.cal_sum / s.cal_n : 0;
	uint16_t brk_avg = s.cal_n ? s.brk.cal_sum / s.cal_n : 0;

	lever_calibrate(&s.thr);
	lever_calibrate(&s.brk);

	if (thr_avg > LEVER_IDLE_MAX && thr_avg < LEVER_FAULT_HIGH &&
			brk_avg > LEVER_IDLE_MAX && brk_avg < LEVER_FAULT_HIGH) {
		s.pwr = PWR_TEST;
		s.test_t = s.now;
		s.test_step = 0;
		beep(2, 80);
		return;
	}

	if (START_OFF) {
		s.pwr = PWR_OFF;
	} else {
		power_on();
	}
}

/* ------------------------------------------------------------------ */
/* Public                                                              */
/* ------------------------------------------------------------------ */

void dash_init(void) {
	memset(&s, 0, sizeof(s));

	settings_t def = {.mode = MODE_DEFAULT, .flags = 0};
	settings_init(&def);
	s.mode = settings_get()->mode % MODE_COUNT;

	base_defaults();
	s.btn_double_ms = BUTTON_LOCAL ? BUTTON_DOUBLE_PRESS_MS : BUTTON_DOUBLE_PRESS_MS + BTN_POLL_MS + 50;
	s.thr.raw = io_read_throttle();
	s.brk.raw = io_read_brake();

	display_init();
	static const uint8_t all_on[6] = {0xFF, 0x00, 0xFF, 0x00, 0x7F, 0xFF};
	display_raw(all_on);
	beep(1, 100);
}

void dash_set_ble_connected(bool connected) {
	s.ble = connected;
}

void dash_task(uint32_t now) {
	s.now = now;
	if (!s.started) {
		s.started = true;
		s.boot_ms = now;
	}

	beep_task();
	settings_task();

	if (now - s.t_lever >= LEVER_PERIOD_MS) {
		s.t_lever = now;
		uint16_t t = io_read_throttle();
		uint16_t b = io_read_brake();
		if (s.pwr == PWR_BOOT) {
			s.thr.raw = (uint16_t)((s.thr.raw + t + 1) / 2);
			s.brk.raw = (uint16_t)((s.brk.raw + b + 1) / 2);
			if (now - s.boot_ms >= CAL_START_MS) {
				s.thr.cal_sum += s.thr.raw;
				s.brk.cal_sum += s.brk.raw;
				s.cal_n++;
			}
		} else {
			lever_update(&s.thr, t);
			lever_update(&s.brk, b);
		}

		if (BUTTON_LOCAL) {
			btn_local_sample();
		}
	}

	if (s.pwr == PWR_BOOT) {
		boot_task();
		return;
	}

	btn_task();

	// Link supervision
	bool link_ok = s.have_values && (now - s.values_rx_ms) < LINK_TIMEOUT_MS;
	if (link_ok && !s.link_ok) {
		// (Re)connected, the VESC may have rebooted and lost the mode
		s.have_base = false;
		s.base_tries = 0;
		s.mode_dirty = true;
	}
	s.link_ok = link_ok;

	if (s.values_pending && now - s.values_req_ms > 500) {
		s.values_pending = false;
	}
	if (s.base_pending && now - s.base_req_ms > 500) {
		s.base_pending = false;
	}

	// Only keep a couple of local frames in the queue so that throttle
	// commands never go out stale.
	if (can_send() && now - s.t_ctrl >= CONTROL_PERIOD_MS && s.pwr != PWR_TEST) {
		s.t_ctrl = now;
		send_control();
	}

	if (can_send() && now - s.t_values >= TELEMETRY_PERIOD_MS) {
		s.t_values = now;
		request_values();
	}

	if (!BUTTON_LOCAL && can_send() && now - s.t_btn >= BTN_POLL_MS) {
		s.t_btn = now;
		poll_button();
	}

	if (s.link_ok && can_send()) {
		if (!s.have_base) {
			if (s.base_tries >= BASE_MAX_TRIES) {
				// Old firmware without COMM_GET_MCCONF_TEMP
				base_defaults();
				s.have_base = true;
				s.mode_dirty = true;
			} else if (!s.base_pending && now - s.base_req_ms >= BASE_RETRY_MS) {
				request_base();
			}
		} else if (s.mode_dirty ||
				(speed_abs_kmh() < 1.0f && now - s.mode_sent_ms >= MODE_REFRESH_MS)) {
			apply_mode();
		}
	}

	if (AUTO_OFF_MIN > 0 && s.pwr == PWR_ON &&
			now - s.last_activity_ms > (uint32_t)AUTO_OFF_MIN * 60000u) {
		power_off();
	}

	io_set_light(s.light && s.pwr == PWR_ON);

	if (now - s.t_disp >= DISPLAY_PERIOD_MS) {
		s.t_disp = now;
		update_battery_display();

		switch (s.pwr) {
		case PWR_ON:
			show_display();
			break;
		case PWR_TEST:
			test_task();
			break;
		default:
			display_off();
			break;
		}
	}
}
