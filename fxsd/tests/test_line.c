// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2026 perz3us
#include <assert.h>
#include <string.h>
#include "../src/line.h"

static int hangups, audio_off, ended, busy;

static void tone(void *ctx, enum line_tone t) { (void)ctx; busy += t == TONE_BUSY; }
static void ring(void *ctx, int on) { (void)ctx; (void)on; }
static void audio(void *ctx, int on) { (void)ctx; audio_off += !on; }
static void dial(void *ctx, const char *n) { (void)ctx; (void)n; }
static void answer(void *ctx) { (void)ctx; }
static void hangup(void *ctx) { (void)ctx; hangups++; }
static void log_ev(void *ctx, const char *what, const char *n) { (void)ctx; (void)n; ended += !strcmp(what, "ended"); }

static const struct line_ops ops = {
	.tone = tone, .ring = ring, .audio = audio, .dial = dial,
	.answer = answer, .hangup = hangup, .log = log_ev,
};

static struct line talking_line(void)
{
	struct line l;

	line_init(&l, &ops);
	l.state = LS_TALKING;
	l.offhook = 1;
	strcpy(l.peer, "0712345678");
	return l;
}

int main(void)
{
	struct clcc_call c[CLCC_MAX] = { { .id = 3, .dir = 0, .stat = CLCC_ACTIVE, .number = "0712345678" } };
	struct line l = talking_line();

	line_modem(&l, c, 1, 1000);
	line_modem(&l, c, 0, 1500);
	assert(l.state == LS_TALKING && !hangups && !audio_off && !ended);
	line_modem(&l, c, 0, 3000);
	assert(l.state == LS_TALKING && !hangups);
	line_modem(&l, c, 1, 3500);
	assert(l.state == LS_TALKING && !hangups && !audio_off && !ended);

	line_modem(&l, c, 0, 4000);
	line_modem(&l, c, 1, 6500);
	line_modem(&l, c, 0, 7000);
	line_modem(&l, c, 0, 9500);
	assert(l.state == LS_TALKING && !hangups);

	line_modem(&l, c, 0, 10000);
	assert(l.state == LS_BUSY && hangups == 1 && audio_off == 1 && ended == 1 && busy == 1);
	line_modem(&l, c, 0, 10500);
	assert(hangups == 1 && ended == 1);

	l = talking_line();
	hangups = audio_off = ended = busy = 0;
	line_modem(&l, c, 0, 20000);
	line_modem(&l, c, 0, 23000);
	assert(l.state == LS_BUSY && hangups == 1 && ended == 1);

	l = talking_line();
	hangups = audio_off = ended = busy = 0;
	line_modem(&l, c, 1, 30000);
	line_released(&l);
	line_modem(&l, c, 0, 30100);
	assert(l.state == LS_BUSY && hangups == 1 && ended == 1);

	l = talking_line();
	hangups = ended = 0;
	line_released(&l);
	line_modem(&l, c, 1, 40000);
	assert(l.released == 9);
	for (int i = 0; i < 9; i++)
		line_modem(&l, c, 1, 40500 + i * 500);
	line_modem(&l, c, 0, 46000);
	assert(l.state == LS_TALKING && !hangups);

	l = talking_line();
	l.gone_ms = 0;
	hangups = ended = 0;
	line_modem(&l, c, 0, 50000);
	assert(l.state == LS_BUSY && hangups == 1 && ended == 1);
	return 0;
}
