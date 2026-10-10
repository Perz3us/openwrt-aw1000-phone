// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2026 perz3us
#include <limits.h>
#include <string.h>
#include "line.h"

#define HANGUP_RETRY_MS 2000

static void enter(struct line *l, enum line_state s, unsigned long now)
{
	l->state = s;
	l->t_state = now;
}

static void note(struct line *l, const char *what, const char *num)
{
	if (l->ops->log)
		l->ops->log(l->ops->ctx, what, num);
}

static void go_busy(struct line *l, unsigned long now)
{
	l->ops->audio(l->ops->ctx, 0);
	l->ops->tone(l->ops->ctx, TONE_BUSY);
	enter(l, LS_BUSY, now);
}

static void place_call(struct line *l, unsigned long now)
{
	l->digits[l->n] = 0;
	strcpy(l->peer, l->digits);
	l->seen_call = 0;
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
	l->dialtone_ms = 15000;
	l->digit_ms = 4000;
	l->flash_ms = 400;
	l->busy_ms = 60000;
	l->setup_ms = 10000;
}

static void went_onhook(struct line *l, unsigned long now)
{
	l->onhook_pending = 0;
	l->offhook = 0;
	l->ops->tone(l->ops->ctx, TONE_OFF);
	l->ops->audio(l->ops->ctx, 0);
	if (l->state == LS_CALLING || l->state == LS_TALKING) {
		l->ops->hangup(l->ops->ctx);
		l->t_hangup = now;
		note(l, "hangup", l->peer);
	}
	enter(l, LS_IDLE, now);
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
	l->onhook_pending = 0;
	if (l->offhook)
		return;
	l->offhook = 1;
	if (l->state == LS_IDLE) {
		l->n = 0;
		l->ops->tone(l->ops->ctx, TONE_DIAL);
		enter(l, LS_DIALTONE, now);
	} else if (l->state == LS_RINGING) {
		l->ops->ring(l->ops->ctx, 0);
		l->ops->answer(l->ops->ctx);
		l->ops->audio(l->ops->ctx, 1);
		l->seen_call = 1;
		note(l, "answered", l->peer);
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

void line_modem(struct line *l, const struct clcc_call *c, unsigned long now)
{
	switch (l->state) {
	case LS_IDLE:
		if (!c)
			break;
		if (c->stat == CLCC_INCOMING || c->stat == CLCC_WAITING) {
			strcpy(l->peer, c->number);
			l->ops->ring(l->ops->ctx, 1);
			note(l, "incoming", l->peer);
			enter(l, LS_RINGING, now);
		} else if (l->t_hangup == ULONG_MAX || now - l->t_hangup >= HANGUP_RETRY_MS) {
			l->ops->hangup(l->ops->ctx);
			l->t_hangup = now;
		}
		break;
	case LS_RINGING:
		if (!c) {
			l->ops->ring(l->ops->ctx, 0);
			note(l, "missed", l->peer);
			enter(l, LS_IDLE, now);
		}
		break;
	case LS_CALLING:
		if (c) {
			l->seen_call = 1;
			if (c->stat == CLCC_ACTIVE) {
				note(l, "connected", l->peer);
				enter(l, LS_TALKING, now);
			}
		} else if (l->seen_call || now - l->t_state >= l->setup_ms) {
			note(l, "failed", l->peer);
			go_busy(l, now);
		}
		break;
	case LS_TALKING:
		if (!c) {
			note(l, "ended", l->peer);
			go_busy(l, now);
		}
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
