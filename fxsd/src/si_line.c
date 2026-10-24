// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2026 perz3us
#include <errno.h>
#include "si_line.h"
#include "si3226x_map.h"

#define WR(r, v) do { int _e = s->wr(s->ctx, ch, (r), (v)); if (_e) return _e; } while (0)
#define WRRAM(a, v) do { int _e = s->wrram(s->ctx, ch, (a), (v)); if (_e) return _e; } while (0)

#define RAM_TXACEQ_C0	540
#define RAM_TXACGAIN	544
#define RAM_RXACGAIN	545
#define RAM_RXACEQ_C0	546
#define RAM_ECFIR_C2	563
#define RAM_ZSYNTH_B0	653
#define RAM_RXACHPF	658
#define RAM_RXACGAIN_SAVE 906
#define SI_RA		45
#define SI_OMODE	48
#define SI_OCON		49
#define SI_O1TALO	50
#define SI_O2TALO	54
#define SI_TONDTMF	60
#define RAM_OSC1FREQ	26

static const struct { int8_t db; int16_t scale; } gain_tab[] = {
	{ -30, 32 }, { -29, 35 }, { -28, 40 }, { -27, 45 }, { -26, 50 }, { -25, 56 },
	{ -24, 63 }, { -23, 71 }, { -22, 79 }, { -21, 89 }, { -20, 100 }, { -19, 112 },
	{ -18, 126 }, { -17, 141 }, { -16, 158 }, { -15, 178 }, { -14, 200 }, { -13, 224 },
	{ -12, 251 }, { -11, 282 }, { -10, 316 }, { -9, 355 }, { -8, 398 }, { -7, 447 },
	{ -6, 501 }, { -5, 562 }, { -4, 631 }, { -3, 708 }, { -2, 794 }, { -1, 891 },
	{ 0, 1000 }, { 1, 1122 }, { 2, 1259 }, { 3, 1413 }, { 4, 1585 }, { 5, 1778 },
	{ 6, 1995 },
};

static int32_t gain_scale(int db)
{
	if (db < -30)
		db = -30;
	if (db > 6)
		db = 6;
	return gain_tab[db + 30].scale;
}

static uint32_t scale_eq(uint32_t v, int32_t scale)
{
	int32_t x = (int32_t)((v & 0x10000000) ? (v | 0xF0000000) : v);

	return (uint32_t)((x / 1000) * scale) & 0x1FFFFFFF;
}

static int load_gain(struct slic *s, uint8_t ch, int tx, uint32_t gain, const uint32_t eq[4], int db)
{
	int32_t pga = 1000, eqs = 1000;
	uint32_t g;
	unsigned i;

	if ((db < 0) == !!tx)
		eqs = gain_scale(db);
	else
		pga = gain_scale(db);
	g = (uint32_t)((gain / 1000) * pga) & 0x1FFFFFFF;
	if (tx) {
		WRRAM(RAM_TXACGAIN, g);
		for (i = 0; i < 4; i++)
			WRRAM(RAM_TXACEQ_C0 + i, scale_eq(eq[i], eqs));
	} else {
		WRRAM(RAM_RXACGAIN_SAVE, g);
		WRRAM(RAM_RXACGAIN, g);
		for (i = 0; i < 4; i++)
			WRRAM(RAM_RXACEQ_C0 + i, scale_eq(eq[i], eqs));
	}
	return 0;
}

int si_zsynth_setup(struct slic *s, uint8_t ch, const struct si_imp *m)
{
	unsigned i, t;
	uint8_t lf, v;
	int e;

	if ((e = s->rd(s->ctx, ch, SI_LINEFEED, &lf)))
		return e;
	WR(SI_LINEFEED, 0);
	for (i = 0; i < 4; i++)
		WRRAM(RAM_TXACEQ_C0 + i, m->txeq[i]);
	for (i = 0; i < 4; i++)
		WRRAM(RAM_RXACEQ_C0 + i, m->rxeq[i]);
	for (i = 0; i < 12; i++)
		WRRAM(RAM_ECFIR_C2 + i, m->ec[i]);
	WRRAM(RAM_ZSYNTH_B0 + 3, m->zs[3]);
	WRRAM(RAM_ZSYNTH_B0 + 4, m->zs[4]);
	WRRAM(RAM_ZSYNTH_B0 + 1, m->zs[1]);
	WRRAM(RAM_ZSYNTH_B0, m->zs[0]);
	WRRAM(RAM_ZSYNTH_B0 + 2, m->zs[2]);
	WR(SI_RA, m->ra);
	WRRAM(RAM_TXACGAIN, m->txgain);
	WRRAM(RAM_RXACGAIN_SAVE, m->rxgain);
	WRRAM(RAM_RXACGAIN, m->rxgain);
	for (i = 0; i < 3; i++)
		WRRAM(RAM_RXACHPF + i, m->hpf[i]);
	if ((e = load_gain(s, ch, 1, m->txgain, m->txeq, m->txgain_db)) ||
	    (e = load_gain(s, ch, 0, m->rxgain, m->rxeq, m->rxgain_db)))
		return e;
	WR(SI_CALR0, 0);
	WR(SI_CALR1, 0x40);
	WR(SI_CALR2, 0);
	WR(SI_CALR3, 0x80);
	for (t = 0; t < 500; t++) {
		if ((e = s->rd(s->ctx, ch, SI_CALR3, &v)))
			return e;
		if (!(v & 0x80))
			break;
		s->delay(s->ctx, 1);
	}
	WR(SI_LINEFEED, lf);
	return t < 500 ? 0 : -ETIMEDOUT;
}

int si_tone_setup(struct slic *s, uint8_t ch, const struct si_tone *t)
{
	unsigned i;

	for (i = 0; i < 2; i++) {
		const struct si_osc *o = &t->osc[i];
		uint8_t r = i ? SI_O2TALO : SI_O1TALO;

		WRRAM(RAM_OSC1FREQ + 3 * i, o->freq);
		WRRAM(RAM_OSC1FREQ + 3 * i + 1, o->amp);
		WRRAM(RAM_OSC1FREQ + 3 * i + 2, o->phas);
		WR(r + 1, o->tahi);
		WR(r, o->talo);
		WR(r + 3, o->tihi);
		WR(r + 2, o->tilo);
	}
	WR(SI_OMODE, t->omode);
	return 0;
}

int si_tone_start(struct slic *s, uint8_t ch, int timers)
{
	uint8_t v;
	int e;

	if ((e = s->rd(s->ctx, ch, SI_OCON, &v)))
		return e;
	WR(SI_OCON, v | 0x11 | (timers ? 0x66 : 0));
	return 0;
}

int si_tone_stop(struct slic *s, uint8_t ch)
{
	uint8_t v;
	int e;

	if ((e = s->rd(s->ctx, ch, SI_OCON, &v)))
		return e;
	WR(SI_OCON, v & ~0x77);
	return 0;
}

int si_irq_enable(struct slic *s, uint8_t ch, const struct si_gen_cfg *c)
{
	uint8_t irq[4];
	unsigned i;
	int e;

	if ((e = si_irq_read(s, ch, irq)))
		return e;
	for (i = 0; i < 4; i++)
		WR(SI_IRQEN1 + i, c->irqen[i]);
	return 0;
}

int si_irq_read(struct slic *s, uint8_t ch, uint8_t irq[4])
{
	unsigned i;
	int e;

	for (i = 0; i < 4; i++)
		if ((e = s->rd(s->ctx, ch, SI_IRQ1 + i, &irq[i])))
			return e;
	return 0;
}

int si_dtmf_digit(struct slic *s, uint8_t ch, char *d)
{
	static const char map[16] = "D1234567890*#ABC";
	uint8_t v;
	int e;

	if ((e = s->rd(s->ctx, ch, SI_TONDTMF, &v)))
		return e;
	*d = map[v & 0xf];
	return 0;
}

static const uint32_t tx_iir[13] = {
	0x3538E80, 0x3538E80, 0x1AA9100, 0x216D100, 0x2505400, 0x216D100, 0x2CB8100,
	0x1D7FA500, 0x2CD9B00, 0x1276D00, 0x2CD9B00, 0x2335300, 0x19D5F700,
};

static const uint32_t rx_iir[13] = {
	0x6A71D00, 0x6A71D00, 0x1AA9100, 0x216D100, 0x2505400, 0x216D100, 0x2CB8100,
	0x1D7FA500, 0x2CD9B00, 0x1276D00, 0x2CD9B00, 0x2335300, 0x19D5F700,
};

#define SI_PCMMODE	11
#define SI_PCMTXLO	12
#define SI_PCMTXHI	13
#define SI_PCMRXLO	14
#define SI_PCMRXHI	15
#define SI_DIGCON	44

int si_pcm_setup(struct slic *s, uint8_t ch, unsigned tx, unsigned rx)
{
	unsigned i;
	uint8_t v;
	int e;

	if (tx > 1023 || rx > 1023)
		return -EINVAL;
	if ((e = s->rd(s->ctx, ch, SI_DIGCON, &v)))
		return e;
	WR(SI_DIGCON, v & ~0x0c);
	for (i = 0; i < 13; i++)
		WRRAM(518 + i, tx_iir[i]);
	for (i = 0; i < 13; i++)
		WRRAM(550 + i, rx_iir[i]);
	if ((e = s->rd(s->ctx, ch, SI_ENHANCE, &v)))
		return e;
	WR(SI_ENHANCE, v & ~1);
	WR(SI_PCMMODE, 0x03);
	WR(SI_PCMTXLO, tx & 0xff);
	WR(SI_PCMTXHI, 0x10 | ((tx >> 8) & 3));
	WR(SI_PCMRXLO, rx & 0xff);
	if ((e = s->rd(s->ctx, ch, SI_PCMRXHI, &v)))
		return e;
	WR(SI_PCMRXHI, (v & 0xfc) | ((rx >> 8) & 3));
	WR(SI_PCMMODE, 0x13);
	return 0;
}

static const uint32_t rx_hpf_wb[9] = {
	0x7CFF900, 0x18300700, 0x79FF201, 0x7CEDA1D, 0x106320D4, 0x7CEDA1D, 0xF9A910F,
	0x185FFDA8, 0x08000000,
};

static const uint32_t tx_hpf_wb[9] = {
	0x0C7FF4CE, 0x13800B32, 0x079FF201, 0x030FDD10, 0x19E0996C, 0x030FDD10, 0x0F9A910F,
	0x185FFDA8, 0x0CD30000,
};

int si_pcm_wideband(struct slic *s, uint8_t ch)
{
	unsigned i;
	uint8_t v;
	int e;

	for (i = 0; i < 9; i++)
		WRRAM(658 + i, rx_hpf_wb[i]);
	for (i = 0; i < 9; i++)
		WRRAM(531 + i, tx_hpf_wb[i]);
	if ((e = s->rd(s->ctx, ch, SI_ENHANCE, &v)))
		return e;
	WR(SI_ENHANCE, v | 1);
	return 0;
}

int si_pcm_stop(struct slic *s, uint8_t ch)
{
	uint8_t v;
	int e;

	if ((e = s->rd(s->ctx, ch, SI_PCMMODE, &v)))
		return e;
	WR(SI_PCMMODE, v & ~0x10);
	return 0;
}
