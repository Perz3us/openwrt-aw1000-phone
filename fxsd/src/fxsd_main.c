// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2026 perz3us
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>
#include "bringup.h"
#include "cid.h"
#include "clcc.h"
#include "country.h"
#include "fxsdev.h"
#include "line.h"
#include "modem.h"
#include "pcmsock.h"
#include "si3226x_map.h"
#include "si_line.h"
#include "urc.h"
#include "voip.h"

#define POLL_MODEM_MS	500
#define POLL_HOOK_MS	1000
#define RING_ARM_MS	3000
#define HANG_S		30
#define STATE_FILE	"/var/run/fxsd.state"
#define CALL_LOG	"/etc/fxs/calls.log"
#define CALL_LOG_MAX	2000
#define CLOCK_VALID	1700000000L
#define PRUNE_MS	3600000UL
#define GUARD_FILE	"/etc/fxs/.starting"
#define GUARD_CLEAR_MS	60000
#define LED_DIR		"/sys/class/leds/"
#define LED_NIGHT	"/tmp/led-night-active"
#define SPEED_FILE	"/var/run/fxsd.speed"
#define BLOCK_FILE	"/var/run/fxsd.block"
#define LIST_MAX	32
#define PCM_SOCK	"/var/run/fxsd/pcm.sock"
#define VOIP_CHECK_MS	5000
#define DND_REPEAT_S	180
#define RING_PHONE_UA	2200

struct fxsd {
	struct slic s;
	struct line line;
	uint8_t ch;
	int ringing, audio, verbose, wb, bridge, sip_audio;
	int cid_mode, cid_pending, cid_before, test_ring, cad_phase, silent;
	int phone, ring_n, ring_quiet;
	long long ring_sum;
	int32_t vbat_mv, vline_mv, iloop_ua;
	unsigned long t_ring_arm, t_gap, t_meas, t_test, t_cad;
	long talk_since;
};

static unsigned int ring_cad[4] = { 2000, 4000, 0, 0 };
static unsigned long data_since;
static int data_redialed;

static volatile sig_atomic_t stop, ring_test;
static char own_number[CLCC_NUM_MAX];
static const char *led_name = "green:phone";
enum { LED_OFF, LED_ON, LED_SLOW, LED_FAST };
enum { LED_READY, LED_RINGING, LED_OFFHOOK };
static int led_mode[3] = { LED_ON, LED_FAST, LED_SLOW };
static int led_cur = -1;
static int keep_days;
static int dnd_from = -1, dnd_to = -1;
static int voice_off;
static int sip_on, client;

static void on_sig(int sig)
{
	(void)sig;
	stop = 1;
}

static void on_usr1(int sig)
{
	(void)sig;
	ring_test = 1;
}

static unsigned long now_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000UL + ts.tv_nsec / 1000000;
}

static void msg(int prio, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vsyslog(prio, fmt, ap);
	va_end(ap);
}

static void led_write(const char *file, const char *v)
{
	char path[128];
	FILE *f;

	snprintf(path, sizeof(path), LED_DIR "%s/%s", led_name, file);
	if ((f = fopen(path, "w"))) {
		fputs(v, f);
		fclose(f);
	}
}

static int led_read(const char *file, char *buf, size_t n)
{
	char path[128];
	FILE *f;
	size_t got;

	snprintf(path, sizeof(path), LED_DIR "%s/%s", led_name, file);
	if (!(f = fopen(path, "r")))
		return -1;
	got = fread(buf, 1, n - 1, f);
	buf[got] = 0;
	fclose(f);
	return 0;
}

static void led_set(int mode)
{
	if (!*led_name)
		return;
	if (mode == LED_SLOW || mode == LED_FAST) {
		const char *ms = mode == LED_FAST ? "120" : "600";

		led_write("trigger", "timer");
		led_write("delay_on", ms);
		led_write("delay_off", ms);
	} else {
		led_write("trigger", "none");
		led_write("brightness", mode == LED_ON ? "1" : "0");
	}
	led_cur = mode;
}

static void led_update(int mode, int verify)
{
	char buf[512];

	if (!*led_name)
		return;
	if (mode != led_cur) {
		led_set(mode);
		return;
	}
	if (!verify || led_read("trigger", buf, sizeof(buf)))
		return;
	if (mode == LED_SLOW || mode == LED_FAST) {
		if (!strstr(buf, "[timer]"))
			led_set(mode);
	} else if (!strstr(buf, "[none]") ||
		   led_read("brightness", buf, sizeof(buf)) || (atoi(buf) != 0) != (mode == LED_ON)) {
		led_set(mode);
	}
}

static int led_want(const struct fxsd *d)
{
	if (d->ringing || d->test_ring)
		return led_mode[LED_RINGING];
	if (!access(LED_NIGHT, F_OK))
		return LED_OFF;
	if (d->line.offhook || d->line.state != LS_IDLE)
		return led_mode[LED_OFFHOOK];
	return led_mode[LED_READY];
}

static int led_parse(const char *s)
{
	if (!strncmp(s, "slow", 4))
		return LED_SLOW;
	if (!strncmp(s, "fast", 4))
		return LED_FAST;
	if (!strncmp(s, "off", 3))
		return LED_OFF;
	return LED_ON;
}

static void led_modes_parse(const char *s)
{
	int i;

	for (i = 0; i < 3 && s; i++) {
		led_mode[i] = led_parse(s);
		if ((s = strchr(s, ',')))
			s++;
	}
}

static void at(const char *cmd)
{
	char r[512];

	if (modem_at(cmd, r, sizeof(r)) || !strstr(r, "OK"))
		msg(LOG_WARNING, "%s failed", cmd);
}

static void op_tone(void *c, enum line_tone t)
{
	struct fxsd *d = c;

	if (t == TONE_OFF) {
		si_tone_stop(&d->s, d->ch);
		return;
	}
	si_tone_stop(&d->s, d->ch);
	si_tone_setup(&d->s, d->ch, t == TONE_DIAL ? &aw1000_dialtone :
		      t == TONE_BUSY ? &aw1000_busytone :
		      t == TONE_WAITING ? &aw1000_waitingtone : &aw1000_ringbacktone);
	si_tone_start(&d->s, d->ch, t != TONE_DIAL);
}

static int same_number(const char *a, const char *b);

static int dnd_now(void)
{
	time_t t = time(NULL);
	struct tm tm;
	int m;

	if (dnd_from < 0 || dnd_from == dnd_to)
		return 0;
	localtime_r(&t, &tm);
	m = tm.tm_hour * 60 + tm.tm_min;
	return dnd_from < dnd_to ? m >= dnd_from && m < dnd_to : m >= dnd_from || m < dnd_to;
}

static int dnd_silence(struct fxsd *d)
{
	static char last[CLCC_NUM_MAX];
	static time_t t_last;
	time_t now = time(NULL);

	if (d->test_ring || d->line.recall || !dnd_now())
		return 0;
	if (*d->line.peer && *last && same_number(last, d->line.peer) && now - t_last < DND_REPEAT_S) {
		*last = 0;
		msg(LOG_INFO, "second call within %d minutes, ringing", DND_REPEAT_S / 60);
		return 0;
	}
	snprintf(last, sizeof(last), "%s", d->line.peer);
	t_last = now;
	msg(LOG_INFO, "do not disturb, not ringing");
	return 1;
}

static void ring_sample(struct fxsd *d)
{
	uint32_t raw;
	int32_t u;

	if (!d->ringing || d->silent || d->s.rdram(d->s.ctx, d->ch, RAM_MADC_ILOOP, &raw))
		return;
	u = si_iloop_ua(raw);
	d->ring_sum += u < 0 ? -u : u;
	d->ring_n++;
}

static void ring_done(struct fxsd *d)
{
	if (d->ring_n >= 20) {
		if (d->ring_sum / d->ring_n >= RING_PHONE_UA) {
			d->ring_quiet = 0;
			d->phone = 1;
		} else if (++d->ring_quiet >= 2 || d->test_ring) {
			if (d->phone != 0)
				msg(LOG_WARNING, "no ring current, check that a phone is plugged in");
			d->phone = 0;
		}
	}
	d->ring_n = 0;
	d->ring_sum = 0;
}

static void op_ring(void *c, int on)
{
	struct fxsd *d = c;
	uint8_t lf;

	if (on && (d->silent = dnd_silence(d))) {
		d->ringing = 1;
		d->cid_pending = 0;
		return;
	}
	if (!on && d->silent) {
		d->silent = 0;
		d->ringing = 0;
		return;
	}
	if (on) {
		if (d->cid_before && d->cid_mode != CID_OFF && !d->line.recall && !d->test_ring) {
			struct tm tm;
			time_t t = time(NULL);
			unsigned long t0 = now_ms();
			int e;

			localtime_r(&t, &tm);
			if (d->cid_before == 2 && d->cid_mode != CID_DTMF) {
				fxsdev_ring_arm(&d->s, RING_ARM_MS);
				e = cid_send_rpas(&d->s, d->ch, d->cid_mode, d->line.peer, &tm);
			} else {
				e = cid_send_before(&d->s, d->ch, d->cid_mode, d->line.peer, &tm);
			}
			if (e)
				msg(LOG_WARNING, "caller ID failed: %s", strerror(-e));
			else
				msg(LOG_INFO, "caller ID sent before ring in %lu ms", now_ms() - t0);
		}
		fxsdev_ring_arm(&d->s, RING_ARM_MS);
		d->cid_pending = d->cid_mode != CID_OFF && !d->cid_before;
		d->t_gap = 0;
		d->t_ring_arm = now_ms();
		d->cad_phase = 0;
		d->t_cad = d->t_ring_arm;
		si_linefeed(&d->s, d->ch, SI_LF_RING);
	} else {
		ring_done(d);
		if (!d->s.rd(d->s.ctx, d->ch, SI_LINEFEED, &lf) && (lf & 0x0f) == SI_LF_RING)
			si_linefeed(&d->s, d->ch, SI_LF_FWD);
		fxsdev_ring_arm(&d->s, 0);
	}
	d->ringing = on;
}

static void apply_bridge(struct fxsd *d)
{
	int want = d->sip_audio ? 2 : d->audio ? 1 : 0;
	uint32_t arg = 1u | (0u << 8) | (32u << 16) | (32u << 24) | 0x80000000u | (d->wb ? 0x40000000u : 0);

	if (want == d->bridge)
		return;
	if (d->bridge) {
		uint32_t st[6];

		if (!fxsdev_stat(&d->s, st))
			msg(LOG_INFO, "audio moved %u, resync %u, ticks %u, status 0x%x, peak modem %u, phone %u",
			    st[0], st[1], st[2], st[3], st[4], st[5]);
	}
	if (d->bridge && want)
		fxsdev_bridge(&d->s, 0);
	if (fxsdev_bridge(&d->s, want ? arg | (want == 2 ? 0x80u : 0) : 0))
		msg(LOG_ERR, "audio bridge %s failed", want ? "start" : "stop");
	d->bridge = want;
	pcmsock_live(want == 2);
}

static void op_audio(void *c, int on)
{
	struct fxsd *d = c;

	d->audio = on;
	apply_bridge(d);
}

static void op_dial(void *c, const char *num)
{
	struct fxsd *d = c;
	char cmd[64];
	size_t n = 3;

	memcpy(cmd, "ATD", 3);
	for (; *num && n + 2 < sizeof(cmd); num++)
		if (strchr("0123456789*#+", *num))
			cmd[n++] = *num;
	cmd[n] = 0;
	if (client) {
		if (voip_request("originate", cmd + 3)) {
			msg(LOG_WARNING, "QModem voice did not accept the call");
			d->line.seen_call = 1;
		}
		return;
	}
	cmd[n++] = ';';
	cmd[n] = 0;
	at(cmd);
}

static void op_answer(void *c)
{
	(void)c;
	if (!client)
		at("ATA");
	else if (voip_request("answer", NULL))
		msg(LOG_WARNING, "QModem voice did not accept the answer");
}

static void op_hangup(void *c)
{
	struct fxsd *d = c;

	if (!client)
		at("AT+CHUP");
	else if (voip_request(d->line.state == LS_IDLE || d->line.state == LS_RINGING ? "reject" : "hangup", NULL))
		msg(LOG_WARNING, "QModem voice did not accept the hangup");
}

static void op_chld(void *c, const char *arg)
{
	char cmd[32];

	(void)c;
	snprintf(cmd, sizeof(cmd), "AT+CHLD=%s", arg);
	at(cmd);
}

static struct { char code[4]; char num[CLCC_NUM_MAX]; } speed[LIST_MAX];
static char block[LIST_MAX][CLCC_NUM_MAX];
static int nspeed, nblock;

static void load_lists(void)
{
	char buf[96];
	FILE *f;

	if ((f = fopen(SPEED_FILE, "r"))) {
		while (nspeed < LIST_MAX && fgets(buf, sizeof(buf), f))
			if (sscanf(buf, "%3s %31s", speed[nspeed].code, speed[nspeed].num) == 2)
				nspeed++;
		fclose(f);
	}
	if ((f = fopen(BLOCK_FILE, "r"))) {
		while (nblock < LIST_MAX && fgets(buf, sizeof(buf), f))
			if (sscanf(buf, "%31s", block[nblock]) == 1)
				nblock++;
		fclose(f);
	}
}

static const char *op_speed(void *c, const char *code)
{
	int i;

	(void)c;
	for (i = 0; i < nspeed; i++)
		if (!strcmp(code, speed[i].code))
			return speed[i].num;
	return NULL;
}

static size_t digits_of(const char *s, char *out)
{
	size_t n = 0;

	for (; *s && n + 1 < CLCC_NUM_MAX; s++)
		if (*s >= '0' && *s <= '9')
			out[n++] = *s;
	out[n] = 0;
	return n;
}

static int same_number(const char *a, const char *b)
{
	char x[CLCC_NUM_MAX], y[CLCC_NUM_MAX];
	size_t nx = digits_of(a, x), ny = digits_of(b, y);

	if (nx >= 9 && ny >= 9)
		return !strcmp(x + nx - 9, y + ny - 9);
	return nx && !strcmp(x, y);
}

static int op_blocked(void *c, const char *num)
{
	int i;

	(void)c;
	for (i = 0; i < nblock; i++)
		if (!strcmp(block[i], "private") ? !*num : *num && same_number(num, block[i]))
			return 1;
	return 0;
}

static void trim_log(void)
{
	static char lines[CALL_LOG_MAX][96];
	char buf[96];
	unsigned n = 0, total = 0, i, first;
	time_t now = time(NULL);
	long cutoff = keep_days > 0 && now > CLOCK_VALID ? (long)now - keep_days * 86400L : 0;
	FILE *f = fopen(CALL_LOG, "r");

	if (!f)
		return;
	while (fgets(buf, sizeof(buf), f)) {
		total++;
		if (cutoff && atol(buf) < cutoff)
			continue;
		snprintf(lines[n++ % CALL_LOG_MAX], sizeof(lines[0]), "%s", buf);
	}
	fclose(f);
	if (n == total && n <= CALL_LOG_MAX)
		return;
	f = fopen(CALL_LOG ".tmp", "w");
	if (!f)
		return;
	first = n > CALL_LOG_MAX ? n - CALL_LOG_MAX : 0;
	for (i = first; i < n; i++)
		fputs(lines[i % CALL_LOG_MAX], f);
	fclose(f);
	rename(CALL_LOG ".tmp", CALL_LOG);
}

static void op_log(void *c, const char *what, const char *num)
{
	FILE *f = fopen(CALL_LOG, "a");
	long sz = 0;

	(void)c;
	msg(LOG_INFO, "call %s", what);
	if (f) {
		fprintf(f, "%ld %s %s\n", (long)time(NULL), what, num ? num : "");
		sz = ftell(f);
		fclose(f);
	}
	if (sz > CALL_LOG_MAX * 48)
		trim_log();
}

static int op_elsewhere(void *c)
{
	(void)c;
	return client && voip_snap()->answered && voip_snap()->owner == VO_OTHER;
}

static struct line_ops ops = {
	.tone = op_tone, .ring = op_ring, .audio = op_audio, .dial = op_dial,
	.answer = op_answer, .hangup = op_hangup, .chld = op_chld, .log = op_log,
	.speed = op_speed, .blocked = op_blocked, .elsewhere = op_elsewhere,
};

static void put_state(const char *state, const char *peer, int offhook)
{
	FILE *f = fopen(STATE_FILE ".tmp", "w");

	if (!f)
		return;
	fprintf(f, "state=%s\npeer=%s\noffhook=%d\n", state, peer, offhook);
	fclose(f);
	rename(STATE_FILE ".tmp", STATE_FILE);
}

static void write_state(struct fxsd *d)
{
	FILE *f = fopen(STATE_FILE ".tmp", "w");

	if (!f)
		return;
	fprintf(f, "state=%s\npeer=%s\noffhook=%d\nvbat=%d\nvline=%d\niloop=%d\nown=%s\ntest=%d\nvoiceoff=%d\nsince=%ld\nhd=%d\nsip=%d\nsipcall=%d\ndnd=%d\nsilent=%d\nphone=%d\n",
		line_state_name(d->line.state), d->line.peer, d->line.offhook,
		d->vbat_mv, d->vline_mv, d->iloop_ua, own_number, d->test_ring, voice_off, d->talk_since, d->wb,
		sip_on ? (client ? 2 : 1) : 0, d->sip_audio, dnd_now(), d->silent, d->phone);
	fclose(f);
	rename(STATE_FILE ".tmp", STATE_FILE);
}

static void measure(struct fxsd *d)
{
	uint32_t vbat, vtip, vring, iloop;

	if (d->s.rdram(d->s.ctx, d->ch, RAM_MADC_VBAT, &vbat) ||
	    d->s.rdram(d->s.ctx, d->ch, RAM_MADC_VTIPC, &vtip) ||
	    d->s.rdram(d->s.ctx, d->ch, RAM_MADC_VRINGC, &vring) ||
	    d->s.rdram(d->s.ctx, d->ch, RAM_MADC_ILOOP, &iloop))
		return;
	d->vbat_mv = si_vbat_mv(vbat);
	d->vline_mv = si_vbat_mv(vtip) - si_vbat_mv(vring);
	d->iloop_ua = si_iloop_ua(iloop);
}

static void test_ring_tick(struct fxsd *d, unsigned long now)
{
	int stop_test = 0;

	if (ring_test) {
		ring_test = 0;
		if (d->test_ring) {
			stop_test = 1;
		} else if (d->line.state == LS_IDLE && !d->ringing && !d->line.offhook) {
			d->test_ring = 1;
			op_ring(d, 1);
			d->cid_pending = 0;
			d->t_test = now;
			msg(LOG_INFO, "test ring");
			write_state(d);
		}
	}
	if (d->test_ring && (stop_test || now - d->t_test >= 10000 || d->line.state != LS_IDLE)) {
		if (d->line.state != LS_RINGING)
			op_ring(d, 0);
		d->test_ring = 0;
		write_state(d);
	}
}

static void read_own_number(void)
{
	char r[1024];
	const char *p;
	size_t n = 0;

	if (modem_at("AT+CNUM", r, sizeof(r)) || !(p = strstr(r, "+CNUM:")) || !(p = strchr(p, ',')))
		return;
	for (p++; *p == '"' || *p == '\\' || *p == ' '; p++)
		;
	while (n + 1 < sizeof(own_number) && ((*p >= '0' && *p <= '9') || *p == '+'))
		own_number[n++] = *p++;
	own_number[n] = 0;
}

static int digits(const char *s)
{
	for (; *s; s++)
		if (*s < '0' || *s > '9')
			return 0;
	return 1;
}

static void local_numbers(const char *opt)
{
	char cc[8] = "", tr[8] = "auto", r[1024];
	const struct country *c = NULL;
	const char *p;

	if (!opt || sscanf(opt, "%7[^,],%7s", cc, tr) < 1 || !strcmp(cc, "off"))
		return;
	if (!modem_at("AT+CIMI", r, sizeof(r)) && (p = strstr(r, "\"response\""))) {
		for (; *p && !(p[0] >= '0' && p[0] <= '9' && p[1] >= '0' && p[1] <= '9' &&
				    p[2] >= '0' && p[2] <= '9'); p++)
			;
		if (*p)
			c = country_find((p[0] - '0') * 100 + (p[1] - '0') * 10 + (p[2] - '0'));
	}
	if (!strcmp(cc, "auto")) {
		if (!c) {
			msg(LOG_WARNING, "could not find the country of the SIM, set it in Settings to show local numbers");
			return;
		}
		snprintf(cc, sizeof(cc), "%s", c->cc);
	}
	if (!strcmp(tr, "auto"))
		snprintf(tr, sizeof(tr), "%s", c && !strcmp(c->cc, cc) ? c->trunk : "0");
	else if (!strcmp(tr, "none"))
		*tr = 0;
	if (!digits(cc) || !digits(tr)) {
		msg(LOG_WARNING, "bad caller ID country or local prefix");
		return;
	}
	cid_local(cc, tr, c && !strcmp(c->cc, cc) ? c->intl : "00");
	msg(LOG_INFO, "caller ID shows local numbers for +%s", cc);
}

static void check_voice(void)
{
	char r[1024];
	const char *p;

	if (modem_at("AT+QCFG=\"call_control\"", r, sizeof(r)) || !(p = strstr(r, "+QCFG:")) ||
	    !(p = strstr(p, "call_control")))
		return;
	while (*p && (*p < '0' || *p > '9'))
		p++;
	voice_off = *p == '1';
	if (voice_off)
		msg(LOG_WARNING, "voice calls are turned off in the modem; run AT+QCFG=\"call_control\",0,0 and restart the modem");
}

static void switch_rate(int hd);

static int wait_modem(int hd)
{
	char r[512];
	int last = 2, q, tried = 0, fails = 0;

	while (!stop) {
		q = modem_at("AT+QDAI?", r, sizeof(r)) ? -1 : qdai_is_slave(r);
		if (q == 1)
			return 0;
		if (q < 0 && modem_port_ok() && ++fails >= 10) {
			msg(LOG_WARNING, "at-daemon does not answer, restarting it");
			modem_heal();
			fails = -10;
		} else if (q >= 0) {
			fails = 0;
		}
		if (q == 0 && !tried) {
			msg(LOG_INFO, "modem PCM is in master mode, switching it to slave");
			tried = 1;
			switch_rate(hd);
			continue;
		}
		if (q != last) {
			if (q == 0)
				msg(LOG_ERR, "modem PCM is in master mode; set it to slave (AT+QDAI) and restart the modem");
			else
				msg(LOG_INFO, "waiting for the modem");
			put_state(q == 0 ? "modem_master" : "waiting_modem", "", 0);
			last = q;
		}
		sleep(q == 0 ? 30 : 3);
	}
	return -1;
}

static int modem_rate(void)
{
	char r[512];

	return modem_at("AT+QDAI?", r, sizeof(r)) ? -1 : qdai_rate(r);
}

static void switch_rate(int hd)
{
	struct clcc_call c[CLCC_MAX];
	char r[1024], cmd[48];

	while (!stop && !modem_at("AT+CLCC", r, sizeof(r)) && clcc_parse_all(r, c, CLCC_MAX) > 0)
		sleep(5);
	if (stop)
		return;
	msg(LOG_INFO, "setting modem audio to %s kHz and restarting the modem", hd ? "16" : "8");
	put_state("modem_restart", "", 0);
	snprintf(cmd, sizeof(cmd), "AT+QDAI=x,1,0,4,0,%d,1,1", hd);
	at(cmd);
	data_since = now_ms();
	data_redialed = 0;
	if (modem_restart())
		msg(LOG_WARNING, "modem did not come back after the restart");
}

static void data_tick(unsigned long now)
{
	if (!data_since || now - data_since < 5000)
		return;
	if (modem_data_up()) {
		msg(LOG_INFO, "mobile data back %lu s after the modem restart", (now - data_since) / 1000);
		data_since = 0;
	} else if (now - data_since >= 600000) {
		msg(LOG_WARNING, "no mobile data 10 minutes after the modem restart");
		data_since = 0;
	} else if (now - data_since >= 180000 && !data_redialed) {
		msg(LOG_WARNING, "no mobile data 3 minutes after the modem restart, dialing again");
		modem_redial();
		data_redialed = 1;
	}
}

static void check_hook(struct fxsd *d, unsigned long now)
{
	uint8_t lcr;

	if (!d->s.rd(d->s.ctx, d->ch, SI_LCRRTP, &lcr))
		line_hook(&d->line, !!(lcr & 2), now);
}

static void handle_irq(struct fxsd *d, unsigned long now)
{
	uint8_t irq[4];
	char digit;

	if (si_irq_read(&d->s, d->ch, irq))
		return;
	if (irq[1] & (SI_IRQ2_LCR | SI_IRQ2_RTP))
		check_hook(d, now);
	if ((irq[1] & SI_IRQ2_DTMF) && !si_dtmf_digit(&d->s, d->ch, &digit))
		line_digit(&d->line, digit, now);
	if (irq[2] & (SI_IRQ3_HVIC | SI_IRQ3_THERM))
		msg(LOG_WARNING, "SLIC power alarm 0x%02x", irq[2]);
}

static void poll_cid(struct fxsd *d, unsigned long now)
{
	struct tm tm;
	time_t t;
	uint8_t lf;
	int e;

	if (!d->ringing || !d->cid_pending)
		return;
	if (ring_cad[2]) {
		if (d->cad_phase != 3 || now - d->t_cad < 400)
			return;
		goto send;
	}
	if (d->s.rd(d->s.ctx, d->ch, SI_LINEFEED, &lf))
		return;
	if ((lf & 0xf0) == 0x40 || (lf & 0x0f) != SI_LF_RING) {
		if ((lf & 0xf0) == 0x40)
			d->cid_pending = 2;
		d->t_gap = 0;
		return;
	}
	if (d->cid_pending != 2)
		return;
	if (!d->t_gap) {
		d->t_gap = now;
		return;
	}
	if (now - d->t_gap < 600)
		return;
send:
	d->cid_pending = 0;
	fxsdev_ring_arm(&d->s, RING_ARM_MS);
	d->t_ring_arm = now;
	t = time(NULL);
	localtime_r(&t, &tm);
	if ((e = cid_send(&d->s, d->ch, d->cid_mode, d->line.peer, &tm)))
		msg(LOG_WARNING, "caller ID failed: %s", strerror(-e));
	else
		msg(LOG_INFO, "caller ID sent in %lu ms", now_ms() - now);
}

static void ring_cad_tick(struct fxsd *d, unsigned long now)
{
	if (!ring_cad[2] || d->silent || !(d->ringing || d->test_ring) || now - d->t_cad < ring_cad[d->cad_phase])
		return;
	d->cad_phase = (d->cad_phase + 1) & 3;
	d->t_cad = now;
	si_linefeed(&d->s, d->ch, d->cad_phase & 1 ? SI_LF_FWD : SI_LF_RING);
}

static void dnd_parse(const char *s)
{
	unsigned int h1, m1, h2, m2;

	if (sscanf(s, "%u:%u,%u:%u", &h1, &m1, &h2, &m2) != 4 || h1 > 23 || h2 > 23 || m1 > 59 || m2 > 59)
		return;
	dnd_from = h1 * 60 + m1;
	dnd_to = h2 * 60 + m2;
}

static void ring_parse(const char *s)
{
	unsigned int v[4] = { 0, 0, 0, 0 };
	int n = sscanf(s, "%u,%u,%u,%u", &v[0], &v[1], &v[2], &v[3]), i;

	if (n != 2 && n != 4)
		return;
	for (i = 0; i < n; i++)
		if (v[i] < 100 || v[i] > 8000)
			return;
	memcpy(ring_cad, v, sizeof(ring_cad));
}

static uint16_t ring_ticks(unsigned int ms)
{
	return ms >= 8191 ? 0xffff : ms * 8;
}

static void poll_modem(struct fxsd *d, unsigned long now)
{
	char r[1024];
	struct clcc_call c[CLCC_MAX];

	if (client) {
		const struct voip_snap *v = voip_snap();
		int sip = voip_sip_audio(v);

		line_modem(&d->line, c, voip_calls(v, c), now);
		if (sip != d->sip_audio) {
			d->sip_audio = sip;
			apply_bridge(d);
			write_state(d);
		}
		return;
	}
	if (modem_at("AT+CLCC", r, sizeof(r)) || !strstr(r, "OK"))
		return;
	line_modem(&d->line, c, clcc_parse_all(r, c, CLCC_MAX), now);
}

static void sip_tick(struct fxsd *d, unsigned long now, unsigned long *t_check)
{
	const struct voip_snap *v = voip_snap();
	int want;

	if (!sip_on)
		return;
	voip_events();
	if (now - *t_check >= VOIP_CHECK_MS) {
		*t_check = now;
		if (voip_connected())
			voip_refresh();
		else
			voip_connect();
	}
	want = voip_connected() && v->enabled && v->st != VS_DISABLED;
	if (want != client && d->line.state == LS_IDLE && !d->ringing && !d->test_ring) {
		client = want;
		ops.chld = client ? NULL : op_chld;
		d->line.gone_ms = client ? 0 : 3000;
		d->sip_audio = 0;
		apply_bridge(d);
		msg(LOG_INFO, client ? "SIP calling on, calls go through QModem voice" :
		    "QModem voice is not running, using the modem directly");
		write_state(d);
	}
	if (client && voip_changed())
		poll_modem(d, now);
}

static int setup(struct fxsd *d, const struct si_patch *p, int gtx, int grx)
{
	struct si_pwr_report rep[2];
	struct si_imp imp = aw1000_imp_600;
	struct si_ring ring = aw1000_ring;
	uint16_t on = ring_ticks(ring_cad[2] ? 8191 : ring_cad[0]), off = ring_ticks(ring_cad[1]);
	int e;

	imp.txgain_db = gtx;
	imp.rxgain_db = grx;
	ring.talo = on & 0xff;
	ring.tahi = on >> 8;
	ring.tilo = off & 0xff;
	ring.tihi = off >> 8;
	if ((e = fxsdev_pcm(&d->s, 1)) || (e = fxsdev_reset(&d->s, 1)))
		return e;
	d->s.delay(d->s.ctx, 50);
	if ((e = fxsdev_reset(&d->s, 0)))
		return e;
	d->s.delay(d->s.ctx, 100);
	if ((e = si_bringup(&d->s, &aw1000_gen_cfg, p, 1u << d->ch, rep)))
		return e;
	msg(LOG_INFO, "SLIC up, VBAT %d mV", rep[d->ch].vbat_final_mv);
	if ((e = si_dcfeed_setup(&d->s, d->ch, &aw1000_dcfeed)) ||
	    (e = si_linefeed(&d->s, d->ch, SI_LF_FWD)) ||
	    (e = si_zsynth_setup(&d->s, d->ch, &imp)) ||
	    (e = si_ring_setup(&d->s, d->ch, &ring, &aw1000_gen_cfg)) ||
	    (e = si_pcm_setup(&d->s, d->ch, 31, 32)) ||
	    (d->wb && (e = si_pcm_wideband(&d->s, d->ch))) ||
	    (e = si_irq_enable(&d->s, d->ch, &aw1000_gen_cfg)))
		return e;
	return fxsdev_ack(&d->s);
}

static int usage(void)
{
	fprintf(stderr, "usage: fxsd -p PATCH [-c CHANNEL] [-g TXDB,RXDB] [-a ATPORT] [-l VOLUME] [-r RXGAIN] [-e LED] [-m READY,RINGING,OFFHOOK] [-b 0|1] [-i bellcore|etsi|dtmf|off] [-t before|after] [-k DAYS] [-w 0|1] [-s 0|1] [-n ON,OFF[,ON,OFF]] [-f] [-v]\n");
	return 2;
}

int main(int argc, char **argv)
{
	const char *patch = NULL, *numfmt = NULL;
	struct fxsd d = { .ch = 1, .cid_mode = CID_BELLCORE, .phone = -1 };
	struct si_patch p;
	unsigned long t_modem = 0, t_hook = 0, t_ready, t_prune, t_voip, t_urc, t_alive = 0;
	int guard = 1;
	enum line_state last = LS_BUSY + 1;
	int gtx = -6, grx = 0, fg = 0, vol = -1, rxg = -1, o, e, q;
	int ringback = 1, hd = 0;
	char r[512];

	while ((o = getopt(argc, argv, "p:c:g:a:l:r:e:m:b:i:t:k:w:s:n:q:u:fv")) != -1) {
		switch (o) {
		case 'p': patch = optarg; break;
		case 'c': d.ch = atoi(optarg) & 1; break;
		case 'g': sscanf(optarg, "%d,%d", &gtx, &grx); break;
		case 'a': modem_set_port(optarg); break;
		case 'l': vol = atoi(optarg); break;
		case 'r': rxg = atoi(optarg); break;
		case 'e': led_name = optarg; break;
		case 'm': led_modes_parse(optarg); break;
		case 'b': ringback = atoi(optarg); break;
		case 'i': d.cid_mode = cid_mode_parse(optarg); break;
		case 't': d.cid_before = !strcmp(optarg, "rpas") ? 2 : !strcmp(optarg, "before"); break;
		case 'k': keep_days = atoi(optarg); break;
		case 'w': hd = atoi(optarg) == 1; break;
		case 's': sip_on = atoi(optarg) == 1; break;
		case 'n': ring_parse(optarg); break;
		case 'q': dnd_parse(optarg); break;
		case 'u': numfmt = optarg; break;
		case 'f': fg = 1; break;
		case 'v': d.verbose = 1; break;
		default: return usage();
		}
	}
	if (!patch)
		return usage();
	openlog("fxsd", LOG_PID | (fg ? LOG_PERROR : 0), LOG_DAEMON);
	if ((e = si_patch_load_file(patch, &p))) {
		msg(LOG_ERR, "patch %s: %s", patch, strerror(-e));
		return 1;
	}
	signal(SIGINT, on_sig);
	signal(SIGTERM, on_sig);
	signal(SIGUSR1, on_usr1);
	signal(SIGPIPE, SIG_IGN);
	if (wait_modem(hd)) {
		unlink(STATE_FILE);
		return 0;
	}
	if ((q = modem_rate()) >= 0 && q != hd) {
		switch_rate(hd);
		if (wait_modem(hd)) {
			unlink(STATE_FILE);
			return 0;
		}
		q = modem_rate();
	}
	d.wb = q == 1;
	if (q >= 0 && d.wb != hd)
		msg(LOG_WARNING, "modem audio is still %s kHz, could not change it", d.wb ? "16" : "8");
	read_own_number();
	local_numbers(numfmt);
	check_voice();
	if (vol >= 0 && vol <= 5) {
		snprintf(r, sizeof(r), "AT+CLVL=%d", vol);
		at(r);
	}
	if (rxg >= 0 && rxg <= 65535) {
		snprintf(r, sizeof(r), "AT+QRXGAIN=%d", rxg);
		at(r);
	}
	if ((e = fxsdev_open(&d.s, "/dev/fxs0"))) {
		msg(LOG_ERR, "/dev/fxs0: %s", strerror(-e));
		return 1;
	}
	if ((e = setup(&d, &p, gtx, grx))) {
		msg(LOG_ERR, "SLIC setup failed: %s", strerror(-e));
		goto out;
	}
	si_patch_free(&p);
	load_lists();
	ops.ctx = &d;
	line_init(&d.line, &ops);
	d.line.ringback = ringback;
	if (sip_on && (e = pcmsock_start(PCM_SOCK, "/dev/fxs0audio", d.wb))) {
		msg(LOG_ERR, "SIP calling needs the newer kernel module (%s), turning it off", strerror(-e));
		sip_on = 0;
		e = 0;
	}
	msg(LOG_INFO, "ready on channel %u, modem AT port %s%s", d.ch, modem_port(),
	    sip_on ? ", SIP calling on (experimental)" : "");
	trim_log();
	led_update(led_want(&d), 0);
	t_ready = now_ms();
	t_prune = t_ready;
	t_voip = t_ready - VOIP_CHECK_MS;
	t_urc = t_ready - VOIP_CHECK_MS;

	while (!stop) {
		unsigned long now = now_ms();
		int w = fxsdev_wait(&d.s, (d.ringing || d.test_ring) && (d.cid_pending || ring_cad[2]) ? 20 : 100);

		now = now_ms();
		if (w > 0) {
			handle_irq(&d, now);
			fxsdev_ack(&d.s);
		}
		if (now - t_hook >= POLL_HOOK_MS) {
			check_hook(&d, now);
			if (d.line.offhook) {
				d.phone = 1;
				d.ring_quiet = 0;
			}
			t_hook = now;
		}
		sip_tick(&d, now, &t_voip);
		if (now - t_urc >= VOIP_CHECK_MS) {
			urc_start();
			t_urc = now;
		}
		if (urc_released()) {
			line_released(&d.line);
			t_modem = now - POLL_MODEM_MS;
		}
		if (now - t_modem >= POLL_MODEM_MS) {
			poll_modem(&d, now);
			t_modem = now;
		}
		ring_sample(&d);
		poll_cid(&d, now);
		ring_cad_tick(&d, now);
		data_tick(now);
		test_ring_tick(&d, now);
		if (now - d.t_meas >= 2000) {
			measure(&d);
			write_state(&d);
			led_update(led_want(&d), 1);
			d.t_meas = now;
		}
		if (d.ringing && !d.silent && now - d.t_ring_arm >= 1000) {
			fxsdev_ring_arm(&d.s, RING_ARM_MS);
			d.t_ring_arm = now;
		}
		if (now - t_alive >= 1000) {
			alarm(HANG_S);
			t_alive = now;
		}
		line_tick(&d.line, now);
		led_update(led_want(&d), 0);
		if (keep_days > 0 && now - t_prune >= PRUNE_MS) {
			trim_log();
			t_prune = now;
		}
		if (guard && now - t_ready >= GUARD_CLEAR_MS) {
			unlink(GUARD_FILE);
			sync();
			guard = 0;
		}
		if (d.line.state != last) {
			last = d.line.state;
			d.talk_since = last == LS_TALKING ? (long)time(NULL) : 0;
			if (d.verbose)
				msg(LOG_DEBUG, "state %s", line_state_name(last));
			write_state(&d);
		}
	}
	alarm(0);
	msg(LOG_INFO, "stopping");
	if (d.line.state == LS_CALLING || d.line.state == LS_TALKING)
		op_hangup(&d);
out:
	led_set(LED_OFF);
	fxsdev_ring_arm(&d.s, 0);
	fxsdev_bridge(&d.s, 0);
	pcmsock_stop();
	voip_disconnect();
	urc_stop();
	si_power_down(&d.s, d.ch);
	fxsdev_reset(&d.s, 1);
	fxsdev_pcm(&d.s, 0);
	fxsdev_close(&d.s);
	unlink(STATE_FILE);
	return e ? 1 : 0;
}
