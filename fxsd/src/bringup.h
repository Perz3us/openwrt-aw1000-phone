/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2026 perz3us */
#ifndef BRINGUP_H
#define BRINGUP_H

#include "si3226x.h"

int si_check_chip(struct slic *s, uint8_t ch);
int si_bringup(struct slic *s, const struct si_gen_cfg *c, const struct si_patch *p,
	       unsigned power_mask, struct si_pwr_report rep[2]);
int qdai_is_slave(const char *resp);
int qdai_rate(const char *resp);

#endif
