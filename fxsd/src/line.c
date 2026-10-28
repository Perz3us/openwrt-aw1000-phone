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

static void go_busy(struct line *l, unsigned long now)
{
	l->waiting_on = 0;
	l->ops->audio(l->ops->ctx, 0);
	l->ops->tone(l->ops->ctx, TONE_BUSY);
	enter(l, LS_BUSY, now);
}

static void place_call(struct line *l, unsigned long now)
{
	const char *num = l->digits;

	l->digits[l->n] = 0;
	if (l->digits[0] == '*' && l->ops->speed && (num = l->ops->speed(l->ops->ctx, l->digits + 1)))
		snprintf(l->digits, sizeof(l->digits), "%s", num);
	strcpy(l->peer, l->digits);
	l->seen_call = 0;
	l->ringback_on = 0;
	l->ops->dial(l->ops->ctx, l->digits);
	l->ops->audio(l->ops->ctx, 1);
	note(l, "dial", l->peer);
	enter(l, LS_CALLING, now);
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
	if (l->state == LS_IDLE) {
		l->n = 0;
		l->ops->tone(l->ops->ctx, TONE_DIAL);
		enter(l, LS_DIALTONE, now);
	} else if (l->state == LS_RINGING) {
		l->ops->ring(l->ops->ctx, 0);
		if (l->recall && l->ops->chld)
			l->ops->chld(l->ops->ctx, "2");
		else
			l->ops->answer(l->ops->ctx);
		l->ops->audio(l->ops->ctx, 1);
		l->seen_call = 1;
		note(l, l->recall ? "retrieved" : "answered", l->peer);
		l->recall = 0;
		enter(l, LS_TALKING, now);
	}
}

void line_digit(struct line *l, char d, unsigned long now)
{
	if (l->state == LS_DIALTONE) {
		if (d == '#')
			return;
		l->ops->tone(l->ops->ctx, TONE_OFF);
		l->n = 0;
		enter(l, LS_DIGITS, now);
	} else if (l->state != LS_DIGITS) {
		return;
	}
	if (d == '#') {
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
		go_busy(l, now);
		return;
	}
	l->t_gone = ULONG_MAX;
	if (l->released)
		l->released--;
	if (n > 1 && active && strcmp(l->peer, active->number)) {
		strcpy(l->peer, active->number);
		note(l, "switched", l->peer);
	}
	if (waiting && !l->waiting_on) {
		l->ops->tone(l->ops->ctx, TONE_WAITING);
		l->waiting_on = 1;
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
			l->ops->ring(l->ops->ctx, 0);
			note(l, l->recall ? "ended" : l->ops->elsewhere && l->ops->elsewhere(l->ops->ctx) ?
			     "elsewhere" : "missed", l->peer);
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
			go_busy(l, now);
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
	if (l->state == LS_DIALTONE && now - l->t_state >= l->dialtone_ms)
		go_busy(l, now);
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
