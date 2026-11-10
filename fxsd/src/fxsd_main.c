// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2026 perz3us
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/wait.h>
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
#include "netsvc.h"
#include "rules.h"
#include "schedule.h"
#include "contacts.h"
#include "alarm.h"
#include "pcmsock.h"
#include "rec.h"
#include "recorder.h"
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
#define ALLOW_FILE	"/var/run/fxsd.allow"
#define OUTGOING_FILE	"/var/run/fxsd.outgoing"
#define QUIET_FILE	"/var/run/fxsd.quiet"
#define CONTACTS_FILE	"/var/run/fxsd.contacts"
#define ALARMS_FILE	"/var/run/fxsd.alarms"
#define REC_FILE	"/var/run/fxsd.rec"
#define REC_STATE	"/var/run/fxsd.rec.state"
#define REC_RULES_MAX	64
#define REC_RESERVE	(20L << 20)
#define MQTT_HOOK	"/usr/libexec/fxs-mqtt"
#define ALARM_MAX	16
#define QUIET_MANUAL	"/etc/fxs/dnd.manual"
#define QUIET_MAX	32
#define LIST_MAX	32
#define PCM_SOCK	"/var/run/fxsd/pcm.sock"
#define VOIP_CHECK_MS	5000
#define RING_PHONE_UA	2200
#define CMD_FILE	"/var/run/fxsd.cmd"
#define HOOK_FILE	"/etc/fxs/hook.sh"
#define TEST_RING_MS	10000
#define CALL_RING_MS	30000
#define CW_RETRY_MS	60000
#define SVC_CHECK_MS	30000
#define CW_TRIES	5

struct fxsd {
	struct slic s;
	struct line line;
	uint8_t ch;
	int ringing, audio, verbose, wb, bridge, sip_audio, sipout;
	int cid_mode, cid_pending, cid_before, test_ring, cad_phase, silent, ring_once;
	int phone, ring_n, ring_quiet;
	long long ring_sum;
	int32_t vbat_mv, vline_mv, iloop_ua;
	unsigned long t_ring_arm, t_gap, t_meas, t_test, t_cad, test_ms;
	long talk_since;
};

static unsigned int ring_cad[4] = { 2000, 4000, 0, 0 };
static unsigned int ring_base[4];
static unsigned long data_since;
static int data_redialed;

static volatile sig_atomic_t stop, ring_test, cmd_pending;
static int call_waiting = 1, cw_tries;
static char own_number[CLCC_NUM_MAX];
static const char *led_name = "green:phone";
enum { LED_OFF, LED_ON, LED_SLOW, LED_FAST, LED_BLIP };
enum { LED_READY, LED_RINGING, LED_OFFHOOK, LED_QUIET };
static int led_mode[4] = { LED_ON, LED_FAST, LED_SLOW, LED_BLIP };
static int led_cur = -1;
static int keep_days;
static struct sched_win quiet[QUIET_MAX];
static int nquiet, quiet_on, dnd_repeat = 180;
static long quiet_until;
static struct alarm alarms[ALARM_MAX];
static int nalarms, mqtt_on;
static struct { char code[4]; char num[CLCC_NUM_MAX]; } speed[LIST_MAX];
static char block[LIST_MAX][CLCC_NUM_MAX], allow[LIST_MAX][CLCC_NUM_MAX];
static int nspeed, nblock, nallow;
static char outgoing[LIST_MAX][32];
static int noutgoing, outgoing_mode;
static int voice_off, svc_known, svc_lost;
static int sip_on, sip_16k, client, phone_off;
static const char *sip_name = "";

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

static void on_usr2(int sig)
{
	(void)sig;
	cmd_pending = 1;
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
	if (mode == LED_SLOW || mode == LED_FAST || mode == LED_BLIP) {
		const char *ms = mode == LED_FAST ? "120" : mode == LED_BLIP ? "150" : "600";

		led_write("trigger", "timer");
		led_write("delay_on", ms);
		led_write("delay_off", mode == LED_BLIP ? "1850" : ms);
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
	if (mode == LED_SLOW || mode == LED_FAST || mode == LED_BLIP) {
		if (!strstr(buf, "[timer]"))
			led_set(mode);
	} else if (!strstr(buf, "[none]") ||
		   led_read("brightness", buf, sizeof(buf)) || (atoi(buf) != 0) != (mode == LED_ON)) {
		led_set(mode);
	}
}

static int dnd_mode(void);

static int led_want(const struct fxsd *d)
{
	if (phone_off)
		return LED_OFF;
	if (d->ringing || d->test_ring)
		return led_mode[LED_RINGING];
	if (!access(LED_NIGHT, F_OK))
		return LED_OFF;
	if (d->line.offhook || d->line.state != LS_IDLE)
		return led_mode[LED_OFFHOOK];
	if (dnd_mode() >= 0)
		return led_mode[LED_QUIET];
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
	if (!strncmp(s, "blip", 4))
		return LED_BLIP;
	return LED_ON;
}

static void led_modes_parse(const char *s)
{
	int i;

	for (i = 0; i < 4 && s; i++) {
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
		      t == TONE_NOSERVICE ? &aw1000_noservicetone :
		      t == TONE_CONGESTION ? &aw1000_congestiontone :
		      t == TONE_CONFIRM ? &aw1000_confirmtone :
		      t == TONE_STUTTER ? &aw1000_stuttertone :
		      t == TONE_WAITING ? &aw1000_waitingtone : &aw1000_ringbacktone);
	si_tone_start(&d->s, d->ch, t != TONE_DIAL);
}


static void dnd_fill(struct dnd_in *in)
{
	time_t t = time(NULL);
	struct tm tm;

	memset(in, 0, sizeof(*in));
	localtime_r(&t, &tm);
	in->clock_ok = tm.tm_year + 1900 >= 2025;
	in->wday = tm.tm_wday;
	in->minute = tm.tm_hour * 60 + tm.tm_min;
	in->day = (int)((t + tm.tm_gmtoff) / 86400);
	in->now = t;
	in->quick = quiet_on;
	in->quick_until = quiet_until;
	in->sip_on = sip_on;
}

static int dnd_mode(void)
{
	struct dnd_in in;
	struct dnd_out out;

	dnd_fill(&in);
	dnd_decide(quiet, nquiet, &in, &out);
	return out.quiet ? 0 : -1;
}

static int dnd_allowed(const char *num)
{
	int i;

	for (i = 0; *num && i < nallow; i++)
		if (same_number(num, allow[i]))
			return 1;
	return 0;
}

static void log_reason(const struct dnd_out *o)
{
	static const char *const what[] = { [ACT_SILENT] = "silent", [ACT_ONCE] = "once", [ACT_BUSY] = "busy" };
	FILE *f;

	if (!*o->reason || !(f = fopen(CALL_LOG, "a")))
		return;
	fprintf(f, "%ld reason %s %s\n", (long)time(NULL), o->ring ? "rang" : what[o->action], o->reason);
	fclose(f);
}

static int dnd_silence(struct fxsd *d)
{
	static char last[CLCC_NUM_MAX];
	static time_t t_last;
	const struct contact *c;
	struct dnd_in in;
	struct dnd_out out;

	d->ring_once = 0;
	if (d->test_ring || d->line.recall)
		return 0;
	if (line_emergency(d->line.peer)) {
		msg(LOG_INFO, "emergency number, ringing");
		return 0;
	}
	dnd_fill(&in);
	c = *d->line.peer ? contacts_find(d->line.peer) : NULL;
	in.priority = c ? c->priority : DND_NORMAL;
	in.allowed = dnd_allowed(d->line.peer);
	in.repeat = dnd_repeat && *d->line.peer && *last && same_number(last, d->line.peer) && in.now - t_last < dnd_repeat;
	dnd_decide(quiet, nquiet, &in, &out);
	log_reason(&out);
	if (out.ring) {
		if (!strcmp(out.reason, "repeat"))
			*last = 0;
		if (*out.reason)
			msg(LOG_INFO, "do not disturb, ringing (%s)", out.reason);
		return 0;
	}
	snprintf(last, sizeof(last), "%s", d->line.peer);
	t_last = in.now;
	msg(LOG_INFO, "do not disturb, %s (%s)", out.action == ACT_BUSY ? "turning the call away" :
	    out.action == ACT_ONCE ? "one ring" : "not ringing", out.reason);
	if (out.action == ACT_ONCE) {
		d->ring_once = 1;
		return 0;
	}
	if (out.action == ACT_BUSY && !client)
		at("AT+CHUP");
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

static uint16_t ring_ticks(unsigned int ms)
{
	return ms >= 8191 ? 0xffff : ms * 8;
}

static const char *peer_name(struct fxsd *d)
{
	const struct contact *ct = contacts_find(d->line.peer);

	return ct ? ct->name : NULL;
}

static int ring_program(struct fxsd *d)
{
	struct si_ring ring = aw1000_ring;
	uint16_t on = ring_ticks(ring_cad[2] ? 8191 : ring_cad[0]), off = ring_ticks(ring_cad[1]);

	ring.talo = on & 0xff;
	ring.tahi = on >> 8;
	ring.tilo = off & 0xff;
	ring.tihi = off >> 8;
	return si_ring_setup(&d->s, d->ch, &ring, &aw1000_gen_cfg);
}

static void ring_use(struct fxsd *d, const unsigned int *cad)
{
	int e;

	if (!memcmp(ring_cad, cad, sizeof(ring_cad)))
		return;
	memcpy(ring_cad, cad, sizeof(ring_cad));
	if ((e = ring_program(d)))
		msg(LOG_WARNING, "ring pattern failed: %s", strerror(-e));
}

static void ring_for_peer(struct fxsd *d)
{
	static const unsigned int pat[][4] = {
		[CONTACT_RING_STANDARD] = { 2000, 4000, 0, 0 },
		[CONTACT_RING_SHORT] = { 1000, 4000, 0, 0 },
		[CONTACT_RING_DOUBLE] = { 400, 200, 400, 2000 },
	};
	const struct contact *ct = contacts_find(d->line.peer);

	if (ct && ct->ring != CONTACT_RING_DEFAULT && !d->test_ring && !d->line.recall)
		ring_use(d, pat[ct->ring]);
}

static void op_ring(void *c, int on)
{
	struct fxsd *d = c;
	uint8_t lf;

	if (on && (d->silent = phone_off || dnd_silence(d))) {
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
		ring_for_peer(d);
		if (d->cid_before && d->cid_mode != CID_OFF && !d->line.recall && !d->test_ring) {
			struct tm tm;
			time_t t = time(NULL);
			unsigned long t0 = now_ms();
			int e;

			localtime_r(&t, &tm);
			if (d->cid_before == 2 && d->cid_mode != CID_DTMF) {
				fxsdev_ring_arm(&d->s, RING_ARM_MS);
				e = cid_send_rpas(&d->s, d->ch, d->cid_mode, d->line.peer, peer_name(d), &tm);
			} else {
				e = cid_send_before(&d->s, d->ch, d->cid_mode, d->line.peer, peer_name(d), &tm);
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
		ring_use(d, ring_base);
		if (!d->s.rd(d->s.ctx, d->ch, SI_LINEFEED, &lf) && (lf & 0x0f) == SI_LF_RING)
			si_linefeed(&d->s, d->ch, SI_LF_FWD);
		fxsdev_ring_arm(&d->s, 0);
	}
	d->ringing = on;
}

static int rec_mode = REC_OFF, rec_stereo = 1, nrec_rules, rec_open, rec_full, rec_pruned, rec_usb;
static long rec_flash_mb = 50, rec_usb_mb = 1000;
static struct rec_rule rec_rules[REC_RULES_MAX];
static char rec_dir[300], rec_last[64];
static void op_log(void *c, const char *what, const char *num);

static void rec_write_state(void)
{
	FILE *f = fopen(REC_STATE ".tmp", "w");

	if (!f)
		return;
	fprintf(f, "mode=%d\ndir=%s\nusb=%d\nused=%ld\nlimit=%ld\nfull=%d\npruned=%d\nlast=%s\nactive=%d\n",
		rec_mode, rec_dir, rec_usb, *rec_dir ? rec_dir_bytes(rec_dir) : 0L,
		(rec_usb ? rec_usb_mb : rec_flash_mb) << 20, rec_full, rec_pruned, rec_last, rec_open);
	fclose(f);
	rename(REC_STATE ".tmp", REC_STATE);
}

static void rec_where(void)
{
	char buf[4096];
	FILE *f = fopen("/proc/mounts", "r");
	size_t n = f ? fread(buf, 1, sizeof(buf) - 1, f) : 0;

	if (f)
		fclose(f);
	buf[n] = 0;
	rec_usb = rec_pick_dir(buf, rec_dir, sizeof(rec_dir));
}

static void rec_load(void)
{
	char line[96], v[CLCC_NUM_MAX + 16];
	FILE *f = fopen(REC_FILE, "r");

	nrec_rules = 0;
	while (f && fgets(line, sizeof(line), f)) {
		line[strcspn(line, "\r\n")] = 0;
		if (sscanf(line, "mode=%15s", v) == 1)
			rec_mode = rec_parse_mode(v);
		else if (sscanf(line, "format=%15s", v) == 1)
			rec_stereo = strcmp(v, "mono") != 0;
		else if (sscanf(line, "flash_mb=%ld", &rec_flash_mb) == 1 || sscanf(line, "usb_mb=%ld", &rec_usb_mb) == 1)
			continue;
		else if (nrec_rules < REC_RULES_MAX && !strncmp(line, "rule=", 5)) {
			struct rec_rule *r = &rec_rules[nrec_rules];
			char kind[8];

			if (sscanf(line + 5, "%7s %23s", kind, r->number) == 2 && (!strcmp(kind, "always") || !strcmp(kind, "never"))) {
				r->always = !strcmp(kind, "always");
				nrec_rules++;
			}
		}
	}
	if (f)
		fclose(f);
	if (rec_flash_mb < 1)
		rec_flash_mb = 1;
	if (rec_usb_mb < 1)
		rec_usb_mb = 1;
	rec_where();
	if (recorder_fix_all(REC_FLASH_DIR) + (rec_usb ? recorder_fix_all(rec_dir) : 0))
		msg(LOG_INFO, "fixed recordings left open by a restart");
	rec_write_state();
}

static long rec_room(const char *dir, long limit)
{
	struct statvfs vf;
	unsigned long long fb;
	long free_b;

	if (statvfs(dir, &vf))
		return 0;
	fb = (unsigned long long)vf.f_bavail * vf.f_frsize;
	free_b = fb > LONG_MAX ? LONG_MAX : (long)fb;
	if (!rec_usb)
		free_b -= REC_RESERVE;
	limit -= rec_dir_bytes(dir);
	return limit < free_b ? limit : free_b;
}

static void rec_begin(struct fxsd *d, int src, const char *num)
{
	char removed[8][64], name[64];
	int gone;
	int rate = src == REC_SRC_SIP ? (pcm_frame_samples(d->wb, sip_16k) == 320 ? 16000 : 8000) : d->wb ? 16000 : 8000;
	long limit, need = (long)rate * (rec_stereo ? 4 : 2) * 60, room;
	time_t t = time(NULL);
	struct tm tm;

	if (rec_open || !rec_wanted(rec_mode, rec_rules, nrec_rules, num ? num : ""))
		return;
	rec_where();
	mkdir(rec_dir, 0700);
	limit = (rec_usb ? rec_usb_mb : rec_flash_mb) << 20;
	rec_full = 0;
	gone = rec_prune(rec_dir, limit, need, removed, 8);
	if (gone < 0) {
		rec_full = 1;
		msg(LOG_WARNING, "recording skipped, kept recordings fill the space");
	} else if (gone) {
		rec_pruned += gone;
		msg(LOG_INFO, "removed %d old recordings to make room", gone);
	}
	room = rec_room(rec_dir, limit);
	if (!rec_full && room < need / 4) {
		rec_full = 1;
		msg(LOG_WARNING, "recording skipped, no free space");
	}
	if (rec_full) {
		rec_write_state();
		return;
	}
	localtime_r(&t, &tm);
	snprintf(name, sizeof(name), "%04d%02d%02d-%02d%02d%02d-%ld.wav", tm.tm_year + 1900, tm.tm_mon + 1,
		 tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, (long)t);
	if (src == REC_SRC_LINE && fxsdev_rec(&d->s, 1, NULL)) {
		msg(LOG_WARNING, "recording needs the newer kernel module");
		return;
	}
	if (recorder_start(rec_dir, name, rate, rec_stereo, src, room)) {
		if (src == REC_SRC_LINE)
			fxsdev_rec(&d->s, 0, NULL);
		msg(LOG_WARNING, "could not start recording in %s", rec_dir);
		return;
	}
	rec_open = src + 1;
	snprintf(rec_last, sizeof(rec_last), "%s", name);
	op_log(d, "recorded", name);
	rec_write_state();
}

static void rec_end(struct fxsd *d)
{
	uint32_t dropped = 0;

	if (!rec_open)
		return;
	if (rec_open == REC_SRC_LINE + 1)
		fxsdev_rec(&d->s, 0, &dropped);
	recorder_stop();
	if (recorder_hit_limit())
		rec_full = 1;
	if (dropped)
		msg(LOG_INFO, "recording lost %u samples", dropped);
	rec_open = 0;
	rec_write_state();
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
	if (rec_open && (!want || (want == 2) != (rec_open == REC_SRC_SIP + 1)))
		rec_end(d);
	if (d->bridge && want)
		fxsdev_bridge(&d->s, 0);
	if (fxsdev_bridge(&d->s, want ? arg | (want == 2 ? 0x80u : 0) : 0))
		msg(LOG_ERR, "audio bridge %s failed", want ? "start" : "stop");
	d->bridge = want;
	pcmsock_live(want == 2);
	if (want == 2 && voip_snap()->from_sip) {
		op_log(d, "sipout", voip_snap()->number);
		d->sipout = 1;
	} else if (want != 2 && d->sipout) {
		op_log(d, "hangup", "");
		d->sipout = 0;
	}
	if (want == 2)
		rec_begin(d, REC_SRC_SIP, voip_snap()->number);
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
		if (voip_request("dial", cmd + 3)) {
			msg(LOG_WARNING, "the SIP service did not accept the call");
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
		msg(LOG_WARNING, "the SIP service did not accept the answer");
}

static void op_hangup(void *c)
{
	struct fxsd *d = c;

	if (!client)
		at("AT+CHUP");
	else if (voip_request(d->line.state == LS_IDLE || d->line.state == LS_RINGING ? "reject" : "hangup", NULL))
		msg(LOG_WARNING, "the SIP service did not accept the hangup");
}

static void op_chld(void *c, const char *arg)
{
	char cmd[32];

	(void)c;
	snprintf(cmd, sizeof(cmd), "AT+CHLD=%s", arg);
	at(cmd);
}

static void op_dtmf(void *c, char digit)
{
	char cmd[16];

	(void)c;
	if (client) {
		if (voip_dtmf(digit))
			msg(LOG_WARNING, "the SIP service did not accept digit %c", digit);
		return;
	}
	snprintf(cmd, sizeof(cmd), "AT+VTS=\"%c\"", digit);
	at(cmd);
}

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
	if ((f = fopen(ALLOW_FILE, "r"))) {
		while (nallow < LIST_MAX && fgets(buf, sizeof(buf), f))
			if (sscanf(buf, "%31s", allow[nallow]) == 1)
				nallow++;
		fclose(f);
	}
	if ((f = fopen(OUTGOING_FILE, "r"))) {
		if (fgets(buf, sizeof(buf), f))
			outgoing_mode = !strncmp(buf, "block", 5) ? RULES_BLOCK :
					!strncmp(buf, "allow", 5) ? RULES_ALLOW : RULES_OFF;
		while (noutgoing < LIST_MAX && fgets(buf, sizeof(buf), f))
			if (sscanf(buf, "%31s", outgoing[noutgoing]) == 1)
				noutgoing++;
		fclose(f);
	}
}

static int at_body(const char *cmd, char *resp, size_t n, int secs, const char *until)
{
	char raw[2048];

	if (modem_at_wait(cmd, raw, sizeof(raw), secs, until))
		return -1;
	if (netsvc_unwrap(raw, resp, n))
		snprintf(resp, n, "%s", raw);
	return 0;
}

static const char *ussd_result(const char *buf, char *text, size_t n, int *more)
{
	int st = netsvc_cusd(buf, text, n);

	if (st < 0)
		return "noanswer";
	if (st == 4)
		return "refused";
	*more = st == 1;
	return NULL;
}

static const char *cusd_wait(FILE *f, char *text, size_t n, int *more)
{
	char line[768], json[1024], acc[1536] = "";
	int st;

	while (fgets(json, sizeof(json), f)) {
		if (netsvc_raw_line(json, line, sizeof(line)))
			continue;
		if (!*acc && !strncmp(line, "+CME ERROR", 10) && strstr(json, "\"correlation\":\"idle\""))
			return "refused";
		if (!*acc && strncmp(line, "+CUSD:", 6))
			continue;
		snprintf(acc + strlen(acc), sizeof(acc) - strlen(acc), "%s\r\n", line);
		st = netsvc_cusd(acc, text, n);
		if (st >= 0 && (*text || !strchr(acc, '"')))
			return ussd_result(acc, text, n, more);
	}
	return "noanswer";
}

static const char *service_send(const char *cmd, char *text, size_t n, int *more)
{
	char r[512];
	const char *err;
	int p[2];
	pid_t pid;
	FILE *f;

	if (modem_direct()) {
		if (at_body(cmd, r, sizeof(r), 6, "+CUSD:") || strstr(r, "ERROR"))
			return "refused";
		return ussd_result(r, text, n, more);
	}
	if (pipe(p))
		return "busy";
	pid = fork();
	if (pid < 0) {
		close(p[0]);
		close(p[1]);
		return "busy";
	}
	if (!pid) {
		dup2(p[1], 1);
		close(p[0]);
		close(p[1]);
		execlp("ubus", "ubus", "-t", "6", "listen", "qmodem.at.line", (char *)NULL);
		_exit(127);
	}
	close(p[1]);
	f = fdopen(p[0], "r");
	usleep(200000);
	if (at_body(cmd, r, sizeof(r), 3, NULL) || strstr(r, "ERROR"))
		err = "refused";
	else if (strstr(r, "+CUSD:"))
		err = ussd_result(r, text, n, more);
	else
		err = f ? cusd_wait(f, text, n, more) : "noanswer";
	kill(pid, SIGTERM);
	if (f)
		fclose(f);
	else
		close(p[0]);
	waitpid(pid, NULL, 0);
	return err;
}

static int op_service(void *c, const char *code)
{
	char cmd[40], r[64], text[256];
	int more = 0, failed;

	(void)c;
	snprintf(cmd, sizeof(cmd), "ATD%s;", code);
	failed = service_send(cmd, text, sizeof(text), &more) != NULL;
	if (more)
		at_body("AT+CUSD=2", r, sizeof(r), 3, NULL);
	return failed ? -1 : 0;
}

static void op_dnd_code(void *c, int on);
static void op_quiet_for(void *c, int minutes);
static void quiet_set(struct fxsd *d, int minutes, const char *from);

static int op_barred(void *c, const char *number)
{
	(void)c;
	return rules_barred(number, outgoing_mode, outgoing, noutgoing);
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

static int op_blocked(void *c, const char *num)
{
	int i;

	(void)c;
	if (line_emergency(num))
		return 0;
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

static void load_last(struct line *l)
{
	char buf[96], what[16], num[CLCC_NUM_MAX];
	FILE *f = fopen(CALL_LOG, "r");
	int n;

	if (!f)
		return;
	while (fgets(buf, sizeof(buf), f)) {
		num[0] = 0;
		n = sscanf(buf, "%*d %15s %31s", what, num);
		if (n < 1)
			continue;
		if (!strcmp(what, "incoming") || !strcmp(what, "waiting"))
			strcpy(l->last_in, num);
		else if (!strcmp(what, "dial") && *num)
			strcpy(l->last_out, num);
	}
	fclose(f);
}

static void run_one(const char *path, const char *what, const char *num)
{
	pid_t p;
	int fd;

	if (access(path, X_OK))
		return;
	p = fork();
	if (p == 0) {
		if (fork() == 0) {
			for (fd = 3; fd < 256; fd++)
				close(fd);
			signal(SIGPIPE, SIG_DFL);
			execl(path, path, what, num ? num : "", (char *)NULL);
			_exit(127);
		}
		_exit(0);
	}
	if (p > 0)
		waitpid(p, NULL, 0);
}

static void run_hook(const char *what, const char *num)
{
	run_one(HOOK_FILE, what, num);
	if (mqtt_on)
		run_one(MQTT_HOOK, what, num);
}

static void op_log(void *c, const char *what, const char *num)
{
	FILE *f = fopen(CALL_LOG, "a");
	long sz = 0;

	(void)c;
	msg(LOG_INFO, "call %s", what);
	if (f) {
		fprintf(f, "%ld %s %s\n", (long)time(NULL), what, num ? num : "");
		if ((!strcmp(what, "elsewhere") || !strcmp(what, "sipout")) && voip_snap()->who[0])
			fprintf(f, "%ld via %s\n", (long)time(NULL), voip_snap()->who);
		sz = ftell(f);
		fclose(f);
	}
	if (sz > CALL_LOG_MAX * 48)
		trim_log();
	run_hook(what, num);
	if (!strcmp(what, "answered") || !strcmp(what, "connected"))
		rec_begin(c, REC_SRC_LINE, num);
	else if (rec_open && (!strcmp(what, "hangup") || !strcmp(what, "ended")))
		rec_end(c);
}

static int op_elsewhere(void *c)
{
	(void)c;
	return client && voip_snap()->answered && voip_snap()->owner == VO_OTHER;
}

static int op_call_fail(void *c)
{
	char r[256];

	(void)c;
	if (client)
		return 0;
	return !at_body("AT+CEER", r, sizeof(r), 3, NULL) && netsvc_ceer_congested(r) == 1;
}

static struct line_ops ops = {
	.tone = op_tone, .ring = op_ring, .audio = op_audio, .dial = op_dial,
	.answer = op_answer, .hangup = op_hangup, .chld = op_chld, .dtmf = op_dtmf, .log = op_log,
	.speed = op_speed, .blocked = op_blocked, .barred = op_barred, .service = op_service, .dnd_code = op_dnd_code, .quiet_for = op_quiet_for, .elsewhere = op_elsewhere,
	.call_fail = op_call_fail,
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
	fprintf(f, "state=%s\npeer=%s\noffhook=%d\nvbat=%d\nvline=%d\niloop=%d\nown=%s\ntest=%d\nvoiceoff=%d\nsince=%ld\nhd=%d\nsip=%d\nsipcall=%d\ndnd=%d\nsilent=%d\nphone=%d\nservice=%d\nquiet=%d\nquiet_until=%ld\n",
		line_state_name(d->line.state), d->line.peer, d->line.offhook,
		d->vbat_mv, d->vline_mv, d->iloop_ua, own_number, d->test_ring, voice_off, d->talk_since, d->wb,
		sip_on ? (client ? 2 : 1) : 0, d->sip_audio, dnd_mode() >= 0, d->silent, d->phone,
		svc_known ? !d->line.no_service : -1, quiet_on, quiet_on ? quiet_until : 0L);
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

static int ring_free(const struct fxsd *d)
{
	return !phone_off && d->line.state == LS_IDLE && !d->ringing && !d->line.offhook;
}

static void ring_start(struct fxsd *d, unsigned long now, unsigned long ms)
{
	d->test_ring = 1;
	op_ring(d, 1);
	d->cid_pending = 0;
	d->t_test = now;
	d->test_ms = ms;
	msg(LOG_INFO, "test ring");
	write_state(d);
}

static void alarms_load(void)
{
	char buf[96], days[40];
	unsigned int h, m, secs;
	int mask;
	FILE *f = fopen(ALARMS_FILE, "r");

	if (!f)
		return;
	while (nalarms < ALARM_MAX && fgets(buf, sizeof(buf), f)) {
		if (sscanf(buf, "%39s %u:%u %u", days, &h, &m, &secs) != 4 || h > 23 || m > 59 ||
		    secs < 10 || secs > 120 || (mask = sched_parse_days(days)) < 0) {
			msg(LOG_WARNING, "bad alarm ignored");
			continue;
		}
		alarms[nalarms].days = mask;
		alarms[nalarms].minute = h * 60 + m;
		alarms[nalarms].seconds = secs;
		alarms[nalarms].last_day = -1;
		nalarms++;
	}
	fclose(f);
}

static void alarm_tick(struct fxsd *d, unsigned long now)
{
	static unsigned long t_last;
	time_t t;
	struct tm tm;
	int i;

	if (!nalarms || now - t_last < 1000)
		return;
	t_last = now;
	t = time(NULL);
	localtime_r(&t, &tm);
	if (tm.tm_year + 1900 < 2025)
		return;
	i = alarm_due(alarms, nalarms, tm.tm_wday, tm.tm_hour * 60 + tm.tm_min, tm.tm_year * 1000L + tm.tm_yday);
	if (i < 0)
		return;
	if (!ring_free(d)) {
		msg(LOG_INFO, "alarm skipped, phone in use");
		return;
	}
	op_log(d, "alarm", "");
	ring_start(d, now, alarms[i].seconds * 1000UL);
}

static void test_ring_tick(struct fxsd *d, unsigned long now)
{
	int stop_test = 0;

	if (ring_test) {
		ring_test = 0;
		if (d->test_ring)
			stop_test = 1;
		else if (ring_free(d))
			ring_start(d, now, TEST_RING_MS);
	}
	if (d->test_ring && (stop_test || now - d->t_test >= d->test_ms || d->line.state != LS_IDLE)) {
		if (d->line.state != LS_RINGING)
			op_ring(d, 0);
		d->test_ring = 0;
		line_autodial(&d->line, NULL, now);
		write_state(d);
	}
}

#define REPLY_FILE	"/var/run/fxsd.reply"

static int req_arg_ok(const char *s)
{
	for (; *s; s++)
		if (!strchr("0123456789*#+,", *s))
			return 0;
	return 1;
}

static void req_reply(const char *id, const char *err, char *text, int more)
{
	char path[64], tmp[72], *p;
	FILE *f;

	snprintf(path, sizeof(path), REPLY_FILE ".%s", id);
	snprintf(tmp, sizeof(tmp), "%s.tmp", path);
	if (!(f = fopen(tmp, "w")))
		return;
	fprintf(f, "ok=%d\n", !err);
	if (err)
		fprintf(f, "error=%s\n", err);
	if (more)
		fprintf(f, "more=1\n");
	if (text && *text) {
		for (p = text; *p; p++)
			if (*p == '\r')
				*p = ' ';
			else if (*p == '\n')
				*p = '|';
		fprintf(f, "text=%s\n", text);
	}
	if (fclose(f) || rename(tmp, path))
		unlink(tmp);
}

static const char *fwd_get(const char *arg, char *text, size_t n)
{
	struct netsvc_fwd f;
	char cmd[32], r[1024];

	if (strlen(arg) != 1 || *arg < '0' || *arg > '3')
		return "arg";
	snprintf(cmd, sizeof(cmd), "AT+CCFC=%c,2", *arg);
	if (at_body(cmd, r, sizeof(r), 12, NULL) || strstr(r, "ERROR") || !strstr(r, "OK"))
		return "refused";
	if (!netsvc_ccfc(r, &f) && f.active)
		snprintf(text, n, "on,%s,%d", f.number, f.seconds);
	else
		snprintf(text, n, "off");
	return NULL;
}

static const char *fwd_set(const char *arg)
{
	char num[32] = "", cmd[96], r[512];
	int reason = -1, secs = 0;

	if (sscanf(arg, "%d,%31[+0-9],%d", &reason, num, &secs) < 1)
		return "arg";
	if (reason < 0 || reason > 3)
		return "arg";
	if (!*num)
		snprintf(cmd, sizeof(cmd), "AT+CCFC=%d,4", reason);
	else if (reason == 2 && secs >= 5 && secs <= 30)
		snprintf(cmd, sizeof(cmd), "AT+CCFC=2,3,\"%s\",%d,,,,%d", num, *num == '+' ? 145 : 129, secs);
	else
		snprintf(cmd, sizeof(cmd), "AT+CCFC=%d,3,\"%s\",%d", reason, num, *num == '+' ? 145 : 129);
	if (at_body(cmd, r, sizeof(r), 12, NULL) || strstr(r, "ERROR") || !strstr(r, "OK"))
		return "refused";
	return NULL;
}

static const char *ussd_run(const char *arg, char *text, size_t n, int *more)
{
	char cmd[96], r[1024];
	FILE *f;

	modem_at("AT+CSCS=\"GSM\"", r, sizeof(r));
	snprintf(cmd, sizeof(cmd), "AT+CUSD=1,\"%s\",15", arg);
	if (modem_direct()) {
		if (at_body(cmd, r, sizeof(r), 12, "+CUSD:") || strstr(r, "ERROR"))
			return strstr(r, "ERROR") ? "refused" : "noanswer";
		return ussd_result(r, text, n, more);
	}
	f = popen("ubus -t 12 listen qmodem.at.line 2>/dev/null", "r");
	usleep(200000);
	if (at_body(cmd, r, sizeof(r), 3, NULL) || strstr(r, "ERROR"))
		return "refused";
	if (strstr(r, "+CUSD:"))
		return ussd_result(r, text, n, more);
	return f ? cusd_wait(f, text, n, more) : "noanswer";
}

static const char *at_dispatch(const char *kind, const char *arg, char *text, size_t n, int *more)
{
	char r[256];

	if (!strcmp(kind, "fwd-get"))
		return fwd_get(arg, text, n);
	if (!strcmp(kind, "fwd-set"))
		return fwd_set(arg);
	if (!strcmp(kind, "ussd-end"))
		return modem_at("AT+CUSD=2", r, sizeof(r)) ? "refused" : NULL;
	return ussd_run(arg, text, n, more);
}

static void req_run(const char *id, const char *kind, const char *arg)
{
	char text[768] = "";
	const char *err;
	int more = 0;
	pid_t p;

	if (modem_direct()) {
		err = at_dispatch(kind, arg, text, sizeof(text), &more);
		req_reply(id, err, text, more);
		return;
	}
	p = fork();
	if (p == 0) {
		if (fork() == 0) {
			int fd;

			for (fd = 3; fd < 64; fd++)
				close(fd);
			signal(SIGTERM, SIG_DFL);
			signal(SIGINT, SIG_DFL);
			err = at_dispatch(kind, arg, text, sizeof(text), &more);
			req_reply(id, err, text, more);
		}
		_exit(0);
	}
	if (p > 0)
		waitpid(p, NULL, 0);
	else
		req_reply(id, "busy", NULL, 0);
}

static void at_req(struct fxsd *d, char *line)
{
	char *sp, *id, *kind, *arg, *extra;
	const char *err;

	id = strtok_r(line + 7, " \t\r\n", &sp);
	kind = strtok_r(NULL, " \t\r\n", &sp);
	arg = strtok_r(NULL, " \t\r\n", &sp);
	extra = strtok_r(NULL, " \t\r\n", &sp);
	if (!id || strlen(id) > 10 || strspn(id, "0123456789") != strlen(id))
		return;
	if (!kind || (strcmp(kind, "fwd-get") && strcmp(kind, "fwd-set") && strcmp(kind, "ussd") && strcmp(kind, "ussd-end")))
		err = "kind";
	else if (extra || (arg && (strlen(arg) > 63 || !req_arg_ok(arg))))
		err = "arg";
	else if (d->line.state != LS_IDLE)
		err = "busy";
	else
		err = NULL;
	if (err)
		req_reply(id, err, NULL, 0);
	else
		req_run(id, kind, arg ? arg : "");
}

static void cmd_tick(struct fxsd *d, unsigned long now)
{
	char buf[128], num[LINE_DIGITS_MAX] = "";
	unsigned long ms;
	unsigned int sec;
	int qm;
	FILE *f;

	if (!cmd_pending)
		return;
	cmd_pending = 0;
	if (!(f = fopen(CMD_FILE, "r")))
		return;
	if (!fgets(buf, sizeof(buf), f))
		buf[0] = 0;
	fclose(f);
	unlink(CMD_FILE);
	if (!strncmp(buf, "at-req ", 7)) {
		at_req(d, buf);
		return;
	}
	if (sscanf(buf, "quiet %d", &qm) == 1) {
		if (qm >= -1 && qm <= 1440)
			quiet_set(d, qm, "the web page");
		return;
	}
	if (sscanf(buf, "ring %u", &sec) == 1 && sec >= 1 && sec <= 60)
		ms = sec * 1000UL;
	else if (sscanf(buf, "call %23s", num) == 1)
		ms = CALL_RING_MS;
	else
		return;
	if (*num && !phone_off && d->line.state == LS_DIALTONE) {
		line_autodial(&d->line, num, now);
	} else if (d->test_ring) {
		d->t_test = now;
		d->test_ms = ms;
		line_autodial(&d->line, num, now);
	} else if (ring_free(d)) {
		line_autodial(&d->line, num, now);
		ring_start(d, now, ms);
	}
}

static void cw_tick(const struct fxsd *d, unsigned long now, unsigned long *t_cw)
{
	char cmd[24], r[512];
	int on = call_waiting && !sip_on;

	if (cw_tries >= CW_TRIES || d->line.state != LS_IDLE || now - *t_cw < CW_RETRY_MS)
		return;
	*t_cw = now;
	snprintf(cmd, sizeof(cmd), "AT+CCWA=0,%d,1", on);
	if (!modem_at(cmd, r, sizeof(r)) && strstr(r, "OK")) {
		cw_tries = CW_TRIES;
		msg(LOG_INFO, "call waiting %s", on ? "on" : "off");
	} else if (++cw_tries == CW_TRIES) {
		msg(LOG_WARNING, "the network did not accept the call waiting setting");
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
	if (!modem_at("AT+CIMI", r, sizeof(r)) && ((p = strstr(r, "\"response\"")) || (p = strstr(r, "\n")))) {
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
		if (q < 0 && modem_port_ok() && !modem_direct() && ++fails >= 10) {
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

	if (!phone_off && !d->s.rd(d->s.ctx, d->ch, SI_LCRRTP, &lcr))
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
	if ((e = cid_send(&d->s, d->ch, d->cid_mode, d->line.peer, peer_name(d), &tm)))
		msg(LOG_WARNING, "caller ID failed: %s", strerror(-e));
	else
		msg(LOG_INFO, "caller ID sent in %lu ms", now_ms() - now);
}

static void ring_once_tick(struct fxsd *d, unsigned long now)
{
	uint8_t lf;

	if (!d->ring_once || !d->ringing || d->silent || d->cid_pending ||
	    now - d->t_ring_arm < ring_cad[0] + 1500)
		return;
	d->ring_once = 0;
	ring_done(d);
	ring_use(d, ring_base);
	if (!d->s.rd(d->s.ctx, d->ch, SI_LINEFEED, &lf) && (lf & 0x0f) == SI_LF_RING)
		si_linefeed(&d->s, d->ch, SI_LF_FWD);
	fxsdev_ring_arm(&d->s, 0);
	d->silent = 1;
	write_state(d);
}

static void ring_cad_tick(struct fxsd *d, unsigned long now)
{
	if (!ring_cad[2] || d->silent || !(d->ringing || d->test_ring) || now - d->t_cad < ring_cad[d->cad_phase])
		return;
	d->cad_phase = (d->cad_phase + 1) & 3;
	d->t_cad = now;
	si_linefeed(&d->s, d->ch, d->cad_phase & 1 ? SI_LF_FWD : SI_LF_RING);
}

static void quiet_load(void)
{
	char buf[160], a[40], b[16], mode[16], act[16];
	unsigned int h1, m1, h2, m2;
	int mask = 0, md, first = 0, last = 0, k, off = 0;
	FILE *f;

	if ((f = fopen(QUIET_MANUAL, "r"))) {
		if (fgets(buf, sizeof(buf), f)) {
			quiet_on = !strncmp(buf, "on", 2) || sscanf(buf, "until=%ld", &quiet_until) == 1;
			if (strncmp(buf, "until=", 6))
				quiet_until = 0;
		}
		fclose(f);
	}
	if (!(f = fopen(QUIET_FILE, "r")))
		return;
	while (fgets(buf, sizeof(buf), f)) {
		struct sched_win *w = &quiet[nquiet];

		buf[strcspn(buf, "\r\n")] = 0;
		if (buf[0] == 'W')
			k = sscanf(buf, "W %39s %u:%u %u:%u %15s %15s %n", a, &h1, &m1, &h2, &m2, mode, act, &off) == 7 &&
			    (mask = sched_parse_days(a)) >= 0;
		else if (buf[0] == 'H')
			k = sscanf(buf, "H %39s %15s %u:%u %u:%u %15s %15s %n", a, b, &h1, &m1, &h2, &m2, mode, act, &off) == 8 &&
			    (first = sched_parse_date(a)) > 0 && (last = sched_parse_date(b)) >= first;
		else
			k = 0;
		if (!k || h1 > 23 || h2 > 23 || m1 > 59 || m2 > 59 || (md = sched_parse_mode(mode)) < 0) {
			msg(LOG_WARNING, "bad quiet window ignored");
			continue;
		}
		if (nquiet == QUIET_MAX) {
			msg(LOG_WARNING, "only the first %d quiet windows and holidays are used", QUIET_MAX);
			break;
		}
		memset(w, 0, sizeof(*w));
		w->days = buf[0] == 'W' ? mask : 0;
		w->first_day = buf[0] == 'H' ? first : 0;
		w->last_day = buf[0] == 'H' ? last : 0;
		w->from = h1 * 60 + m1;
		w->to = h2 * 60 + m2;
		w->mode = md;
		w->action = sched_parse_action(act);
		snprintf(w->name, sizeof(w->name), "%s", off ? buf + off : "");
		nquiet++;
	}
	fclose(f);
}

static void quiet_set(struct fxsd *d, int minutes, const char *from)
{
	FILE *f;

	quiet_on = minutes != 0;
	quiet_until = minutes > 0 ? (long)time(NULL) + minutes * 60L : 0;
	if (quiet_on && (f = fopen(QUIET_MANUAL ".tmp", "w"))) {
		if (quiet_until)
			fprintf(f, "until=%ld\n", quiet_until);
		else
			fputs("on\n", f);
		fclose(f);
		rename(QUIET_MANUAL ".tmp", QUIET_MANUAL);
	} else if (!quiet_on) {
		unlink(QUIET_MANUAL);
	}
	if (minutes > 0)
		msg(LOG_INFO, "quiet for %d minutes from %s", minutes, from);
	else
		msg(LOG_INFO, "quiet %s from %s", quiet_on ? "on" : "off", from);
	write_state(d);
	run_hook("quiet", quiet_on ? "on" : "off");
}

static void quiet_expire(struct fxsd *d)
{
	if (quiet_on && quiet_until && time(NULL) >= quiet_until && time(NULL) > 1735689600L) {
		quiet_set(d, 0, "the timer");
	}
}

static void op_dnd_code(void *c, int on)
{
	quiet_set(c, on ? -1 : 0, "the phone");
}

static void op_quiet_for(void *c, int minutes)
{
	quiet_set(c, minutes, "the phone");
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

static void svc_tick(struct fxsd *d, unsigned long now, unsigned long *t_svc)
{
	char creg[256], cereg[256], c5g[256];
	int reg;

	if (client || now - *t_svc < SVC_CHECK_MS || d->line.state != LS_IDLE || d->line.offhook || d->ringing || d->test_ring)
		return;
	*t_svc = now;
	creg[0] = cereg[0] = c5g[0] = 0;
	if (at_body("AT+CREG?", creg, sizeof(creg), 3, NULL))
		creg[0] = 0;
	if (netsvc_registered(creg, NULL, NULL) != 1) {
		if (at_body("AT+CEREG?", cereg, sizeof(cereg), 3, NULL))
			cereg[0] = 0;
		if (netsvc_registered(creg, cereg, NULL) != 1 && at_body("AT+C5GREG?", c5g, sizeof(c5g), 3, NULL))
			c5g[0] = 0;
	}
	reg = netsvc_registered(creg, cereg, c5g);
	if (reg < 0)
		return;
	if (svc_known && reg == !d->line.no_service) {
		svc_lost = 0;
		return;
	}
	if (!reg && !svc_lost++)
		return;
	svc_lost = 0;
	svc_known = 1;
	d->line.no_service = !reg;
	msg(LOG_INFO, reg ? "mobile service found" : "no mobile service");
	write_state(d);
}

static void sip_load(const char *dir)
{
	struct sipmap m;
	char path[128];
	const char *slash = strrchr(dir, '/');
	FILE *f;
	int bad;

	sip_name = slash ? slash + 1 : dir;
	snprintf(path, sizeof(path), "%s/map", dir);
	if (!(f = fopen(path, "r"))) {
		msg(LOG_WARNING, "SIP service map in %s is not valid", dir);
		return;
	}
	bad = sipmap_load(&m, f);
	fclose(f);
	if (bad) {
		msg(LOG_WARNING, "SIP service map in %s is not valid", dir);
		return;
	}
	voip_service(&m);
	sip_16k = m.audio == 16000;
	sip_on = 1;
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
		msg(LOG_INFO, client ? "SIP calling on, calls go through %s" :
		    "%s is not running, using the modem directly", sip_name);
		write_state(d);
	}
	if (client && voip_changed())
		poll_modem(d, now);
}

static int setup(struct fxsd *d, const struct si_patch *p, int gtx, int grx)
{
	struct si_pwr_report rep[2];
	struct si_imp imp = aw1000_imp_600;
	int e;

	imp.txgain_db = gtx;
	imp.rxgain_db = grx;
	memcpy(ring_base, ring_cad, sizeof(ring_base));
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
	    (e = ring_program(d)) ||
	    (e = si_pcm_setup(&d->s, d->ch, 31, 32)) ||
	    (d->wb && (e = si_pcm_wideband(&d->s, d->ch))) ||
	    (e = si_irq_enable(&d->s, d->ch, &aw1000_gen_cfg)))
		return e;
	return fxsdev_ack(&d->s);
}

static int usage(void)
{
	fprintf(stderr, "usage: fxsd -p PATCH [-c CHANNEL] [-g TXDB,RXDB] [-a ATPORT] [-l VOLUME] [-r RXGAIN] [-e LED] [-m READY,RINGING,OFFHOOK] [-b 0|1] [-i bellcore|etsi|dtmf|off] [-t before|after] [-k DAYS] [-w 0|1] [-S SERVICE_DIR] [-n ON,OFF[,ON,OFF]] [-C 0|1] [-H NUMBER,SECONDS] [-F MIN,MAX] [-D SECONDS] [-R REDIAL,CALLBACK] [-O QUIETON,QUIETOFF] [-q REPEATMIN] [-M 0|1] [-T 0|1] [-o] [-f] [-v]\n");
	return 2;
}

int main(int argc, char **argv)
{
	const char *patch = NULL, *numfmt = NULL, *sip_dir = NULL;
	struct fxsd d = { .ch = 1, .cid_mode = CID_BELLCORE, .phone = -1 };
	struct si_patch p;
	unsigned long t_modem = 0, t_hook = 0, t_ready, t_prune, t_voip, t_urc, t_alive = 0, t_cw, t_svc;
	unsigned int hot_s = 5, flash_min = 80, flash_max = 700, dial_s = 4;
	char hotline[LINE_DIGITS_MAX] = "";
	char code_redial[16] = "", code_callback[16] = "", code_dnd_on[16] = "", code_dnd_off[16] = "";
	int guard = 1;
	enum line_state last = LS_BUSY + 1;
	int gtx = -6, grx = 0, fg = 0, vol = -1, rxg = -1, o, e, q;
	int ringback = 1, hd = 0, missed_tone = 0;
	char r[512];

	while ((o = getopt(argc, argv, "p:c:g:a:l:r:e:m:b:i:t:k:w:S:n:q:u:C:H:F:D:R:O:M:T:ofv")) != -1) {
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
		case 'S': sip_dir = optarg; break;
		case 'n': ring_parse(optarg); break;
		case 'q': dnd_repeat = atoi(optarg) >= 0 && atoi(optarg) <= 60 ? atoi(optarg) * 60 : dnd_repeat; break;
		case 'O': sscanf(optarg, "%15[^,],%15s", code_dnd_on, code_dnd_off); break;
		case 'u': numfmt = optarg; break;
		case 'M': missed_tone = atoi(optarg) == 1; break;
		case 'T': mqtt_on = atoi(optarg) == 1; break;
		case 'C': call_waiting = atoi(optarg) == 1; break;
		case 'H': sscanf(optarg, "%23[^,],%u", hotline, &hot_s); break;
		case 'F': sscanf(optarg, "%u,%u", &flash_min, &flash_max); break;
		case 'D': dial_s = atoi(optarg); break;
		case 'R': sscanf(optarg, "%15[^,],%15s", code_redial, code_callback); break;
		case 'o': phone_off = 1; break;
		case 'f': fg = 1; break;
		case 'v': d.verbose = 1; break;
		default: return usage();
		}
	}
	if (!patch)
		return usage();
	openlog("fxsd", LOG_PID | (fg ? LOG_PERROR : 0), LOG_DAEMON);
	if (sip_dir)
		sip_load(sip_dir);
	if ((e = si_patch_load_file(patch, &p))) {
		msg(LOG_ERR, "patch %s: %s", patch, strerror(-e));
		return 1;
	}
	signal(SIGINT, on_sig);
	signal(SIGTERM, on_sig);
	signal(SIGUSR1, on_usr1);
	signal(SIGUSR2, on_usr2);
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
	if (phone_off)
		si_linefeed(&d.s, d.ch, SI_LF_OPEN);
	load_lists();
	contacts_load(CONTACTS_FILE);
	ops.ctx = &d;
	line_init(&d.line, &ops);
	d.line.ringback = ringback;
	d.line.missed_tone = missed_tone;
	load_last(&d.line);
	if (flash_min >= 20 && flash_min <= 500 && flash_max >= 300 && flash_max <= 3000 && flash_max >= flash_min + 100) {
		d.line.flash_min_ms = flash_min;
		d.line.flash_ms = flash_max;
	} else {
		msg(LOG_WARNING, "bad flash times, using the defaults");
	}
	if (dial_s >= 2 && dial_s <= 15)
		d.line.digit_ms = dial_s * 1000;
	if (*code_redial && line_set_codes(&d.line, code_redial, code_callback))
		msg(LOG_WARNING, "bad redial or call back code, using the defaults");
	if (*code_dnd_on && line_set_dnd_codes(&d.line, code_dnd_on, code_dnd_off))
		msg(LOG_WARNING, "bad quiet code, using the defaults");
	quiet_load();
	alarms_load();
	rec_load();
	if (*hotline && hot_s <= 10) {
		snprintf(d.line.hotline, sizeof(d.line.hotline), "%s", hotline);
		d.line.hotline_ms = hot_s * 1000;
	}
	if (sip_on && (e = pcmsock_start(PCM_SOCK, "/dev/fxs0audio", d.wb, sip_16k))) {
		msg(LOG_ERR, "SIP calling needs the newer kernel module (%s), turning it off", strerror(-e));
		sip_on = 0;
		e = 0;
	}
	if (sip_on)
		msg(LOG_INFO, "SIP audio %d kHz", pcm_frame_samples(d.wb, sip_16k) == 320 ? 16 : 8);
	msg(LOG_INFO, "ready on channel %u, modem AT port %s%s%s", d.ch, modem_port(),
	    sip_on ? ", SIP calling on (experimental)" : "", phone_off ? ", phone port off" : "");
	trim_log();
	led_update(led_want(&d), 0);
	t_ready = now_ms();
	t_prune = t_ready;
	t_voip = t_ready - VOIP_CHECK_MS;
	t_urc = t_ready - VOIP_CHECK_MS;
	t_cw = t_ready - CW_RETRY_MS;
	t_svc = t_ready - SVC_CHECK_MS;

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
		ring_once_tick(&d, now);
		quiet_expire(&d);
		data_tick(now);
		cmd_tick(&d, now);
		test_ring_tick(&d, now);
		cw_tick(&d, now, &t_cw);
		svc_tick(&d, now, &t_svc);
		alarm_tick(&d, now);
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
		if (rec_open && !recorder_active())
			rec_end(&d);
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
	rec_end(&d);
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
