// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2026 perz3us
#include <errno.h>
#include "si3226x.h"
#include "si3226x_map.h"

#define WR(r, v) do { int _e = s->wr(s->ctx, ch, (r), (v)); if (_e) return _e; } while (0)
#define WRRAM(a, v) do { int _e = s->wrram(s->ctx, ch, (a), (v)); if (_e) return _e; } while (0)

int si_set_user_mode(struct slic *s, uint8_t ch, int on)
{
	static const uint8_t seq[] = { 2, 8, 0xe, 0 };
	uint8_t v;
	unsigned i;
	int e;

	if ((e = s->rd(s->ctx, ch, SI_USERMODE, &v)))
		return e;
	if (!!(v & 1) == !!on)
		return 0;
	for (i = 0; i < sizeof(seq); i++)
		WR(SI_USERMODE, seq[i]);
	if ((e = s->rd(s->ctx, ch, SI_USERMODE, &v)))
		return e;
	return (!!(v & 1) == !!on) ? 0 : -EIO;
}

static int load_jmp(struct slic *s, uint8_t ch, const uint16_t *e)
{
	unsigned i;

	for (i = 0; i < SI_JMP_LOW; i++) {
		WR(SI_JMP0LO + 2 * i, e ? e[i] & 0xff : 0);
		WR(SI_JMP0LO + 2 * i + 1, e ? e[i] >> 8 : 0);
	}
	for (i = 0; i < SI_PATCH_ENTRIES - SI_JMP_LOW; i++)
		WRRAM(RAM_JMP8 + i, e ? e[SI_JMP_LOW + i] & 0x1fff : 0);
	return 0;
}

int si_load_patch(struct slic *s, uint8_t ch, const struct si_patch *p, int second_chan)
{
	uint32_t i;
	int e;

	if ((e = si_set_user_mode(s, ch, 1)))
		return e;
	WR(SI_JMPEN, 0);
	if (!second_chan) {
		if ((e = load_jmp(s, ch, NULL)))
			return e;
		WRRAM(RAM_PRAM_ADDR, 0);
		for (i = 0; i < p->n_data && p->data[i]; i++)
			WRRAM(RAM_PRAM_DATA, p->data[i] << 9);
		WR(SI_RAM_ADDR_HI, 0);
		if ((e = load_jmp(s, ch, p->entries)))
			return e;
		WRRAM(RAM_PATCHID, p->serial);
	}
	for (i = 0; i < p->n_ps; i++)
		WRRAM(p->ps_addr[i], p->ps_data[i]);
	return 0;
}

static int check_patch(struct slic *s, uint8_t ch, const struct si_patch *p)
{
	uint32_t i, v;
	uint8_t lo, hi;
	int e;

	WRRAM(RAM_PRAM_ADDR, 0);
	for (i = 0; i < p->n_data && p->data[i]; i++) {
		if ((e = s->rdram(s->ctx, ch, RAM_PRAM_DATA, &v)))
			return e;
		if (v != p->data[i] << 9)
			return -EIO;
	}
	WR(SI_RAM_ADDR_HI, 0);
	for (i = 0; i < SI_JMP_LOW; i++) {
		if ((e = s->rd(s->ctx, ch, SI_JMP0LO + 2 * i, &lo)) ||
		    (e = s->rd(s->ctx, ch, SI_JMP0LO + 2 * i + 1, &hi)))
			return e;
		if (lo != (p->entries[i] & 0xff) || hi != (p->entries[i] >> 8))
			return -EIO;
	}
	for (i = 0; i < SI_PATCH_ENTRIES - SI_JMP_LOW; i++) {
		if ((e = s->rdram(s->ctx, ch, RAM_JMP8 + i, &v)))
			return e;
		if ((v & 0x1fff) != (p->entries[SI_JMP_LOW + i] & 0x1fff))
			return -EIO;
	}
	for (i = 0; i < p->n_ps; i++) {
		if ((e = s->rdram(s->ctx, ch, p->ps_addr[i], &v)))
			return e;
		if (v != p->ps_data[i])
			return -EIO;
	}
	return 0;
}

int si_verify_patch(struct slic *s, uint8_t ch, const struct si_patch *p)
{
	int e;

	WR(SI_JMPEN, 0);
	e = check_patch(s, ch, p);
	if (e) {
		s->wrram(s->ctx, ch, RAM_PATCHID, 0);
		return e;
	}
	WR(SI_JMPEN, 1);
	return 0;
}

int si_load_general(struct slic *s, uint8_t ch, const struct si_gen_cfg *c)
{
	const struct { uint16_t a; uint32_t v; } ram[] = {
		{ RAM_BAT_HYST, c->bat_hyst },
		{ RAM_VBATR_EXPECT, c->vbatr_expect },
		{ RAM_VBATH_EXPECT, c->vbath_expect },
		{ RAM_PWRSAVE_TIMER, c->pwrsave_timer },
		{ RAM_OFFHOOK_THRESH, c->offhook_thresh },
		{ RAM_VBAT_TRACK_MIN, c->vbat_track_min },
		{ RAM_VBAT_TRACK_MIN_RNG, c->vbat_track_min_rng },
		{ RAM_THERM_DBI, c->therm_dbi },
		{ RAM_DCDC_VERR, c->dcdc_verr },
		{ RAM_DCDC_VERR_HYST, c->dcdc_verr_hyst },
		{ RAM_DCDC_OITHRESH_LO, c->dcdc_oithresh_lo },
		{ RAM_DCDC_OITHRESH_HI, c->dcdc_oithresh_hi },
		{ RAM_PD_UVLO, c->pd_uvlo },
		{ RAM_PD_OVLO, c->pd_ovlo },
		{ RAM_PD_OCLO, c->pd_oclo },
		{ RAM_DCDC_UVHYST, c->dcdc_uvhyst },
		{ RAM_DCDC_UVTHRESH, c->dcdc_uvthresh },
		{ RAM_DCDC_OVTHRESH, c->dcdc_ovthresh },
		{ RAM_DCDC_UVPOL, c->dcdc_uvpol },
		{ RAM_DCDC_VREF_CTRL, c->dcdc_vref_ctrl },
		{ RAM_DCDC_RNGTYPE, c->dcdc_rngtype },
		{ RAM_DCDC_ANA_GAIN, c->dcdc_ana_gain },
		{ RAM_DCDC_ANA_TOFF, c->dcdc_ana_toff },
		{ RAM_DCDC_ANA_TONMIN, c->dcdc_ana_tonmin },
		{ RAM_DCDC_ANA_TONMAX, c->dcdc_ana_tonmax },
		{ RAM_DCDC_ANA_DSHIFT, c->dcdc_ana_dshift },
		{ RAM_DCDC_ANA_LPOLY, c->dcdc_ana_lpoly },
		{ RAM_COEF_P_HVIC, c->coef_p_hvic },
		{ RAM_P_TH_HVIC, c->p_th_hvic },
		{ RAM_SCALE_KAUDIO, c->scale_kaudio },
		{ RAM_LKG_OFHK_OFFSET, c->lkg_ofhk_offset },
		{ RAM_LKG_LB_OFFSET, c->lkg_lb_offset },
		{ RAM_VBATH_DELTA, c->vbath_delta },
		{ RAM_UVTHRESH_MAX, c->uvthresh_max },
		{ RAM_UVTHRESH_SCALE, c->uvthresh_scale },
		{ RAM_UVTHRESH_BIAS, c->uvthresh_bias },
	};
	static const struct { uint16_t a; uint32_t v; } fixed[] = {
		{ RAM_TXACHPF_A1_1, 0x71EB851 },
		{ RAM_ROW0_C2, 0x723F235 },
		{ RAM_ROW1_C2, 0x57A9804 },
		{ RAM_XTALK_TIMER, 0x36000 },
		{ RAM_DCDC_CPUMP_LP_MASK, 0x1100000 },
		{ RAM_VOV_DCDC_SLOPE, 0xFFFFFF },
		{ RAM_VOV_DCDC_OS, 0xA18937 },
		{ RAM_VOV_RING_BAT_MAX, 0xE49BA5 },
		{ RAM_VDIFFLPF, 0x10038D },
		{ RAM_ILOOPLPF, 0x4EDDB9 },
		{ RAM_ILONGLPF, 0x806D6 },
		{ RAM_VCMLPF, 0x10059F },
		{ RAM_CM_SPEEDUP_TIMER, 0xF0000 },
		{ RAM_VCM_TH, 0x106240 },
	};
	static const struct { uint16_t a; uint32_t v; } track[] = {
		{ RAM_DCDC_UV_DEBOUNCE, 0x200000 },
		{ RAM_DCDC_OV_DEBOUNCE, 0 },
		{ RAM_DCDC_OIMASK, 0xC00000 },
		{ RAM_VCM_HYST, 0x206280 },
		{ RAM_DCDC_DCFF_ENABLE, 0 },
		{ RAM_LPR_SCALE, 0x1F00000 },
		{ RAM_LPR_CM_OS, 0x51EB80 },
	};
	uint32_t v;
	uint8_t g;
	unsigned i;
	int e;

	if (c->device_key != SI_DEVICE_KEY || c->bom_opt != SI_BO_LCUB ||
	    c->bat_type != SI_BAT_TRACKING)
		return -EINVAL;
	if ((e = si_set_user_mode(s, ch, 1)))
		return e;
	WR(SI_ENHANCE, 0);
	WR(SI_AUTO, 0x2f);
	for (i = 0; i < sizeof(ram) / sizeof(ram[0]); i++)
		WRRAM(ram[i].a, ram[i].v);
	if ((e = s->rd(s->ctx, ch, SI_GPIO_CFG1, &g)))
		return e;
	WR(SI_GPIO_CFG1, (g & 0xf9) | 0x60);
	WR(SI_PDN, 0x80);
	for (i = 0; i < sizeof(fixed) / sizeof(fixed[0]); i++)
		WRRAM(fixed[i].a, fixed[i].v);
	if ((e = s->rdram(s->ctx, ch, RAM_PWRSAVE_CTRL_LO, &v)))
		return e;
	WRRAM(RAM_PWRSAVE_CTRL_LO, v & 0x0BFFFFFF);
	for (i = 0; i < sizeof(track) / sizeof(track[0]); i++)
		WRRAM(track[i].a, track[i].v);
	WRRAM(RAM_DCDC_SWDRV_POL, c->dcdc_swdrv_pol);
	WRRAM(RAM_IRING_LIM, SI_IRING_LIM_MAX);
	return 0;
}

static int32_t sext(uint32_t v)
{
	return (int32_t)((v & 0x10000000) ? (v | 0xF0000000) : (v & 0x1FFFFFFF));
}

int32_t si_vbat_mv(uint32_t raw)
{
	return sext(raw) / SI_MADC_MV_DIV;
}

int32_t si_iloop_ua(uint32_t raw)
{
	return sext(raw) / SI_MADC_UA_DIV;
}

int si_calibrate(struct slic *s, uint8_t ch, const uint8_t cal[4], unsigned timeout_ms)
{
	unsigned i, t = 0;
	uint8_t v;
	int e;

	for (i = 0; i < 4; i++)
		WR(SI_CALR0 + i, cal[i]);
	for (;;) {
		s->delay(s->ctx, 10);
		t += 10;
		if ((e = s->rd(s->ctx, ch, SI_CALR3, &v)))
			return e;
		if (!(v & 0x80))
			return 0;
		if (t >= timeout_ms)
			return -ETIMEDOUT;
	}
}

static int read_vbat(struct slic *s, uint8_t ch, int32_t *raw)
{
	uint32_t v;
	int e = s->rdram(s->ctx, ch, RAM_MADC_VBAT, &v);

	if (!e)
		*raw = sext(v);
	return e;
}

static int pwr_fail(struct slic *s, uint8_t ch, struct si_pwr_report *r, int err)
{
	s->wrram(s->ctx, ch, RAM_PD_DCDC, DCDC_SHUTDOWN);
	r->result = err;
	return err;
}

int si_power_up(struct slic *s, uint8_t ch, const struct si_gen_cfg *c, struct si_pwr_report *r)
{
	int32_t vbat, vbath;
	uint32_t ram;
	unsigned i;
	uint8_t enh;
	int e;

	r->vbat_50ms_mv = r->vbat_final_mv = 0;
	r->ms = 0;
	r->result = 0;
	WRRAM(RAM_DCDC_OITHRESH, c->dcdc_oithresh_lo);
	WR(SI_LINEFEED, 0);
	if ((e = s->rd(s->ctx, ch, SI_ENHANCE, &enh)))
		return e;
	WR(SI_ENHANCE, enh & 7);
	WRRAM(RAM_PD_DCDC, DCDC_CLEAR_LOCKOUT);
	WRRAM(RAM_DCDC_CPUMP, 0x100000);
	s->delay(s->ctx, 10);
	WRRAM(RAM_PD_DCDC, DCDC_START_DIGITAL);
	s->delay(s->ctx, 50);
	r->ms = 60;
	if ((e = s->rdram(s->ctx, ch, RAM_VBATH_EXPECT, &ram)))
		return pwr_fail(s, ch, r, e);
	vbath = (int32_t)ram;
	if ((e = read_vbat(s, ch, &vbat)))
		return pwr_fail(s, ch, r, e);
	r->vbat_50ms_mv = r->vbat_final_mv = vbat / SI_MADC_MV_DIV;
	if (vbat < vbath / 2)
		return pwr_fail(s, ch, r, -EIO);
	WRRAM(RAM_DCDC_STATUS, 0);
	WRRAM(RAM_PD_DCDC, DCDC_RUN_ANALOG);
	WR(SI_ENHANCE, enh);
	s->delay(s->ctx, 50);
	r->ms += 50;
	for (i = 0; i < 200; i++) {
		if ((e = read_vbat(s, ch, &vbat)))
			return pwr_fail(s, ch, r, e);
		r->vbat_final_mv = vbat / SI_MADC_MV_DIV;
		if (vbat >= vbath - SI_COMP_5V)
			return 0;
		s->delay(s->ctx, 10);
		r->ms += 10;
	}
	return pwr_fail(s, ch, r, -ETIMEDOUT);
}

int si_power_down(struct slic *s, uint8_t ch)
{
	WR(SI_LINEFEED, 0);
	WRRAM(RAM_PD_DCDC, DCDC_SHUTDOWN);
	WRRAM(RAM_DCDC_CPUMP, 0);
	return 0;
}

static const uint16_t dcfeed_addr[SI_DCFEED_WORDS] = {
	634, 635, 636, 638, 639, 640, 641, 642, 643, 644, 645, 853,
	852, 701, 858, 859, 702, 854, 855, 856, 857, 748, 752, 751,
};

int si_dcfeed_setup(struct slic *s, uint8_t ch, const struct si_dcfeed *d)
{
	unsigned i;
	uint8_t lf;
	int e;

	if ((e = s->rd(s->ctx, ch, SI_LINEFEED, &lf)))
		return e;
	WR(SI_LINEFEED, SI_LF_OPEN);
	for (i = 0; i < SI_DCFEED_WORDS; i++)
		WRRAM(dcfeed_addr[i], d->w[i]);
	WR(SI_LINEFEED, lf);
	return 0;
}

int si_linefeed(struct slic *s, uint8_t ch, uint8_t lf)
{
	WR(SI_LINEFEED, lf);
	return 0;
}

int si_ring_setup(struct slic *s, uint8_t ch, const struct si_ring *r, const struct si_gen_cfg *c)
{
	WRRAM(755, r->rtper);
	WRRAM(844, r->freq);
	WRRAM(845, r->amp);
	WRRAM(846, r->phas);
	WRRAM(843, r->offset);
	WRRAM(637, r->slope_ring);
	WRRAM(RAM_IRING_LIM, r->iring_lim > SI_IRING_LIM_MAX ? SI_IRING_LIM_MAX : r->iring_lim);
	WRRAM(848, r->rtacth);
	WRRAM(847, r->rtdcth);
	WRRAM(850, r->rtacdb);
	WRRAM(849, r->rtdcdb);
	WRRAM(753, r->vov_ring_bat);
	WRRAM(896, r->vov_ring_gnd);
	WRRAM(RAM_VBATR_EXPECT, r->vbatr_expect > c->vbatr_expect ? c->vbatr_expect : r->vbatr_expect);
	WR(39, r->talo);
	WR(40, r->tahi);
	WR(41, r->tilo);
	WR(42, r->tihi);
	WRRAM(RAM_VBAT_TRACK_MIN_RNG, r->vbat_track_min_rng);
	WR(38, r->ringcon);
	WR(66, r->userstat);
	WRRAM(749, r->vcm_ring);
	WRRAM(482, r->vcm_ring_fixed);
	WRRAM(483, r->delta_vcm);
	WRRAM(RAM_DCDC_RNGTYPE, r->dcdc_rngtype);
	return 0;
}
