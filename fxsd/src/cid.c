// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2026 perz3us
#include <errno.h>
#include <string.h>
#include "cid.h"
#include "si3226x_map.h"

#define WR(r, v) do { int _e = s->wr(s->ctx, ch, (r), (v)); if (_e) return _e; } while (0)
#define WRRAM(a, v) do { int _e = s->wrram(s->ctx, ch, (a), (v)); if (_e) return _e; } while (0)

#define SI_OMODE	48
#define SI_OCON		49
#define SI_O1TALO	50
#define SI_O1TAHI	51
#define SI_O1TILO	52
#define SI_O1TIHI	53
#define SI_FSKDAT	58
#define SI_FSKDEPTH	59
#define RAM_OSC1FREQ	26
#define RAM_FSKFREQ0	834
#define RAM_FSKAMP0	836
#define RAM_FSK01	838

#define IRQ1_FSKBUF	0x40
#define OMODE_STARTSTOP	0x80

struct fsk_cfg {
	uint32_t freq[2], amp[2], tr[2];
	unsigned seizure, mark;
};

static const struct fsk_cfg bell202 = {
	{ 0x06B60000, 0x079C0000 }, { 0x0015C000, 0x000BA000 }, { 0x02232000, 0x077C2000 },
	30, 19,
};

static const struct fsk_cfg v23 = {
	{ 0x06D20000, 0x078B0000 }, { 0x0014C000, 0x000CA000 }, { 0x026E4000, 0x0694C000 },
	38, 23,
};

static const uint32_t dtmf_freq[8] = {
	0x06D50000, 0x06950000, 0x06460000, 0x05EA0000,
	0x04A80000, 0x03FC0000, 0x03320000, 0x02460000,
};

static const uint32_t dtmf_amp[8] = {
	0x00321000, 0x0037A000, 0x003E1000, 0x00451000,
	0x005BB000, 0x00674000, 0x0074D000, 0x00853000,
};

int cid_mode_parse(const char *s)
{
	if (!strcmp(s, "bellcore"))
		return CID_BELLCORE;
	if (!strcmp(s, "etsi"))
		return CID_ETSI;
	if (!strcmp(s, "dtmf"))
		return CID_DTMF;
	return CID_OFF;
}

static int put2(uint8_t *p, int v)
{
	p[0] = '0' + v / 10 % 10;
	p[1] = '0' + v % 10;
	return 2;
}

int cid_mdmf(const char *number, const struct tm *t, uint8_t *out, size_t max)
{
	size_t n = strlen(number), len = 0, i;
	uint8_t sum = 0, *p = out + 2;

	if (n > 20)
		n = 20;
	if (max < 2 + 10 + 2 + 20 + 1)
		return -ENOSPC;
	*p++ = 0x01;
	*p++ = 8;
	p += put2(p, t->tm_mon + 1);
	p += put2(p, t->tm_mday);
	p += put2(p, t->tm_hour);
	p += put2(p, t->tm_min);
	if (n) {
		*p++ = 0x02;
		*p++ = n;
		memcpy(p, number, n);
		p += n;
	} else {
		*p++ = 0x04;
		*p++ = 1;
		*p++ = 'P';
	}
	len = p - out - 2;
	out[0] = 0x80;
	out[1] = len;
	for (i = 0; i < len + 2; i++)
		sum += out[i];
	*p++ = -sum;
	return p - out;
}

static int fsk_byte(struct slic *s, uint8_t ch, uint8_t b)
{
	uint8_t irq;
	int i, e;

	WR(SI_FSKDAT, b);
	for (i = 0; i < 40; i++) {
		if ((e = s->rd(s->ctx, ch, SI_IRQ1, &irq)))
			return e;
		if (irq & IRQ1_FSKBUF)
			return 0;
		s->delay(s->ctx, 1);
	}
	return -ETIMEDOUT;
}

static int fsk_send(struct slic *s, uint8_t ch, const struct fsk_cfg *c, const uint8_t *msg, int n)
{
	uint8_t omode, irq;
	unsigned i;
	int e;

	WR(SI_FSKDEPTH, 0x08);
	WR(SI_O1TAHI, 0);
	WR(SI_O1TIHI, 0);
	WR(SI_O1TILO, 0);
	WR(SI_O1TALO, 0x13);
	WR(SI_FSKDEPTH, 0);
	for (i = 0; i < 2; i++) {
		WRRAM(RAM_FSK01 + i, c->tr[i]);
		WRRAM(RAM_FSKAMP0 + i, c->amp[i]);
		WRRAM(RAM_FSKFREQ0 + i, c->freq[i]);
	}
	if ((e = s->rd(s->ctx, ch, SI_OMODE, &omode)))
		return e;
	omode = (omode & ~OMODE_STARTSTOP) | 0x0A;
	WR(SI_OCON, 0);
	s->rd(s->ctx, ch, SI_IRQ1, &irq);
	WR(SI_OMODE, omode);
	WR(SI_OCON, 0x05);

	for (i = 0; i < c->seizure; i++)
		if ((e = fsk_byte(s, ch, 0x55)))
			goto out;
	for (i = 0; i < c->mark; i++)
		if ((e = fsk_byte(s, ch, 0xFF)))
			goto out;
	if ((e = fsk_byte(s, ch, 0xFF)))
		goto out;
	WR(SI_OMODE, omode | OMODE_STARTSTOP);
	for (i = 0; i < (unsigned)n; i++)
		if ((e = fsk_byte(s, ch, msg[i])))
			goto out;
	if (!(e = fsk_byte(s, ch, 0xFF)))
		s->delay(s->ctx, 20);
out:
	s->wr(s->ctx, ch, SI_OCON, 0);
	s->wr(s->ctx, ch, SI_OMODE, omode & ~(OMODE_STARTSTOP | 0x08));
	return e;
}

static int dtmf_digit(struct slic *s, uint8_t ch, char d)
{
	static const char keys[] = "123A456B789C*0#D";
	const char *k = strchr(keys, d);
	unsigned lo, hi, i;

	if (!k || !d)
		return 0;
	i = k - keys;
	lo = i / 4;
	hi = 4 + i % 4;
	WR(SI_OCON, 0);
	WRRAM(RAM_OSC1FREQ, dtmf_freq[lo]);
	WRRAM(RAM_OSC1FREQ + 1, dtmf_amp[lo]);
	WRRAM(RAM_OSC1FREQ + 2, 0);
	WRRAM(RAM_OSC1FREQ + 3, dtmf_freq[hi]);
	WRRAM(RAM_OSC1FREQ + 4, dtmf_amp[hi]);
	WRRAM(RAM_OSC1FREQ + 5, 0);
	WR(SI_OMODE, 0x66);
	WR(SI_OCON, 0x11);
	s->delay(s->ctx, 70);
	WR(SI_OCON, 0);
	s->delay(s->ctx, 70);
	return 0;
}

static int dtmf_send(struct slic *s, uint8_t ch, const char *number)
{
	int e;

	if (!*number)
		return 0;
	if ((e = dtmf_digit(s, ch, 'A')))
		return e;
	for (; *number; number++)
		if ((e = dtmf_digit(s, ch, *number)))
			return e;
	return dtmf_digit(s, ch, 'C');
}

int cid_send(struct slic *s, uint8_t ch, int mode, const char *number, const struct tm *t)
{
	uint8_t msg[CID_MSG_MAX];
	char num[21];
	int n = 0;

	for (; *number && n < 20; number++)
		if (*number >= '0' && *number <= '9')
			num[n++] = *number;
	num[n] = 0;
	number = num;

	switch (mode) {
	case CID_BELLCORE:
	case CID_ETSI:
		if ((n = cid_mdmf(number, t, msg, sizeof(msg))) < 0)
			return n;
		return fsk_send(s, ch, mode == CID_ETSI ? &v23 : &bell202, msg, n);
	case CID_DTMF:
		return dtmf_send(s, ch, number);
	default:
		return 0;
	}
}
