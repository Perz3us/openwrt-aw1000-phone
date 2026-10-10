/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2026 perz3us */
#ifndef SLIC_H
#define SLIC_H

#include <stdint.h>

struct slic {
	int (*rd)(void *ctx, uint8_t ch, uint8_t reg, uint8_t *v);
	int (*wr)(void *ctx, uint8_t ch, uint8_t reg, uint8_t v);
	int (*rdram)(void *ctx, uint8_t ch, uint16_t addr, uint32_t *v);
	int (*wrram)(void *ctx, uint8_t ch, uint16_t addr, uint32_t v);
	void (*delay)(void *ctx, unsigned ms);
	void *ctx;
};

#endif
