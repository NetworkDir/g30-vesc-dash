/*
 * Persistent settings in one flash page. Each change appends a single word,
 * the page is only erased when it is full (256 changes on the nRF51).
 *
 * Record: [0xD5][value0][value1][~value0]
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "settings.h"

#include <stddef.h>

#include "fstorage.h"
#include "systime.h"

#define REC_MAGIC       0xD5
#define SAVE_DELAY_MS   3000

static void fs_evt_handler(fs_evt_t const * const evt, fs_ret_t result);

FS_REGISTER_CFG(fs_config_t m_fs_cfg) = {
	.callback  = fs_evt_handler,
	.num_pages = 1,
	.priority  = 0xFE
};

static settings_t m_cur;
static settings_t m_stored;
static bool m_dirty;
static uint32_t m_change_ms;
static volatile bool m_busy;
static uint32_t m_pending_word;
static bool m_store_after_erase;

static uint32_t encode(const settings_t *s) {
	return REC_MAGIC |
			((uint32_t)s->mode << 8) |
			((uint32_t)s->flags << 16) |
			((uint32_t)(uint8_t)~s->mode << 24);
}

static bool decode(uint32_t w, settings_t *s) {
	if ((w & 0xFF) != REC_MAGIC) {
		return false;
	}

	uint8_t mode = (w >> 8) & 0xFF;
	if ((uint8_t)~mode != ((w >> 24) & 0xFF)) {
		return false;
	}

	s->mode = mode;
	s->flags = (w >> 16) & 0xFF;
	return true;
}

static void fs_evt_handler(fs_evt_t const * const evt, fs_ret_t result) {
	if (evt->id == FS_EVT_ERASE && result == FS_SUCCESS && m_store_after_erase) {
		m_store_after_erase = false;
		if (fs_store(&m_fs_cfg, m_fs_cfg.p_start_addr, &m_pending_word, 1, NULL) == FS_SUCCESS) {
			return;
		}
	}
	m_busy = false;
}

void settings_init(const settings_t *defaults) {
	m_cur = *defaults;
	fs_init();

	const uint32_t *p = m_fs_cfg.p_start_addr;
	const uint32_t *end = m_fs_cfg.p_end_addr;
	settings_t s;

	for (;p < end;p++) {
		if (*p == 0xFFFFFFFF) {
			break;
		}
		if (decode(*p, &s)) {
			m_cur = s;
		}
	}

	m_stored = m_cur;
}

const settings_t *settings_get(void) {
	return &m_cur;
}

void settings_set(const settings_t *s) {
	if (s->mode != m_cur.mode || s->flags != m_cur.flags) {
		m_cur = *s;
		m_dirty = true;
		m_change_ms = systime_ms();
	}
}

void settings_task(void) {
	if (!m_dirty || m_busy || systime_since(m_change_ms) < SAVE_DELAY_MS) {
		return;
	}

	m_dirty = false;
	if (m_cur.mode == m_stored.mode && m_cur.flags == m_stored.flags) {
		return;
	}

	m_pending_word = encode(&m_cur);
	m_stored = m_cur;

	const uint32_t *p = m_fs_cfg.p_start_addr;
	const uint32_t *end = m_fs_cfg.p_end_addr;
	while (p < end && *p != 0xFFFFFFFF) {
		p++;
	}

	m_busy = true;
	fs_ret_t res;
	if (p < end) {
		res = fs_store(&m_fs_cfg, p, &m_pending_word, 1, NULL);
	} else {
		m_store_after_erase = true;
		res = fs_erase(&m_fs_cfg, m_fs_cfg.p_start_addr, 1, NULL);
	}

	if (res != FS_SUCCESS) {
		m_busy = false;
		m_store_after_erase = false;
		m_dirty = true; // try again later
		m_change_ms = systime_ms();
	}
}
