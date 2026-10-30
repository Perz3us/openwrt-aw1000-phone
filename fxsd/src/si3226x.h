/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2026 perz3us */
#ifndef SI3226X_H
#define SI3226X_H

#include <stdint.h>
#include "slic.h"
#include "patch.h"

#define SI_BAT_TRACKING	1
#define SI_BO_LCUB	13

struct si_gen_cfg {
	uint8_t device_key;
	uint8_t bom_opt;
	uint8_t bat_type;
	uint32_t bat_hyst;
	uint32_t vbatr_expect;
	uint32_t vbath_expect;
	uint32_t pwrsave_timer;
	uint32_t offhook_thresh;
	uint32_t vbat_track_min;
	uint32_t vbat_track_min_rng;
	uint32_t pwrsave_dbi;
	uint32_t dcdc_ana_scale;
	uint32_t vov_bat_pwrsave_min;
	uint32_t vov_bat_pwrsave_min_rng;
	uint32_t therm_dbi;
	uint32_t cpump_dbi;
	uint32_t dcdc_verr;
	uint32_t dcdc_verr_hyst;
	uint32_t dcdc_oithresh_lo;
	uint32_t dcdc_oithresh_hi;
	uint32_t pd_uvlo;
	uint32_t pd_ovlo;
	uint32_t pd_oclo;
	uint32_t pd_swdrv;
	uint32_t dcdc_uvhyst;
	uint32_t dcdc_uvthresh;
	uint32_t dcdc_ovthresh;
	uint32_t dcdc_oithresh;
	uint32_t dcdc_swdrv_pol;
	uint32_t dcdc_uvpol;
	uint32_t dcdc_vref_man;
	uint32_t dcdc_vref_ctrl;
	uint32_t dcdc_rngtype;
	uint32_t dcdc_ana_vref;
	uint32_t dcdc_ana_gain;
	uint32_t dcdc_ana_toff;
	uint32_t dcdc_ana_tonmin;
	uint32_t dcdc_ana_tonmax;
	uint32_t dcdc_ana_dshift;
	uint32_t dcdc_ana_lpoly;
	uint32_t dcdc_aux_invert;
	uint32_t dcdc_cpump_lp;
	uint32_t dcdc_cpump_pulldown;
	uint32_t dcdc_lift_en;
	uint32_t coef_p_hvic;
	uint32_t p_th_hvic;
	uint8_t vdc_range;
	uint8_t auto_reg;
	uint8_t irqen[4];
	uint8_t enhance;
	uint32_t scale_kaudio;
	uint8_t zcal_en;
	uint32_t lkg_ofhk_offset;
	uint32_t lkg_lb_offset;
	uint32_t vbath_delta;
	uint32_t uvthresh_max;
	uint32_t uvthresh_scale;
	uint32_t uvthresh_bias;
};

extern const struct si_gen_cfg aw1000_gen_cfg;

int si_set_user_mode(struct slic *s, uint8_t ch, int on);
int si_load_patch(struct slic *s, uint8_t ch, const struct si_patch *p, int second_chan);
int si_verify_patch(struct slic *s, uint8_t ch, const struct si_patch *p);
int si_load_general(struct slic *s, uint8_t ch, const struct si_gen_cfg *c);

struct si_pwr_report {
	int32_t vbat_50ms_mv;
	int32_t vbat_final_mv;
	unsigned ms;
	int result;
};

int si_calibrate(struct slic *s, uint8_t ch, const uint8_t cal[4], unsigned timeout_ms);
int si_power_up(struct slic *s, uint8_t ch, const struct si_gen_cfg *c, struct si_pwr_report *r);
int si_power_down(struct slic *s, uint8_t ch);
int32_t si_vbat_mv(uint32_t raw);
int32_t si_iloop_ua(uint32_t raw);

#define SI_DCFEED_WORDS 24
#define SI_LF_OPEN 0
#define SI_LF_FWD 1
#define SI_LF_FWD_OHT 2
#define SI_LF_RING 4
#define SI_LF_REV_OHT 6

struct si_ring {
	uint32_t rtper, freq, amp, phas, offset, slope_ring, iring_lim;
	uint32_t rtacth, rtdcth, rtacdb, rtdcdb, vov_ring_bat, vov_ring_gnd, vbatr_expect;
	uint8_t talo, tahi, tilo, tihi;
	uint32_t vbat_track_min_rng;
	uint8_t ringcon, userstat;
	uint32_t vcm_ring, vcm_ring_fixed, delta_vcm, dcdc_rngtype;
};

extern const struct si_ring aw1000_ring;

int si_ring_setup(struct slic *s, uint8_t ch, const struct si_ring *r, const struct si_gen_cfg *c);

struct si_dcfeed {
	uint32_t w[SI_DCFEED_WORDS];
};

extern const struct si_dcfeed aw1000_dcfeed;

int si_dcfeed_setup(struct slic *s, uint8_t ch, const struct si_dcfeed *d);
int si_linefeed(struct slic *s, uint8_t ch, uint8_t lf);

#endif
