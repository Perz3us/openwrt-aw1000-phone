// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2026 perz3us
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include "line.h"

#define HANGUP_RETRY_MS 2000

static void enter(struct line *l, enum line_state s, unsigned long now)
{
	l->state = s;
	l->t_state = now;
	l->t_gone = ULONG_MAX;
	l->released = 0;
}

void line_released(struct line *l)
{
	if (l->state == LS_TALKING)
		l->released = 10;
}

static void note(struct line *l, const char *what, const char *num)
{
	if (l->ops->log)
		l->ops->log(l->ops->ctx, what, num);
}

static void waiting_off(struct line *l)
{
	if (l->waiting_on) {
		l->ops->tone(l->ops->ctx, TONE_OFF);
		l->waiting_on = 0;
	}
}

static void go_busy(struct line *l, enum line_tone t, unsigned long now)
{
	l->waiting_on = 0;
	l->ops->audio(l->ops->ctx, 0);
	l->ops->tone(l->ops->ctx, t);
	enter(l, LS_BUSY, now);
}

int line_emergency(const char *number)
{
	static const char *const list[] = {
		"000", "08", "100", "101", "102", "108", "110", "112", "118", "119", "911", "999", "1990",
	};
	size_t i;

	if (*number == '+')
		number++;
	for (i = 0; i < sizeof(list) / sizeof(list[0]); i++)
		if (!strcmp(number, list[i]))
			return 1;
	return 0;
}

static int code_ok(const char *c);

int line_set_dnd_codes(struct line *l, const char *on, const char *off)
{
	if (!code_ok(on) || !code_ok(off) || !strcmp(on, off) ||
	    !strcmp(on, l->code_redial) || !strcmp(on, l->code_callback) ||
	    !strcmp(off, l->code_redial) || !strcmp(off, l->code_callback))
		return -1;
	strcpy(l->code_dnd_on, on);
	strcpy(l->code_dnd_off, off);
	return 0;
}

static int only_marks(const char *d, unsigned n)
{
	unsigned i;

	for (i = 0; i < n; i++)
		if (d[i] != '*' && d[i] != '#')
			return 0;
	return 1;
}

static void service_code(struct line *l, unsigned long now)
{
	char *d = l->digits;
	size_t n = strlen(d);

	if (d[n - 1] != '#') {
		if (n + 1 >= sizeof(l->digits)) {
			go_busy(l, TONE_BUSY, now);
			return;
		}
		d[n] = '#';
		d[n + 1] = 0;
	}
	go_busy(l, l->ops->service(l->ops->ctx, d) ? TONE_BUSY : TONE_CONFIRM, now);
}

static int quiet_minutes(const struct line *l, const char *d)
{
	size_t k = strlen(l->code_dnd_on), i;
	int m = 0;

	if (strncmp(d, l->code_dnd_on, k) || d[k] != '*' || !d[k + 1] || strlen(d + k + 1) > 3)
		return -1;
	for (i = k + 1; d[i]; i++) {
		if (d[i] < '0' || d[i] > '9')
			return -1;
		m = m * 10 + d[i] - '0';
	}
	return m;
}

static void place_call(struct line *l, unsigned long now)
{
	const char *num;
	char *d = l->digits;
	int m;

	d[l->n] = 0;
	if (l->ops->quiet_for && (m = quiet_minutes(l, d)) >= 0) {
		if (m >= 1 && m <= 720)
			l->ops->quiet_for(l->ops->ctx, m);
		go_busy(l, m >= 1 && m <= 720 ? TONE_CONFIRM : TONE_BUSY, now);
		return;
	}
	if (d[0] == '*' && l->ops->speed && (num = l->ops->speed(l->ops->ctx, d + 1))) {
		snprintf(d, sizeof(l->digits), "%s", num);
	} else if (l->ops->dnd_code && (!strcmp(d, l->code_dnd_on) || !strcmp(d, l->code_dnd_off))) {
		l->ops->dnd_code(l->ops->ctx, !strcmp(d, l->code_dnd_on));
		go_busy(l, TONE_CONFIRM, now);
		return;
	} else if (!strcmp(d, l->code_redial) || !strcmp(d, l->code_callback) || !strcmp(d, "*86")) {
		num = !strcmp(d, l->code_redial) ? l->last_out : !strcmp(d, l->code_callback) ? l->last_in : "";
		if (!*num || strlen(num) >= sizeof(l->digits)) {
			go_busy(l, TONE_BUSY, now);
			return;
		}
		memcpy(d, num, strlen(num) + 1);
	}
	if (!line_emergency(d) && l->ops->barred && l->ops->barred(l->ops->ctx, d)) {
		note(l, "barred", d);
		go_busy(l, TONE_BUSY, now);
		return;
	}
	if ((*d == '*' || *d == '#') && only_marks(d, strlen(d))) {
		go_busy(l, TONE_BUSY, now);
		return;
	}
	if ((*d == '*' || *d == '#') && l->ops->service) {
		service_code(l, now);
		return;
	}
	strcpy(l->peer, d);
	strcpy(l->last_out, d);
	l->seen_call = 0;
	l->ringback_on = 0;
	l->ops->dial(l->ops->ctx, l->digits);
	l->ops->audio(l->ops->ctx, 1);
	note(l, "dial", l->peer);
	enter(l, LS_CALLING, now);
}

static void dial_now(struct line *l, const char *number, unsigned long now)
{
	snprintf(l->digits, sizeof(l->digits), "%s", number);
	l->n = strlen(l->digits);
	l->autodial[0] = 0;
	l->ops->tone(l->ops->ctx, TONE_OFF);
	place_call(l, now);
}

void line_autodial(struct line *l, const char *number, unsigned long now)
{
	snprintf(l->autodial, sizeof(l->autodial), "%s", number ? number : "");
	if (*l->autodial && l->state == LS_DIALTONE)
		dial_now(l, l->autodial, now);
}

void line_init(struct line *l, const struct line_ops *ops)
{
	memset(l, 0, sizeof(*l));
	l->ops = ops;
	l->state = LS_IDLE;
	l->t_hangup = ULONG_MAX;
	l->t_gone = ULONG_MAX;
	l->dialtone_ms = 15000;
	l->digit_ms = 4000;
	l->flash_min_ms = 80;
	l->flash_ms = 700;
	l->busy_ms = 60000;
	l->setup_ms = 10000;
	l->gone_ms = 3000;
	l->ringback = 1;
	strcpy(l->code_redial, "*66");
	strcpy(l->code_callback, "*69");
	strcpy(l->code_dnd_on, "*78");
	strcpy(l->code_dnd_off, "*79");
}

static int code_ok(const char *c)
{
	size_t n = strlen(c);

	if (n < 2 || n >= LINE_CODE_MAX || *c != '*' || strspn(c + 1, "0123456789") != n - 1)
		return 0;
	return strcmp(c, "*86") != 0;
}

int line_set_codes(struct line *l, const char *redial, const char *callback)
{
	if (!code_ok(redial) || !code_ok(callback) || !strcmp(redial, callback) ||
	    !strcmp(redial, l->code_dnd_on) || !strcmp(redial, l->code_dnd_off) ||
	    !strcmp(callback, l->code_dnd_on) || !strcmp(callback, l->code_dnd_off))
		return -1;
	strcpy(l->code_redial, redial);
	strcpy(l->code_callback, callback);
	return 0;
}

static void hangup_active(struct line *l, unsigned long now)
{
	char arg[16];

	if (l->ncalls > 1 && l->active_id > 0 && l->ops->chld) {
		snprintf(arg, sizeof(arg), "1%d", l->active_id);
		l->ops->chld(l->ops->ctx, arg);
	} else {
		l->ops->hangup(l->ops->ctx);
	}
	l->t_hangup = now;
}

static void went_onhook(struct line *l, unsigned long now)
{
	int others = l->state == LS_TALKING && l->held && l->ncalls > 1;

	l->onhook_pending = 0;
	l->offhook = 0;
	l->waiting_on = 0;
	l->ops->tone(l->ops->ctx, TONE_OFF);
	l->ops->audio(l->ops->ctx, 0);
	if (l->state == LS_CALLING || l->state == LS_TALKING) {
		hangup_active(l, now);
		note(l, "hangup", l->peer);
	}
	if (others) {
		strcpy(l->peer, l->held_peer);
		l->recall = 1;
		l->t_hangup = ULONG_MAX;
		l->ops->ring(l->ops->ctx, 1);
		enter(l, LS_RINGING, now);
		return;
	}
	enter(l, LS_IDLE, now);
}

static void flash(struct line *l)
{
	if (l->state != LS_TALKING || !l->ops->chld)
		return;
	waiting_off(l);
	l->ops->chld(l->ops->ctx, "2");
	note(l, "flash", l->peer);
}

void line_hook(struct line *l, int offhook, unsigned long now)
{
	if (!offhook) {
		if (l->offhook && !l->onhook_pending) {
			l->onhook_pending = 1;
			l->t_onhook = now;
		}
		return;
	}
	if (l->onhook_pending) {
		l->onhook_pending = 0;
		if (now - l->t_onhook >= l->flash_min_ms)
			flash(l);
	}
	if (l->offhook)
		return;
	l->offhook = 1;
	if (l->state == LS_IDLE && *l->autodial) {
		l->ops->ring(l->ops->ctx, 0);
		dial_now(l, l->autodial, now);
	} else if (l->state == LS_IDLE) {
		l->n = 0;
		l->stutter = l->missed_flag && !l->no_service;
		l->missed_flag = 0;
		l->ops->tone(l->ops->ctx, l->stutter ? TONE_STUTTER : l->no_service ? TONE_NOSERVICE : TONE_DIAL);
		enter(l, LS_DIALTONE, now);
	} else if (l->state == LS_RINGING) {
		l->ops->ring(l->ops->ctx, 0);
		if (l->recall && l->ops->chld)
			l->ops->chld(l->ops->ctx, "2");
		else
			l->ops->answer(l->ops->ctx);
		l->missed_flag = 0;
		l->ops->audio(l->ops->ctx, 1);
		l->seen_call = 1;
		note(l, l->recall ? "retrieved" : "answered", l->peer);
		l->recall = 0;
		enter(l, LS_TALKING, now);
	}
}

void line_digit(struct line *l, char d, unsigned long now)
{
	if (l->state == LS_TALKING) {
		if (l->ops->dtmf)
			l->ops->dtmf(l->ops->ctx, d);
		return;
	}
	if (l->state == LS_DIALTONE) {
		l->ops->tone(l->ops->ctx, TONE_OFF);
		l->stutter = 0;
		l->n = 0;
		enter(l, LS_DIGITS, now);
	} else if (l->state != LS_DIGITS) {
		return;
	}
	if (d == '#' && !only_marks(l->digits, l->n)) {
		place_call(l, now);
		return;
	}
	l->digits[l->n++] = d;
	l->t_digit = now;
	if (l->n + 1 >= LINE_DIGITS_MAX)
		place_call(l, now);
}

static const struct clcc_call *find(const struct clcc_call *c, int n, int stat)
{
	int i;

	for (i = 0; i < n; i++)
		if (c[i].stat == stat)
			return &c[i];
	return NULL;
}

static const struct clcc_call *find_incoming(const struct clcc_call *c, int n)
{
	const struct clcc_call *in = find(c, n, CLCC_INCOMING);

	return in ? in : find(c, n, CLCC_WAITING);
}

static void talking(struct line *l, const struct clcc_call *calls, int n, unsigned long now)
{
	const struct clcc_call *active = find(calls, n, CLCC_ACTIVE);
	const struct clcc_call *waiting = find(calls, n, CLCC_WAITING);

	if (!n) {
		if (l->t_gone == ULONG_MAX)
			l->t_gone = now;
		if (!l->released && now - l->t_gone < l->gone_ms)
			return;
		l->t_gone = ULONG_MAX;
		l->ops->hangup(l->ops->ctx);
		l->t_hangup = now;
		note(l, "ended", l->peer);
		go_busy(l, TONE_BUSY, now);
		return;
	}
	l->t_gone = ULONG_MAX;
	if (l->released)
		l->released--;
	if (n > 1 && active && strcmp(l->peer, active->number)) {
		strcpy(l->peer, active->number);
		note(l, "switched", l->peer);
	}
	if (waiting && l->ops->blocked && l->ops->blocked(l->ops->ctx, waiting->number)) {
		if (waiting->id != l->blocked_id) {
			l->blocked_id = waiting->id;
			note(l, "blocked", waiting->number);
		} else if (now - l->t_block < HANGUP_RETRY_MS) {
			return;
		}
		l->t_block = now;
		waiting_off(l);
		if (l->ops->chld)
			l->ops->chld(l->ops->ctx, "0");
		return;
	}
	if (waiting && !l->waiting_on) {
		l->ops->tone(l->ops->ctx, TONE_WAITING);
		l->waiting_on = 1;
		strcpy(l->last_in, waiting->number);
		note(l, "waiting", waiting->number);
	} else if (!waiting) {
		waiting_off(l);
	}
}

void line_modem(struct line *l, const struct clcc_call *calls, int n, unsigned long now)
{
	const struct clcc_call *c = n ? &calls[0] : NULL, *in, *active, *held;

	active = find(calls, n, CLCC_ACTIVE);
	l->ncalls = n;
	l->active_id = active ? active->id : 0;
	held = find(calls, n, CLCC_HELD);
	l->held = held != NULL;
	if (held)
		strcpy(l->held_peer, held->number);

	switch (l->state) {
	case LS_IDLE:
		if (!c) {
			l->blocked_id = 0;
			break;
		}
		if ((in = find_incoming(calls, n)) && l->ops->blocked && l->ops->blocked(l->ops->ctx, in->number)) {
			if (in->id != l->blocked_id) {
				l->blocked_id = in->id;
				note(l, "blocked", in->number);
			} else if (now - l->t_hangup < HANGUP_RETRY_MS) {
				break;
			}
			l->ops->hangup(l->ops->ctx);
			l->t_hangup = now;
		} else if (in) {
			l->blocked_id = 0;
			strcpy(l->peer, in->number);
			strcpy(l->last_in, in->number);
			l->recall = 0;
			l->ops->ring(l->ops->ctx, 1);
			note(l, "incoming", l->peer);
			enter(l, LS_RINGING, now);
		} else if (l->t_hangup == ULONG_MAX || now - l->t_hangup >= HANGUP_RETRY_MS) {
			l->ops->hangup(l->ops->ctx);
			l->t_hangup = now;
		}
		break;
	case LS_RINGING:
		if (l->recall ? !find(calls, n, CLCC_HELD) && !active : !find_incoming(calls, n)) {
			const char *what = l->recall ? "ended" : l->ops->elsewhere && l->ops->elsewhere(l->ops->ctx) ?
					   "elsewhere" : "missed";

			l->ops->ring(l->ops->ctx, 0);
			note(l, what, l->peer);
			if (!strcmp(what, "missed") && l->missed_tone)
				l->missed_flag = 1;
			l->recall = 0;
			enter(l, LS_IDLE, now);
		}
		break;
	case LS_CALLING:
		if (c) {
			l->seen_call = 1;
			if (c->stat == CLCC_ALERTING && l->ringback && !l->ringback_on) {
				l->ops->tone(l->ops->ctx, TONE_RINGBACK);
				l->ringback_on = 1;
			}
			if (c->stat == CLCC_ACTIVE) {
				if (l->ringback_on)
					l->ops->tone(l->ops->ctx, TONE_OFF);
				note(l, "connected", l->peer);
				enter(l, LS_TALKING, now);
			}
		} else if (l->seen_call || now - l->t_state >= l->setup_ms) {
			note(l, "failed", l->peer);
			go_busy(l, l->ops->call_fail && l->ops->call_fail(l->ops->ctx) ? TONE_CONGESTION : TONE_BUSY, now);
		}
		break;
	case LS_TALKING:
		talking(l, calls, n, now);
		break;
	default:
		break;
	}
}

void line_tick(struct line *l, unsigned long now)
{
	if (l->onhook_pending && now - l->t_onhook >= l->flash_ms) {
		went_onhook(l, now);
		return;
	}
	if (l->state == LS_DIALTONE && l->stutter && now - l->t_state >= 1500) {
		l->stutter = 0;
		l->ops->tone(l->ops->ctx, TONE_DIAL);
	}
	if (l->state == LS_DIALTONE && *l->hotline && now - l->t_state >= l->hotline_ms)
		dial_now(l, l->hotline, now);
	else if (l->state == LS_DIALTONE && now - l->t_state >= l->dialtone_ms)
		go_busy(l, TONE_BUSY, now);
	else if (l->state == LS_DIGITS && now - l->t_digit >= l->digit_ms)
		place_call(l, now);
	else if (l->state == LS_BUSY && now - l->t_state >= l->busy_ms)
		l->ops->tone(l->ops->ctx, TONE_OFF);
}

const char *line_state_name(enum line_state s)
{
	static const char *const n[] = {
		"idle", "dialtone", "digits", "calling", "talking", "ringing", "busy",
	};

	return s <= LS_BUSY ? n[s] : "?";
}
