/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2026 perz3us */
#ifndef LINE_H
#define LINE_H

#include "clcc.h"

enum line_tone { TONE_OFF, TONE_DIAL, TONE_BUSY, TONE_RINGBACK, TONE_WAITING, TONE_NOSERVICE, TONE_CONGESTION, TONE_CONFIRM, TONE_STUTTER };

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
	void (*dtmf)(void *ctx, char digit);
	void (*log)(void *ctx, const char *what, const char *number);
	const char *(*speed)(void *ctx, const char *code);
	int (*blocked)(void *ctx, const char *number);
	int (*barred)(void *ctx, const char *number);
	int (*service)(void *ctx, const char *code);
	void (*dnd_code)(void *ctx, int on);
	void (*quiet_for)(void *ctx, int minutes);
	int (*elsewhere)(void *ctx);
	int (*call_fail)(void *ctx);
	void *ctx;
};

#define LINE_DIGITS_MAX 24
#define LINE_CODE_MAX 8

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
	int no_service;
	int missed_tone, missed_flag, stutter;
	int recall, waiting_on, ncalls, active_id, held, blocked_id, released;
	char last_out[CLCC_NUM_MAX];
	char last_in[CLCC_NUM_MAX];
	char code_redial[LINE_CODE_MAX];
	char code_callback[LINE_CODE_MAX];
	char code_dnd_on[LINE_CODE_MAX];
	char code_dnd_off[LINE_CODE_MAX];
	char hotline[LINE_DIGITS_MAX];
	char autodial[LINE_DIGITS_MAX];
	unsigned long t_state, t_digit, t_onhook, t_hangup, t_gone, t_block;
	unsigned dialtone_ms, digit_ms, flash_min_ms, flash_ms, busy_ms, setup_ms, gone_ms, hotline_ms;
};

int line_emergency(const char *number);
void line_autodial(struct line *l, const char *number, unsigned long now);
void line_init(struct line *l, const struct line_ops *ops);
int line_set_codes(struct line *l, const char *redial, const char *callback);
int line_set_dnd_codes(struct line *l, const char *on, const char *off);
void line_hook(struct line *l, int offhook, unsigned long now);
void line_digit(struct line *l, char d, unsigned long now);
void line_modem(struct line *l, const struct clcc_call *calls, int n, unsigned long now);
void line_tick(struct line *l, unsigned long now);
void line_released(struct line *l);
const char *line_state_name(enum line_state s);

#endif
