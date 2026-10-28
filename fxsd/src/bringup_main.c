// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2026 perz3us
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "bringup.h"
#include "si_line.h"
#include "fxsdev.h"
#include "../../kernel/aw1000-fxs/fxs_uapi.h"
#include "si3226x_map.h"

#define QDAI_CMD "ubus call at-daemon sendat '{\"at_port\":\"/dev/ttyUSB2\",\"at_cmd\":\"AT+QDAI?\",\"timeout\":3}' 2>/dev/null"

static volatile sig_atomic_t stop;

static void on_sig(int sig)
{
	(void)sig;
	stop = 1;
}

static int check_qdai(void)
{
	char buf[1024];
	size_t n;
	FILE *p = popen(QDAI_CMD, "r");

	if (!p)
		return -1;
	n = fread(buf, 1, sizeof(buf) - 1, p);
	pclose(p);
	buf[n] = 0;
	return qdai_is_slave(buf);
}

static void report(struct slic *s, uint8_t ch, const struct si_pwr_report *r)
{
	uint32_t vbat = 0, vtip = 0, vring = 0, iloop = 0, pd = 0, st = 0;
	uint8_t lcr = 0, lf = 0;

	s->rdram(s->ctx, ch, RAM_MADC_VBAT, &vbat);
	s->rdram(s->ctx, ch, RAM_MADC_VTIPC, &vtip);
	s->rdram(s->ctx, ch, RAM_MADC_VRINGC, &vring);
	s->rdram(s->ctx, ch, RAM_MADC_ILOOP, &iloop);
	s->rdram(s->ctx, ch, RAM_PD_DCDC, &pd);
	s->rdram(s->ctx, ch, RAM_DCDC_STATUS, &st);
	s->rd(s->ctx, ch, SI_LCRRTP, &lcr);
	s->rd(s->ctx, ch, SI_LINEFEED, &lf);
	printf("ch%u vbat %d mV vtip %d mV vring %d mV iloop %d uA", ch,
	       si_vbat_mv(vbat), si_vbat_mv(vtip), si_vbat_mv(vring),
	       si_iloop_ua(iloop));
	printf(" vtr %d mV lf 0x%02x %s pd_dcdc 0x%x dcdc_status 0x%x",
	       si_vbat_mv(vtip) - si_vbat_mv(vring), lf, (lcr & 2) ? "OFFHOOK" : "onhook", pd, st);
	if (r)
		printf(" (50ms %d mV, up %d mV in %u ms, %s)", r->vbat_50ms_mv, r->vbat_final_mv,
		       r->ms, r->result ? strerror(-r->result) : "ok");
	putchar('\n');
}

static void dial_loop(struct slic *s, unsigned mask, int secs)
{
	char digits[64];
	unsigned n = 0;
	int t, c, ms = secs * 1000;

	for (t = 0; t < ms && !stop; t += 200) {
		if (fxsdev_wait(s, 200) <= 0)
			continue;
		for (c = 0; c < 2; c++) {
			uint8_t irq[4], lcr;
			char d;

			if (!(mask & (1u << c)) || si_irq_read(s, c, irq))
				continue;
			if ((irq[1] & SI_IRQ2_LCR) && !s->rd(s->ctx, c, SI_LCRRTP, &lcr)) {
				if (lcr & 2) {
					n = 0;
					si_tone_start(s, c, 0);
					printf("ch%d OFFHOOK, dial tone on\n", c);
				} else {
					si_tone_stop(s, c);
					digits[n] = 0;
					printf("ch%d ONHOOK, dialed \"%s\"\n", c, digits);
				}
			}
			if ((irq[1] & SI_IRQ2_DTMF) && !si_dtmf_digit(s, c, &d)) {
				if (!n)
					si_tone_stop(s, c);
				if (n + 1 < sizeof(digits))
					digits[n++] = d;
				printf("ch%d DTMF %c\n", c, d);
			}
			if (irq[2] & (SI_IRQ3_HVIC | SI_IRQ3_THERM))
				printf("ch%d power alarm irq3 0x%02x\n", c, irq[2]);
			fflush(stdout);
		}
		fxsdev_ack(s);
	}
}

static int capture(struct slic *s, unsigned bitmap)
{
	uint16_t w[FXS_CAP_MAX];
	int slot[16], k = 0, i, j, e;
	uint32_t n;

	for (i = 0; i < 16; i++)
		if (bitmap & (1u << i))
			slot[k++] = i;
	if ((e = fxsdev_capture(s, bitmap, 15, w, &n))) {
		fprintf(stderr, "capture failed: %s\n", strerror(-e));
		return e;
	}
	printf("capture %u words, %d slots\n", n, k);
	for (j = 0; j < k; j++) {
		int lo = 0xffff, hi = 0, idle = 0, cnt = 0;

		for (i = j; i < (int)n; i += k) {
			int v = w[i];

			idle += v == 0xffff;
			lo = v < lo ? v : lo;
			hi = v > hi ? v : hi;
			cnt++;
		}
		printf("slot %2d: %3d samples, %3d idle, min 0x%04x max 0x%04x first", slot[j], cnt, idle, lo, hi);
		for (i = j; i < (int)n && i < j + 6 * k; i += k)
			printf(" %04x", w[i]);
		putchar('\n');
	}
	return 0;
}

static void txtest(struct slic *s, uint8_t ch)
{
	static const uint16_t pat[] = { 0x1234, 0xa5a5, 0x5a5a, 0x7ffe };
	unsigned ph, p, i;

	if (getenv("NOLB") == NULL)
		s->wr(s->ctx, ch, 43, 1);
	for (ph = 0; ph < 4; ph++) {
		printf("tx phase %u:", ph);
		for (p = 0; p < sizeof(pat) / sizeof(pat[0]); p++) {
			unsigned ok = 0, bad = 0, n;
			uint32_t v, last = 0;

			fxsdev_pcm_wr(s, 0x0c, 0);
			fxsdev_pcm_wr(s, 0x04, 0x21000002 | (ph << 2));
			fxsdev_pcm_wr(s, 0x00, 7);
			for (i = 0; i < 400; i++) {
				fxsdev_pcm_rd(s, 0x28, &v);
				if (!((v >> 9) & 0x1ff))
					break;
				fxsdev_pcm_rd(s, 0x30, &v);
			}
			for (i = 0; i < 100; i++) {
				fxsdev_pcm_wr(s, 0x34, 0xffff);
				fxsdev_pcm_wr(s, 0x34, 0x1ffff);
				fxsdev_pcm_wr(s, 0x34, 0x20000 | pat[p]);
			}
			fxsdev_pcm_wr(s, 0x0c, 1);
			usleep(11000);
			for (n = 0; n < 300; n++) {
				fxsdev_pcm_rd(s, 0x30, &v);
				if (((v >> 16) & 0x1f) != 2)
					continue;
				if (n < 30)
					continue;
				if ((v & 0xffff) == pat[p])
					ok++;
				else
					bad++, last = v & 0xffff;
			}
			printf("  %04x ok %u bad %u%s", pat[p], ok, bad, bad ? "" : "");
			if (bad)
				printf(" (e.g. %04x)", last);
		}
		putchar('\n');
	}
	fxsdev_pcm_wr(s, 0x0c, 0);
	fxsdev_pcm_wr(s, 0x04, 0x01000002);
	fxsdev_pcm_wr(s, 0x00, 1);
	fxsdev_pcm_wr(s, 0x0c, 1);
	s->wr(s->ctx, ch, 43, 0);
}

static void user_loop(int au, int ms)
{
	uint32_t buf[1024];
	struct pollfd p = { .fd = au, .events = POLLIN };
	ssize_t n;
	int i;

	for (; ms > 0 && !stop; ms -= 20) {
		if (poll(&p, 1, 20) <= 0)
			continue;
		n = read(au, buf, sizeof(buf));
		if (n <= 0)
			continue;
		for (i = 0; i < n / 4; i++)
			buf[i] = (buf[i] >> 16) | (buf[i] << 16);
		if (write(au, buf, n) != n)
			fprintf(stderr, "audio write: %s\n", strerror(errno));
	}
}

static int usage(void)
{
	fprintf(stderr, "usage: fxs-bringup --patch FILE [--power [--linefeed fwd [--ring SECONDS | --dialtone] [--pcm TXCOUNT,RXCOUNT]]]\n"
		"       [--wb] [--capture BITMAP] [--bridge AIN,AOUT,BIN,BOUT [--user]] [--chan 0|1] [--hold SECONDS]\n");
	return 2;
}

int main(int argc, char **argv)
{
	const char *patch_file = NULL;
	struct si_pwr_report rep[2];
	struct si_patch p;
	struct slic s;
	int power = 0, hold = 0, fwd = 0, ring = 0, dial = 0, i, e, q;
	int pcm_tx = -1, pcm_rx = -1, br_a = -1, br_b = -1, br_c = -1, br_d = -1;
	unsigned mask = 3, cap = 0;
	int txt = 0, wb = 0, user = 0, au = -1;
	struct si_imp imp = aw1000_imp_600;
	int gtx = 0, grx = 0;

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--patch") && i + 1 < argc)
			patch_file = argv[++i];
		else if (!strcmp(argv[i], "--power"))
			power = 1;
		else if (!strcmp(argv[i], "--linefeed") && i + 1 < argc && !strcmp(argv[i + 1], "fwd"))
			fwd = 1, i++;
		else if (!strcmp(argv[i], "--chan") && i + 1 < argc)
			mask = 1u << (atoi(argv[++i]) & 1);
		else if (!strcmp(argv[i], "--ring") && i + 1 < argc)
			ring = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--dialtone"))
			dial = 1;
		else if (!strcmp(argv[i], "--pcm") && i + 1 < argc)
			sscanf(argv[++i], "%d,%d", &pcm_tx, &pcm_rx);
		else if (!strcmp(argv[i], "--gain") && i + 1 < argc)
			sscanf(argv[++i], "%d,%d", &gtx, &grx);
		else if (!strcmp(argv[i], "--wb"))
			wb = 1;
		else if (!strcmp(argv[i], "--user"))
			user = 1;
		else if (!strcmp(argv[i], "--txtest"))
			txt = 1;
		else if (!strcmp(argv[i], "--capture") && i + 1 < argc)
			cap = strtoul(argv[++i], NULL, 0);
		else if (!strcmp(argv[i], "--bridge") && i + 1 < argc)
			sscanf(argv[++i], "%d,%d,%d,%d", &br_a, &br_b, &br_c, &br_d);
		else if (!strcmp(argv[i], "--hold") && i + 1 < argc)
			hold = atoi(argv[++i]);
		else
			return usage();
	}
	if (!patch_file || hold < 0 || (fwd && !power) || ring < 0 || (ring && !fwd) || (dial && (!fwd || ring)))
		return usage();
	imp.txgain_db = gtx;
	imp.rxgain_db = grx;
	if ((pcm_tx >= 0 && (!fwd || pcm_rx < 0)) || (br_a >= 0 && br_d < 0) || cap > 0xffff ||
	    (user && br_a < 0))
		return usage();
	if ((e = si_patch_load_file(patch_file, &p))) {
		fprintf(stderr, "patch %s: %s\n", patch_file, strerror(-e));
		return 1;
	}
	printf("patch serial 0x%08x, %u words\n", p.serial, p.n_data);
	q = check_qdai();
	if (q != 1) {
		fprintf(stderr, q ? "modem PCM mode unknown, refusing\n" : "modem is PCM master, refusing\n");
		si_patch_free(&p);
		return 1;
	}
	if ((e = fxsdev_open(&s, "/dev/fxs0"))) {
		fprintf(stderr, "/dev/fxs0: %s\n", strerror(-e));
		si_patch_free(&p);
		return 1;
	}
	signal(SIGINT, on_sig);
	signal(SIGTERM, on_sig);
	if ((e = fxsdev_pcm(&s, 1)) || (e = fxsdev_reset(&s, 1)))
		goto out;
	s.delay(s.ctx, 50);
	if ((e = fxsdev_reset(&s, 0)))
		goto out;
	s.delay(s.ctx, 100);
	e = si_bringup(&s, &aw1000_gen_cfg, &p, power ? mask : 0, rep);
	if (e) {
		fprintf(stderr, "bringup failed: %s\n", strerror(-e));
		report(&s, 0, power ? &rep[0] : NULL);
		report(&s, 1, power ? &rep[1] : NULL);
		goto out;
	}
	printf("patch verified, calibration done%s\n", power ? ", converters up" : "");
	for (i = 0; fwd && i < 2; i++) {
		if (!(mask & (1u << i)) || rep[i].result)
			continue;
		if ((e = si_dcfeed_setup(&s, i, &aw1000_dcfeed)) || (e = si_linefeed(&s, i, SI_LF_FWD))) {
			fprintf(stderr, "linefeed ch%d failed: %s\n", i, strerror(-e));
			goto out;
		}
		printf("ch%d linefeed forward active\n", i);
		if (dial && ((e = si_zsynth_setup(&s, i, &imp)) ||
			     (e = si_tone_setup(&s, i, &aw1000_dialtone)) ||
			     (e = si_irq_enable(&s, i, &aw1000_gen_cfg)) || (e = fxsdev_ack(&s)))) {
			fprintf(stderr, "audio setup ch%d failed: %s\n", i, strerror(-e));
			goto out;
		}
		if (pcm_tx >= 0) {
			if ((!dial && (e = si_zsynth_setup(&s, i, &imp))) ||
			    (e = si_pcm_setup(&s, i, pcm_tx, pcm_rx)) ||
			    (wb && (e = si_pcm_wideband(&s, i)))) {
				fprintf(stderr, "pcm setup ch%d failed: %s\n", i, strerror(-e));
				goto out;
			}
			printf("ch%d pcm 16-bit linear%s tx slot %d rx slot %d\n", i, wb ? " wideband" : "",
			       pcm_tx, pcm_rx);
		}
		if (ring) {
			if ((e = si_ring_setup(&s, i, &aw1000_ring, &aw1000_gen_cfg)) ||
			    (e = fxsdev_ring_arm(&s, 3000)) || (e = si_linefeed(&s, i, SI_LF_RING))) {
				fprintf(stderr, "ring ch%d failed: %s\n", i, strerror(-e));
				goto out;
			}
			printf("ch%d ringing\n", i);
		}
	}
	for (i = 0; i < 2; i++)
		if (mask & (1u << i))
			report(&s, i, power ? &rep[i] : NULL);
	if (cap && (e = capture(&s, cap)))
		goto out;
	for (i = 0; txt && pcm_tx >= 0 && i < 2; i++)
		if (mask & (1u << i))
			txtest(&s, i);
	if (br_a >= 0) {
		if ((e = fxsdev_bridge(&s, (br_a & 0x3f) | ((br_b & 0x3f) << 8) | ((br_c & 0x3f) << 16) | ((uint32_t)(br_d & 0x3f) << 24) | 0x80000000u | (wb ? 0x40000000u : 0) |
					   (user ? 0x80u : 0)))) {
			fprintf(stderr, "bridge failed: %s\n", strerror(-e));
			goto out;
		}
		printf("bridge modem in %d out %d, phone in %d out %d%s\n", br_a, br_b, br_c, br_d,
		       user ? " through userspace" : "");
		if (user && (au = open("/dev/fxs0audio", O_RDWR | O_CLOEXEC)) < 0) {
			e = -errno;
			fprintf(stderr, "/dev/fxs0audio: %s\n", strerror(errno));
			goto out;
		}
	}
	if (dial) {
		dial_loop(&s, mask, hold);
		hold = 0;
	}
	for (i = 0; i < hold && !stop; i++) {
		int c;

		if (au >= 0)
			user_loop(au, 1000);
		else
			sleep(1);
		if (ring > 0 && i + 1 < ring)
			fxsdev_ring_arm(&s, 3000);
		for (c = 0; c < 2; c++) {
			uint8_t lf;

			if (!(mask & (1u << c)))
				continue;
			if (ring > 0 && !s.rd(s.ctx, c, SI_LINEFEED, &lf)) {
				if ((lf & 0x0f) != SI_LF_RING) {
					fxsdev_ring_arm(&s, 0);
					printf("ch%d ring trip\n", c);
					ring = -1;
				} else if (i + 1 >= ring) {
					si_linefeed(&s, c, SI_LF_FWD);
					fxsdev_ring_arm(&s, 0);
					printf("ch%d ring stopped\n", c);
					ring = -1;
				}
			}
			report(&s, c, NULL);
		}
		if (cap)
			capture(&s, cap);
		if (br_a >= 0) {
			uint32_t st[6];

			if (!fxsdev_stat(&s, st))
				printf("bridge moved %u resync %u ticks %u status 0x%x peak modem %u phone %u\n",
				       st[0], st[1], st[2], st[3], st[4], st[5]);
			if (au >= 0 && !fxsdev_austat(&s, st))
				printf("userspace under %u over %u drop %u waiting %u\n",
				       st[0], st[1], st[2], st[3]);
		}
		fflush(stdout);
	}
out:
	if (au >= 0)
		close(au);
	if (br_a >= 0)
		fxsdev_bridge(&s, 0);
	if (ring)
		fxsdev_ring_arm(&s, 0);
	if (power) {
		si_power_down(&s, 0);
		si_power_down(&s, 1);
	}
	fxsdev_reset(&s, 1);
	fxsdev_pcm(&s, 0);
	fxsdev_close(&s);
	si_patch_free(&p);
	if (e)
		fprintf(stderr, "error: %s\n", strerror(-e));
	return e ? 1 : 0;
}
