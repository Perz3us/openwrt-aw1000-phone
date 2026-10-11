/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2026 perz3us */
#ifndef LINE_H
#define LINE_H

#include "clcc.h"

enum line_tone { TONE_OFF, TONE_DIAL, TONE_BUSY, TONE_RINGBACK, TONE_WAITING };

enum line_state {
	LS_IDLE,
	LS_DIALTONE,
	LS_DIGITS,
	LS_CALLING,
	LS_TALKING,
	LS_RINGING,
	LS_BUSY,
};

struct line_ops {
	void (*tone)(void *ctx, enum line_tone t);
	void (*ring)(void *ctx, int on);
	void (*audio)(void *ctx, int on);
	void (*dial)(void *ctx, const char *number);
	void (*answer)(void *ctx);
	void (*hangup)(void *ctx);
	void (*chld)(void *ctx, const char *arg);
	void (*log)(void *ctx, const char *what, const char *number);
	const char *(*speed)(void *ctx, const char *code);
	int (*blocked)(void *ctx, const char *number);
	void *ctx;
};

#define LINE_DIGITS_MAX 24

struct line {
	enum line_state state;
	const struct line_ops *ops;
	char digits[LINE_DIGITS_MAX];
	unsigned n;
	char peer[CLCC_NUM_MAX];
	char held_peer[CLCC_NUM_MAX];
	int offhook;
	int onhook_pending;
	int seen_call;
	int ringback, ringback_on;
	int recall, waiting_on, ncalls, active_id, held, blocked_id;
	unsigned long t_state, t_digit, t_onhook, t_hangup;
	unsigned dialtone_ms, digit_ms, flash_min_ms, flash_ms, busy_ms, setup_ms;
};

void line_init(struct line *l, const struct line_ops *ops);
void line_hook(struct line *l, int offhook, unsigned long now);
void line_digit(struct line *l, char d, unsigned long now);
void line_modem(struct line *l, const struct clcc_call *calls, int n, unsigned long now);
void line_tick(struct line *l, unsigned long now);
const char *line_state_name(enum line_state s);

#endif
