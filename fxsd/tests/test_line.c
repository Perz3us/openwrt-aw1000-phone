// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2026 perz3us
#include <assert.h>
#include <string.h>
#include "../src/line.h"

static int hangups, audio_off, ended, busy, dialtone, ring_off;
static char dialed[LINE_DIGITS_MAX];

static void tone(void *ctx, enum line_tone t) { (void)ctx; busy += t == TONE_BUSY; dialtone += t == TONE_DIAL; }
static void ring(void *ctx, int on) { (void)ctx; ring_off += !on; }
static void audio(void *ctx, int on) { (void)ctx; audio_off += !on; }
static void dial(void *ctx, const char *n) { (void)ctx; strcpy(dialed, n); }
static void answer(void *ctx) { (void)ctx; }
static void hangup(void *ctx) { (void)ctx; hangups++; }
static void log_ev(void *ctx, const char *what, const char *n) { (void)ctx; (void)n; ended += !strcmp(what, "ended"); }

static int speed_66;
static struct line talking_line(void);
static char sent[8];
static void dtmf(void *ctx, char d) { (void)ctx; sent[strlen(sent)] = d; }

static const struct line_ops ops = {
	.tone = tone, .ring = ring, .audio = audio, .dial = dial,
	.answer = answer, .hangup = hangup, .dtmf = dtmf, .log = log_ev,
};

static const char *speed(void *ctx, const char *code)
{
	(void)ctx;
	if (!strcmp(code, "86"))
		return "0771110000";
	return !strcmp(code, "66") && speed_66 ? "0779990000" : NULL;
}

static const struct line_ops ops_speed = {
	.tone = tone, .ring = ring, .audio = audio, .dial = dial,
	.answer = answer, .hangup = hangup, .dtmf = dtmf, .log = log_ev, .speed = speed,
};

static void dial_keys(struct line *l, const char *keys, unsigned long now)
{
	busy = 0;
	dialed[0] = 0;
	line_init(l, &ops_speed);
	strcpy(l->last_out, "0770000001");
	strcpy(l->last_in, "0770000002");
	line_hook(l, 1, now);
	for (; *keys; keys++)
		line_digit(l, *keys, now);
}

static void test_codes(void)
{
	struct clcc_call in[CLCC_MAX] = { { .id = 1, .dir = 1, .stat = CLCC_INCOMING, .number = "0770000003" } };
	struct clcc_call priv[CLCC_MAX] = { { .id = 1, .dir = 1, .stat = CLCC_INCOMING } };
	struct line l;

	dial_keys(&l, "*66#", 0);
	assert(!strcmp(dialed, "0770000001") && l.state == LS_CALLING);
	dial_keys(&l, "*69#", 0);
	assert(!strcmp(dialed, "0770000002") && !strcmp(l.peer, "0770000002"));

	dial_keys(&l, "", 0);
	l.last_out[0] = 0;
	line_digit(&l, '*', 0);
	line_digit(&l, '6', 0);
	line_digit(&l, '6', 0);
	line_digit(&l, '#', 0);
	assert(!dialed[0] && l.state == LS_BUSY && busy == 1);

	speed_66 = 1;
	dial_keys(&l, "*66#", 0);
	assert(!strcmp(dialed, "0779990000"));
	speed_66 = 0;
	dial_keys(&l, "*86#", 0);
	assert(!strcmp(dialed, "0771110000"));

	dial_keys(&l, "*670771234567#", 0);
	assert(!strcmp(dialed, "0771234567") && l.clir == 1 && !strcmp(l.peer, "0771234567"));
	dial_keys(&l, "*67*21*0771#", 0);
	assert(!strcmp(dialed, "*67*21*0771") && l.clir == 0);
	dial_keys(&l, "0771234567#", 0);
	assert(l.clir == 0);

	line_init(&l, &ops_speed);
	l.hide = 1;
	line_hook(&l, 1, 0);
	line_digit(&l, '0', 0);
	line_digit(&l, '#', 0);
	assert(l.clir == 1);

	line_init(&l, &ops_speed);
	l.hide = 1;
	line_hook(&l, 1, 0);
	for (const char *k = "*820771#"; *k; k++)
		line_digit(&l, *k, 0);
	assert(!strcmp(dialed, "0771") && l.clir == 2);

	line_init(&l, &ops_speed);
	l.hide = 1;
	line_hook(&l, 1, 0);
	for (const char *k = "112#"; *k; k++)
		line_digit(&l, *k, 0);
	assert(!strcmp(dialed, "112") && l.clir == 0);
	dial_keys(&l, "*67112#", 0);
	assert(!strcmp(dialed, "112") && l.clir == 0);

	assert(line_emergency("112") && line_emergency("+911") && !line_emergency("1120") &&
	       !line_emergency("") && !line_emergency("0771234567"));

	line_init(&l, &ops_speed);
	line_modem(&l, in, 1, 0);
	assert(!strcmp(l.last_in, "0770000003"));
	line_init(&l, &ops_speed);
	strcpy(l.last_in, "0770000002");
	line_modem(&l, priv, 1, 0);
	assert(!l.last_in[0]);
}

static int is_blocked(void *ctx, const char *n)
{
	(void)ctx;
	return !strcmp(n, "0770000009");
}

static char chld_arg[8];
static int chld_calls, waiting_tones, blocked_notes;
static void chld(void *ctx, const char *arg) { (void)ctx; strcpy(chld_arg, arg); chld_calls++; }
static void tone_w(void *ctx, enum line_tone t) { (void)ctx; waiting_tones += t == TONE_WAITING; }
static void log_w(void *ctx, const char *what, const char *n) { (void)ctx; (void)n; blocked_notes += !strcmp(what, "blocked"); }

static const struct line_ops ops_block = {
	.tone = tone_w, .ring = ring, .audio = audio, .dial = dial, .answer = answer, .hangup = hangup,
	.chld = chld, .log = log_w, .blocked = is_blocked,
};

static void test_waiting_blocked(void)
{
	struct clcc_call c[CLCC_MAX] = {
		{ .id = 1, .dir = 0, .stat = CLCC_ACTIVE, .number = "0712345678" },
		{ .id = 2, .dir = 1, .stat = CLCC_WAITING, .number = "0770000009" },
	};
	struct line l;

	line_init(&l, &ops_block);
	l.state = LS_TALKING;
	l.offhook = 1;
	strcpy(l.last_in, "0770000002");
	line_modem(&l, c, 2, 1000);
	assert(!waiting_tones && chld_calls == 1 && !strcmp(chld_arg, "0") && blocked_notes == 1);
	assert(!strcmp(l.last_in, "0770000002") && l.state == LS_TALKING);
	line_modem(&l, c, 2, 1500);
	assert(chld_calls == 1 && blocked_notes == 1);
	line_modem(&l, c, 2, 3100);
	assert(chld_calls == 2 && blocked_notes == 1);

	strcpy(c[1].number, "0770000004");
	line_modem(&l, c, 2, 3200);
	assert(waiting_tones == 1 && chld_calls == 2 && !strcmp(l.last_in, "0770000004"));
}

static void test_no_voicemail(void)
{
	struct line l;

	busy = 0;
	dialed[0] = 0;
	line_init(&l, &ops);
	line_hook(&l, 1, 0);
	line_digit(&l, '*', 0);
	line_digit(&l, '8', 0);
	line_digit(&l, '6', 0);
	line_digit(&l, '#', 0);
	assert(!dialed[0] && l.state == LS_BUSY && busy == 1);
}

static void test_hotline(void)
{
	struct line l;

	dialed[0] = 0;
	line_init(&l, &ops_speed);
	strcpy(l.hotline, "0771112222");
	l.hotline_ms = 5000;
	line_hook(&l, 1, 1000);
	line_tick(&l, 5900);
	assert(l.state == LS_DIALTONE && !dialed[0]);
	line_tick(&l, 6000);
	assert(l.state == LS_CALLING && !strcmp(dialed, "0771112222"));

	dialed[0] = 0;
	line_init(&l, &ops_speed);
	strcpy(l.hotline, "0771112222");
	l.hotline_ms = 5000;
	line_hook(&l, 1, 1000);
	line_digit(&l, '1', 2000);
	line_tick(&l, 5500);
	assert(l.state == LS_DIGITS && !dialed[0]);

	dialed[0] = 0;
	line_init(&l, &ops_speed);
	strcpy(l.hotline, "0771112222");
	line_hook(&l, 1, 1000);
	line_tick(&l, 1000);
	assert(!strcmp(dialed, "0771112222"));
}

static void test_autodial(void)
{
	struct line l;

	dialed[0] = 0;
	dialtone = ring_off = 0;
	line_init(&l, &ops_speed);
	line_autodial(&l, "0773334444", 0);
	assert(l.state == LS_IDLE && !dialed[0]);
	line_hook(&l, 1, 100);
	assert(l.state == LS_CALLING && !strcmp(dialed, "0773334444") && !dialtone && ring_off == 1 && !l.autodial[0]);

	dialed[0] = 0;
	line_init(&l, &ops_speed);
	line_hook(&l, 1, 100);
	assert(l.state == LS_DIALTONE);
	line_autodial(&l, "0773334444", 200);
	assert(l.state == LS_CALLING && !strcmp(dialed, "0773334444"));

	dialed[0] = 0;
	line_init(&l, &ops_speed);
	line_autodial(&l, "0773334444", 0);
	line_autodial(&l, NULL, 10);
	line_hook(&l, 1, 100);
	assert(l.state == LS_DIALTONE && !dialed[0]);
}

static void test_timing(void)
{
	struct line l;

	line_init(&l, &ops_speed);
	l.digit_ms = 8000;
	line_hook(&l, 1, 0);
	line_digit(&l, '5', 1000);
	line_tick(&l, 8900);
	assert(l.state == LS_DIGITS);
	line_tick(&l, 9000);
	assert(l.state == LS_CALLING);

	l = talking_line();
	l.flash_ms = 1200;
	line_hook(&l, 0, 1000);
	line_tick(&l, 2100);
	assert(l.state == LS_TALKING);
	line_tick(&l, 2200);
	assert(l.state == LS_IDLE);
}

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

	l = talking_line();
	line_digit(&l, '1', 60000);
	line_digit(&l, '*', 60100);
	line_digit(&l, '#', 60200);
	assert(!strcmp(sent, "1*#") && l.state == LS_TALKING && !l.n);

	sent[0] = sent[1] = sent[2] = 0;
	l.state = LS_CALLING;
	line_digit(&l, '5', 61000);
	l.state = LS_RINGING;
	line_digit(&l, '5', 61100);
	l.state = LS_BUSY;
	line_digit(&l, '5', 61200);
	assert(!sent[0]);

	test_codes();
	test_waiting_blocked();
	test_no_voicemail();
	test_hotline();
	test_autodial();
	test_timing();
	return 0;
}
