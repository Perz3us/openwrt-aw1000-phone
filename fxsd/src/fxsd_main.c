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
#include "clcc.h"
#include "fxsdev.h"
#include "line.h"
#include "modem.h"
#include "si3226x_map.h"
#include "si_line.h"

#define POLL_MODEM_MS	500
#define POLL_HOOK_MS	1000
#define RING_ARM_MS	3000
#define STATE_FILE	"/var/run/fxsd.state"
#define CALL_LOG	"/etc/fxs/calls.log"
#define CALL_LOG_MAX	200
#define GUARD_FILE	"/etc/fxs/.starting"
#define GUARD_CLEAR_MS	60000

struct fxsd {
	struct slic s;
	struct line line;
	uint8_t ch;
	int ringing, audio, verbose;
	unsigned long t_ring_arm;
};

static volatile sig_atomic_t stop;

static void on_sig(int sig)
{
	(void)sig;
	stop = 1;
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
	si_tone_setup(&d->s, d->ch, t == TONE_DIAL ? &aw1000_dialtone : &aw1000_busytone);
	si_tone_start(&d->s, d->ch, t == TONE_BUSY);
}

static void op_ring(void *c, int on)
{
	struct fxsd *d = c;
	uint8_t lf;

	if (on) {
		fxsdev_ring_arm(&d->s, RING_ARM_MS);
		d->t_ring_arm = now_ms();
		si_linefeed(&d->s, d->ch, SI_LF_RING);
	} else {
		if (!d->s.rd(d->s.ctx, d->ch, SI_LINEFEED, &lf) && (lf & 0x0f) == SI_LF_RING)
			si_linefeed(&d->s, d->ch, SI_LF_FWD);
		fxsdev_ring_arm(&d->s, 0);
	}
	d->ringing = on;
}

static void op_audio(void *c, int on)
{
	struct fxsd *d = c;

	if (on == d->audio)
		return;
	if (fxsdev_bridge(&d->s, on ? (1u | (0u << 8) | (32u << 16) | (32u << 24) | 0x80000000u) : 0))
		msg(LOG_ERR, "audio bridge %s failed", on ? "start" : "stop");
	d->audio = on;
}

static void op_dial(void *c, const char *num)
{
	char cmd[64];

	(void)c;
	snprintf(cmd, sizeof(cmd), "ATD%s;", num);
	at(cmd);
}

static void op_answer(void *c)
{
	(void)c;
	at("ATA");
}

static void op_hangup(void *c)
{
	(void)c;
	at("AT+CHUP");
}

static void trim_log(void)
{
	static char lines[CALL_LOG_MAX][96];
	char buf[96];
	unsigned n = 0, i;
	FILE *f = fopen(CALL_LOG, "r");

	if (!f)
		return;
	while (fgets(buf, sizeof(buf), f))
		snprintf(lines[n++ % CALL_LOG_MAX], sizeof(lines[0]), "%s", buf);
	fclose(f);
	if (n <= CALL_LOG_MAX)
		return;
	f = fopen(CALL_LOG ".tmp", "w");
	if (!f)
		return;
	for (i = n - CALL_LOG_MAX; i < n; i++)
		fputs(lines[i % CALL_LOG_MAX], f);
	fclose(f);
	rename(CALL_LOG ".tmp", CALL_LOG);
}

static void op_log(void *c, const char *what, const char *num)
{
	FILE *f = fopen(CALL_LOG, "a");
	long sz = 0;

	(void)c;
	msg(LOG_INFO, "call %s %s", what, num ? num : "");
	if (f) {
		fprintf(f, "%ld %s %s\n", (long)time(NULL), what, num ? num : "");
		sz = ftell(f);
		fclose(f);
	}
	if (sz > CALL_LOG_MAX * 48)
		trim_log();
}

static struct line_ops ops = {
	op_tone, op_ring, op_audio, op_dial, op_answer, op_hangup, op_log, NULL,
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
	put_state(line_state_name(d->line.state), d->line.peer, d->line.offhook);
}

static int wait_modem(void)
{
	char r[512];
	int last = 2, q;

	while (!stop) {
		q = modem_at("AT+QDAI?", r, sizeof(r)) ? -1 : qdai_is_slave(r);
		if (q == 1)
			return 0;
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
	if ((irq[1] & SI_IRQ2_DTMF) && !si_dtmf_digit(&d->s, d->ch, &digit)) {
		if (d->verbose)
			msg(LOG_DEBUG, "digit %c", digit);
		line_digit(&d->line, digit, now);
	}
	if (irq[2] & (SI_IRQ3_HVIC | SI_IRQ3_THERM))
		msg(LOG_WARNING, "SLIC power alarm 0x%02x", irq[2]);
}

static void poll_modem(struct fxsd *d, unsigned long now)
{
	char r[1024];
	struct clcc_call c;

	if (modem_at("AT+CLCC", r, sizeof(r)) || !strstr(r, "OK"))
		return;
	line_modem(&d->line, clcc_parse_voice(r, &c) ? &c : NULL, now);
}

static int setup(struct fxsd *d, const struct si_patch *p, int gtx, int grx)
{
	struct si_pwr_report rep[2];
	struct si_imp imp = aw1000_imp_600;
	int e;

	imp.txgain_db = gtx;
	imp.rxgain_db = grx;
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
	    (e = si_ring_setup(&d->s, d->ch, &aw1000_ring, &aw1000_gen_cfg)) ||
	    (e = si_pcm_setup(&d->s, d->ch, 31, 32)) ||
	    (e = si_irq_enable(&d->s, d->ch, &aw1000_gen_cfg)))
		return e;
	return fxsdev_ack(&d->s);
}

static int usage(void)
{
	fprintf(stderr, "usage: fxsd -p PATCH [-c CHANNEL] [-g TXDB,RXDB] [-a ATPORT] [-l VOLUME] [-r RXGAIN] [-f] [-v]\n");
	return 2;
}

int main(int argc, char **argv)
{
	const char *patch = NULL;
	struct fxsd d = { .ch = 1 };
	struct si_patch p;
	unsigned long t_modem = 0, t_hook = 0, t_ready;
	int guard = 1;
	enum line_state last = LS_BUSY + 1;
	int gtx = -6, grx = 0, fg = 0, vol = -1, rxg = -1, o, e;
	char r[512];

	while ((o = getopt(argc, argv, "p:c:g:a:l:r:fv")) != -1) {
		switch (o) {
		case 'p': patch = optarg; break;
		case 'c': d.ch = atoi(optarg) & 1; break;
		case 'g': sscanf(optarg, "%d,%d", &gtx, &grx); break;
		case 'a': modem_set_port(optarg); break;
		case 'l': vol = atoi(optarg); break;
		case 'r': rxg = atoi(optarg); break;
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
	signal(SIGPIPE, SIG_IGN);
	if (wait_modem()) {
		unlink(STATE_FILE);
		return 0;
	}
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
	ops.ctx = &d;
	line_init(&d.line, &ops);
	msg(LOG_INFO, "ready on channel %u", d.ch);
	t_ready = now_ms();

	while (!stop) {
		unsigned long now = now_ms();
		int w = fxsdev_wait(&d.s, 100);

		now = now_ms();
		if (w > 0) {
			handle_irq(&d, now);
			fxsdev_ack(&d.s);
		}
		if (now - t_hook >= POLL_HOOK_MS) {
			check_hook(&d, now);
			t_hook = now;
		}
		if (now - t_modem >= POLL_MODEM_MS) {
			poll_modem(&d, now);
			t_modem = now;
		}
		if (d.ringing && now - d.t_ring_arm >= 1000) {
			fxsdev_ring_arm(&d.s, RING_ARM_MS);
			d.t_ring_arm = now;
		}
		line_tick(&d.line, now);
		if (guard && now - t_ready >= GUARD_CLEAR_MS) {
			unlink(GUARD_FILE);
			sync();
			guard = 0;
		}
		if (d.line.state != last) {
			last = d.line.state;
			if (d.verbose)
				msg(LOG_DEBUG, "state %s", line_state_name(last));
			write_state(&d);
		}
	}
	msg(LOG_INFO, "stopping");
	if (d.line.state == LS_CALLING || d.line.state == LS_TALKING)
		at("AT+CHUP");
out:
	fxsdev_ring_arm(&d.s, 0);
	fxsdev_bridge(&d.s, 0);
	si_power_down(&d.s, d.ch);
	fxsdev_reset(&d.s, 1);
	fxsdev_pcm(&d.s, 0);
	fxsdev_close(&d.s);
	unlink(STATE_FILE);
	return e ? 1 : 0;
}
