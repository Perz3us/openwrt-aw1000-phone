/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2026 perz3us */
#ifndef FXSDEV_H
#define FXSDEV_H

#include "slic.h"

int fxsdev_open(struct slic *s, const char *path);
void fxsdev_close(struct slic *s);
int fxsdev_reset(struct slic *s, int assert_reset);
int fxsdev_pcm(struct slic *s, int on);
int fxsdev_ring_arm(struct slic *s, unsigned ms);
int fxsdev_wait(struct slic *s, int timeout_ms);
int fxsdev_ack(struct slic *s);
int fxsdev_capture(struct slic *s, uint32_t bitmap, uint32_t ms, uint16_t *w, uint32_t *n);
int fxsdev_bridge(struct slic *s, uint32_t arg);
int fxsdev_stat(struct slic *s, uint32_t st[6]);
int fxsdev_pcm_rd(struct slic *s, uint32_t off, uint32_t *v);
int fxsdev_pcm_wr(struct slic *s, uint32_t off, uint32_t v);

#endif
