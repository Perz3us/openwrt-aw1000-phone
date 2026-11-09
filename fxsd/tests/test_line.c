// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2026 perz3us
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/line.h"
#include "../src/rules.h"

static int hangups, audio_off, ended, busy, dialtone, ring_off;
static char dialed[LINE_DIGITS_MAX];
static enum line_tone last_tone;
static int fail_ret, fail_calls;

static void tone(void *ctx, enum line_tone t) { (void)ctx; last_tone = t; busy += t == TONE_BUSY; dialtone += t == TONE_DIAL; }
static void ring(void *ctx, int on) { (void)ctx; ring_off += !on; }
static void audio(void *ctx, int on) { (void)ctx; audio_off += !on; }
static void dial(void *ctx, const char *n) { (void)ctx; strcpy(dialed, n); }
static void answer(void *ctx) { (void)ctx; }
static void hangup(void *ctx) { (void)ctx; hangups++; }
static void log_ev(void *ctx, const char *what, const char *n) { (void)ctx; (void)n; ended += !strcmp(what, "ended"); }

static int call_fail(void *ctx) { (void)ctx; fail_calls++; return fail_ret; }

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

	line_init(&l, &ops_speed);
	assert(line_set_codes(&l, "*86", "*77") && line_set_codes(&l, "66", "*77") &&
	       line_set_codes(&l, "*", "*77") && line_set_codes(&l, "*1a", "*77") &&
	       line_set_codes(&l, "*1234567", "*77") && line_set_codes(&l, "*77", "*77") &&
	       line_set_codes(&l, "*77", ""));
	assert(!strcmp(l.code_redial, "*66") && !strcmp(l.code_callback, "*69"));
	assert(!line_set_codes(&l, "*123456", "*77"));
	strcpy(l.last_out, "0770000001");
	strcpy(l.last_in, "0770000002");
	line_hook(&l, 1, 0);
	for (const char *k = "*123456#"; *k; k++)
		line_digit(&l, *k, 0);
	assert(!strcmp(dialed, "0770000001"));
	line_hook(&l, 0, 0);
	line_tick(&l, 5000);
	line_hook(&l, 1, 5000);
	for (const char *k = "*77#"; *k; k++)
		line_digit(&l, *k, 5000);
	assert(!strcmp(dialed, "0770000002"));
	line_hook(&l, 0, 5000);
	line_tick(&l, 10000);
	line_hook(&l, 1, 10000);
	for (const char *k = "*69#"; *k; k++)
		line_digit(&l, *k, 10000);
	assert(!strcmp(dialed, "*69"));

	dial_keys(&l, "*670771234567#", 0);
	assert(!strcmp(dialed, "*670771234567"));

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

static const struct line_ops ops_fail = {
	.tone = tone, .ring = ring, .audio = audio, .dial = dial,
	.answer = answer, .hangup = hangup, .dtmf = dtmf, .log = log_ev, .call_fail = call_fail,
};

static void test_tones(void)
{
	struct line l;
	struct clcc_call talk[CLCC_MAX] = { { .id = 1, .dir = 0, .stat = CLCC_ACTIVE, .number = "0771234567" } };

	line_init(&l, &ops);
	l.no_service = 1;
	line_hook(&l, 1, 0);
	assert(l.state == LS_DIALTONE && last_tone == TONE_NOSERVICE);
	line_digit(&l, '1', 100);
	assert(l.state == LS_DIGITS && last_tone == TONE_OFF);
	line_digit(&l, '1', 200);
	line_digit(&l, '2', 300);
	line_digit(&l, '#', 400);
	assert(l.state == LS_CALLING && !strcmp(dialed, "112"));

	line_init(&l, &ops);
	l.no_service = 0;
	line_hook(&l, 1, 0);
	assert(last_tone == TONE_DIAL);

	line_init(&l, &ops);
	l.no_service = 1;
	line_hook(&l, 1, 0);
	line_tick(&l, 15000);
	assert(l.state == LS_BUSY && last_tone == TONE_BUSY);

	fail_ret = 1;
	fail_calls = 0;
	line_init(&l, &ops_fail);
	line_hook(&l, 1, 0);
	line_digit(&l, '5', 10);
	line_digit(&l, '#', 20);
	line_modem(&l, talk, 0, 11000);
	assert(l.state == LS_BUSY && last_tone == TONE_CONGESTION && fail_calls == 1);

	fail_ret = 0;
	line_init(&l, &ops_fail);
	line_hook(&l, 1, 0);
	line_digit(&l, '5', 10);
	line_digit(&l, '#', 20);
	line_modem(&l, talk, 0, 11000);
	assert(l.state == LS_BUSY && last_tone == TONE_BUSY);

	fail_ret = 1;
	line_init(&l, &ops);
	line_hook(&l, 1, 0);
	line_digit(&l, '5', 10);
	line_digit(&l, '#', 20);
	line_modem(&l, talk, 0, 11000);
	assert(l.state == LS_BUSY && last_tone == TONE_BUSY);

	fail_calls = 0;
	l = talking_line();
	l.ops = &ops_fail;
	l.gone_ms = 0;
	line_modem(&l, talk, 0, 50000);
	assert(l.state == LS_BUSY && last_tone == TONE_BUSY && !fail_calls);
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

static const char out_list[2][32] = { "00", "+1" };
static const char out_allow[1][32] = { "077" };
static int out_mode;

static int barred(void *ctx, const char *number)
{
	(void)ctx;
	return out_mode == RULES_BLOCK ? rules_barred(number, out_mode, out_list, 2) :
	       rules_barred(number, out_mode, out_allow, 1);
}

static void keys(struct line *l, const char *k)
{
	busy = 0;
	dialed[0] = 0;
	line_hook(l, 1, 0);
	for (; *k; k++)
		line_digit(l, *k, 0);
}

static const char *svc_seen;
static char svc_copy[32];
static int svc_ret;

static int service(void *ctx, const char *code)
{
	(void)ctx;
	snprintf(svc_copy, sizeof(svc_copy), "%s", code);
	svc_seen = svc_copy;
	return svc_ret;
}

static void test_service(void)
{
	static const struct line_ops o = {
		.tone = tone, .ring = ring, .audio = audio, .dial = dial, .answer = answer,
		.hangup = hangup, .dtmf = dtmf, .log = log_ev, .speed = speed, .service = service,
	};
	static const char *const codes[] = { "*#21#", "#21#", "##21#", "*21*0771#" };
	struct line l;
	size_t i;

	for (i = 0; i < sizeof(codes) / sizeof(codes[0]); i++) {
		svc_ret = 0;
		svc_seen = NULL;
		line_init(&l, &o);
		keys(&l, codes[i]);
		assert(svc_seen && !strcmp(svc_seen, codes[i]));
		assert(!dialed[0] && l.state == LS_BUSY && last_tone == TONE_CONFIRM && !l.last_out[0]);
	}

	svc_ret = -1;
	line_init(&l, &o);
	keys(&l, "*#21#");
	assert(l.state == LS_BUSY && last_tone == TONE_BUSY);

	svc_ret = 0;
	svc_seen = NULL;
	line_init(&l, &o);
	strcpy(l.last_out, "0771");
	keys(&l, "*66#");
	assert(!svc_seen && !strcmp(dialed, "0771"));

	line_init(&l, &o);
	keys(&l, "0771#");
	assert(!svc_seen && !strcmp(dialed, "0771"));

	svc_seen = NULL;
	line_init(&l, &o);
	keys(&l, "*#");
	line_tick(&l, 5000);
	assert(!svc_seen && !dialed[0] && l.state == LS_BUSY && last_tone == TONE_BUSY);
}

static int dnd_seen = -1;

static void dnd_code(void *ctx, int on)
{
	(void)ctx;
	dnd_seen = on;
}

static int quiet_minutes = -1;

static void quiet_for(void *ctx, int minutes)
{
	(void)ctx;
	quiet_minutes = minutes;
}

static void test_quiet_codes(void)
{
	static const struct line_ops o = {
		.tone = tone, .ring = ring, .audio = audio, .dial = dial, .answer = answer,
		.hangup = hangup, .dtmf = dtmf, .log = log_ev, .speed = speed, .dnd_code = dnd_code,
		.quiet_for = quiet_for,
	};
	struct line l;

	line_init(&l, &o);
	svc_seen = NULL;
	keys(&l, "*78*60#");
	assert(quiet_minutes == 60 && !svc_seen && !dialed[0] && l.state == LS_BUSY && last_tone == TONE_CONFIRM);
	quiet_minutes = -1;
	line_init(&l, &o);
	keys(&l, "*78*0#");
	assert(quiet_minutes == -1 && !svc_seen && last_tone == TONE_BUSY);
	line_init(&l, &o);
	keys(&l, "*78*721#");
	assert(quiet_minutes == -1 && !svc_seen && last_tone == TONE_BUSY);
	line_init(&l, &o);
	keys(&l, "*78*1#");
	assert(quiet_minutes == 1);
	quiet_minutes = -1;

	line_init(&l, &o);
	keys(&l, "*78#");
	assert(dnd_seen == 1 && !dialed[0] && l.state == LS_BUSY && last_tone == TONE_CONFIRM);
	line_init(&l, &o);
	keys(&l, "*79#");
	assert(dnd_seen == 0 && !dialed[0]);

	line_init(&l, &o);
	assert(line_set_dnd_codes(&l, "*66", "*79") && line_set_dnd_codes(&l, "*78", "*69"));
	assert(line_set_dnd_codes(&l, "*70", "*70") && line_set_dnd_codes(&l, "78", "*79"));
	assert(line_set_codes(&l, "*78", "*77") && line_set_codes(&l, "*77", "*79"));
	assert(!strcmp(l.code_dnd_on, "*78"));
	assert(!line_set_dnd_codes(&l, "*70", "*71"));
	dnd_seen = -1;
	keys(&l, "*70#");
	assert(dnd_seen == 1);
	line_hook(&l, 0, 0);
	line_tick(&l, 5000);
	keys(&l, "*78#");
	assert(dnd_seen == 1 && !strcmp(dialed, "*78"));
}

static void test_missed_tone(void)
{
	static const struct line_ops o = {
		.tone = tone, .ring = ring, .audio = audio, .dial = dial, .answer = answer,
		.hangup = hangup, .dtmf = dtmf, .log = log_ev, .speed = speed,
	};
	struct clcc_call in[CLCC_MAX] = { { .id = 1, .dir = 1, .stat = CLCC_INCOMING, .number = "0771" } };
	struct line l;

	line_init(&l, &o);
	l.missed_tone = 1;
	line_modem(&l, in, 1, 0);
	line_modem(&l, in, 0, 100);
	line_hook(&l, 1, 200);
	assert(last_tone == TONE_STUTTER && l.state == LS_DIALTONE);
	line_tick(&l, 1000);
	assert(last_tone == TONE_STUTTER);
	line_tick(&l, 1800);
	assert(last_tone == TONE_DIAL);
	line_hook(&l, 0, 2000);
	line_tick(&l, 3000);
	line_hook(&l, 1, 3000);
	assert(last_tone == TONE_DIAL);

	line_init(&l, &o);
	l.missed_tone = 1;
	line_modem(&l, in, 1, 0);
	line_modem(&l, in, 0, 100);
	line_hook(&l, 1, 200);
	line_digit(&l, '0', 300);
	assert(last_tone == TONE_OFF && l.state == LS_DIGITS);
	line_tick(&l, 1800);
	assert(last_tone == TONE_OFF);

	line_init(&l, &o);
	l.missed_tone = 1;
	l.no_service = 1;
	line_modem(&l, in, 1, 0);
	line_modem(&l, in, 0, 100);
	line_hook(&l, 1, 200);
	assert(last_tone == TONE_NOSERVICE);

	line_init(&l, &o);
	line_modem(&l, in, 1, 0);
	line_modem(&l, in, 0, 100);
	line_hook(&l, 1, 200);
	assert(last_tone == TONE_DIAL);

	line_init(&l, &o);
	l.missed_tone = 1;
	l.missed_flag = 1;
	line_modem(&l, in, 1, 0);
	line_hook(&l, 1, 100);
	assert(l.state == LS_TALKING && !l.missed_flag);
}

static void test_outgoing(void)
{
	static const struct line_ops o = {
		.tone = tone, .ring = ring, .audio = audio, .dial = dial, .answer = answer,
		.hangup = hangup, .dtmf = dtmf, .log = log_ev, .speed = speed, .barred = barred,
	};
	struct line l;

	assert(rules_barred("0094771", RULES_BLOCK, out_list, 2));
	assert(rules_barred("+94771", RULES_BLOCK, out_list, 2));
	assert(rules_barred("0014155", RULES_BLOCK, out_list, 2));
	assert(!rules_barred("0771", RULES_BLOCK, out_list, 2));
	assert(!rules_barred("0094771", RULES_OFF, out_list, 2));
	assert(!rules_barred("0771", RULES_ALLOW, out_allow, 1));
	assert(rules_barred("011", RULES_ALLOW, out_allow, 1));
	assert(rules_barred("011", RULES_ALLOW, out_allow, 0));

	out_mode = RULES_BLOCK;
	line_init(&l, &o);
	keys(&l, "0094771#");
	assert(!dialed[0] && l.state == LS_BUSY && busy == 1 && !l.last_out[0]);

	line_init(&l, &o);
	keys(&l, "0771#");
	assert(!strcmp(dialed, "0771"));

	line_init(&l, &o);
	strcpy(l.last_out, "0094771");
	keys(&l, "*66#");
	assert(!dialed[0] && l.state == LS_BUSY);

	line_init(&l, &o);
	strcpy(l.autodial, "+94771");
	line_hook(&l, 1, 0);
	assert(!dialed[0] && l.state == LS_BUSY);

	line_init(&l, &o);
	strcpy(l.hotline, "0094771");
	l.hotline_ms = 100;
	line_hook(&l, 1, 0);
	line_tick(&l, 200);
	assert(!dialed[0] && l.state == LS_BUSY);

	out_mode = RULES_ALLOW;
	line_init(&l, &o);
	keys(&l, "011#");
	assert(!dialed[0] && l.state == LS_BUSY);

	line_init(&l, &o);
	keys(&l, "112#");
	assert(!strcmp(dialed, "112"));
	line_init(&l, &o);
	keys(&l, "119#");
	assert(!strcmp(dialed, "119"));
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
	test_outgoing();
	test_service();
	test_quiet_codes();
	test_missed_tone();
	test_waiting_blocked();
	test_no_voicemail();
	test_hotline();
	test_autodial();
	test_timing();
	test_tones();
	return 0;
}
