/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2026 perz3us */
#ifndef SI_LINE_H
#define SI_LINE_H

#include "si3226x.h"

#define SI_IRQ1_RING_T1	0x10
#define SI_IRQ2_RTP	0x01
#define SI_IRQ2_LCR	0x02
#define SI_IRQ2_DTMF	0x10
#define SI_IRQ3_HVIC	0x01
#define SI_IRQ3_THERM	0x02

struct si_imp {
	uint32_t txeq[4];
	uint32_t rxeq[4];
	uint32_t ec[12];
	uint32_t zs[5];
	uint8_t ra;
	uint32_t txgain;
	uint32_t rxgain;
	uint32_t hpf[3];
	int8_t txgain_db;
	int8_t rxgain_db;
};

struct si_osc {
	uint32_t freq, amp, phas;
	uint8_t talo, tahi, tilo, tihi;
};

struct si_tone {
	struct si_osc osc[2];
	uint8_t omode;
};

extern const struct si_imp aw1000_imp_600;
extern const struct si_tone aw1000_dialtone;
extern const struct si_tone aw1000_busytone;
extern const struct si_tone aw1000_ringbacktone;
extern const struct si_tone aw1000_waitingtone;

int si_zsynth_setup(struct slic *s, uint8_t ch, const struct si_imp *m);
int si_tone_setup(struct slic *s, uint8_t ch, const struct si_tone *t);
int si_tone_start(struct slic *s, uint8_t ch, int timers);
int si_tone_stop(struct slic *s, uint8_t ch);
int si_irq_enable(struct slic *s, uint8_t ch, const struct si_gen_cfg *c);
int si_irq_read(struct slic *s, uint8_t ch, uint8_t irq[4]);
int si_dtmf_digit(struct slic *s, uint8_t ch, char *d);
int si_pcm_setup(struct slic *s, uint8_t ch, unsigned tx_count, unsigned rx_count);
int si_pcm_wideband(struct slic *s, uint8_t ch);
int si_pcm_stop(struct slic *s, uint8_t ch);

#endif
