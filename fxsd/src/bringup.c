// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2026 perz3us
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "bringup.h"
#include "si3226x_map.h"

#define RAM_VERIFY_IO 449

static const uint8_t cal_madc[4] = { 0, 0, 1, 0x80 };
static const uint8_t cal_std[4] = { 0, 0xc0, 0x18, 0x80 };

int si_check_chip(struct slic *s, uint8_t ch)
{
	uint32_t v;
	uint8_t r;
	int e;

	if ((e = s->rd(s->ctx, ch, SI_ID, &r)))
		return e;
	if (r != SI3226X_ID)
		return -ENODEV;
	if ((e = s->wr(s->ctx, ch, SI_MSTRSTAT, 0xff)) ||
	    (e = s->rd(s->ctx, ch, SI_MSTRSTAT, &r)))
		return e;
	if (r != 0x1f)
		return -ENXIO;
	if ((e = s->wrram(s->ctx, ch, RAM_VERIFY_IO, 0x12345678)) ||
	    (e = s->rdram(s->ctx, ch, RAM_VERIFY_IO, &v)))
		return e;
	return v == 0x12345678 ? 0 : -EIO;
}

static void all_down(struct slic *s)
{
	si_power_down(s, 0);
	si_power_down(s, 1);
}

int si_bringup(struct slic *s, const struct si_gen_cfg *c, const struct si_patch *p,
	       unsigned power_mask, struct si_pwr_report rep[2])
{
	uint8_t ch;
	int e;

	memset(rep, 0, 2 * sizeof(*rep));
	if (c->device_key != SI_DEVICE_KEY)
		return -EINVAL;
	for (ch = 0; ch < 2; ch++)
		if ((e = si_check_chip(s, ch)))
			return e;
	if ((e = si_load_patch(s, 0, p, 0)) || (e = si_verify_patch(s, 0, p)) ||
	    (e = si_load_patch(s, 1, p, 1)) || (e = s->wr(s->ctx, 1, SI_JMPEN, 1)))
		return e;
	for (ch = 0; ch < 2; ch++)
		if ((e = si_load_general(s, ch, c)))
			return e;
	for (ch = 0; ch < 2; ch++)
		if ((e = si_calibrate(s, ch, cal_madc, 1000)))
			return e;
	if (!power_mask)
		return 0;
	for (ch = 0; ch < 2; ch++) {
		if (!(power_mask & (1u << ch)))
			continue;
		if ((e = si_power_up(s, ch, c, &rep[ch]))) {
			rep[ch].result = e;
			power_mask &= ~(1u << ch);
		}
	}
	if (!power_mask)
		return rep[0].result ? rep[0].result : rep[1].result;
	for (ch = 0; ch < 2; ch++) {
		if (!(power_mask & (1u << ch)))
			continue;
		if ((e = si_calibrate(s, ch, cal_std, 3000)) ||
		    (e = s->wr(s->ctx, ch, SI_ENHANCE, c->enhance)) ||
		    (e = s->wr(s->ctx, ch, SI_AUTO, c->auto_reg)) ||
		    (e = s->wr(s->ctx, ch, SI_ZCAL_EN, c->zcal_en))) {
			all_down(s);
			return e;
		}
	}
	return 0;
}

int qdai_is_slave(const char *resp)
{
	const char *q = strstr(resp, "+QDAI:");
	char *end;
	long mode;

	if (!q)
		return -1;
	end = strpbrk(q, ",\n");
	if (!end || *end != ',')
		return -1;
	q = end + 1;
	mode = strtol(q, &end, 10);
	if (end == q)
		return -1;
	return mode == 1;
}

int qdai_rate(const char *resp)
{
	const char *q = strstr(resp, "+QDAI:");
	char *end;
	long rate;
	int i;

	if (!q)
		return -1;
	for (i = 0; i < 5; i++) {
		end = strpbrk(q, ",\n");
		if (!end || *end != ',')
			return -1;
		q = end + 1;
	}
	rate = strtol(q, &end, 10);
	if (end == q || rate < 0 || rate > 1)
		return -1;
	return rate;
}
