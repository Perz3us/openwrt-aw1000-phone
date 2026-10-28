/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2026 perz3us */
#ifndef PCMSOCK_H
#define PCMSOCK_H

#include <stdint.h>

#define PCMSOCK_FRAME 160

struct pcm_down {
	int16_t held;
	int16_t prev;
	int have;
};

struct pcm_up {
	int16_t last;
};

int pcm_downsample(struct pcm_down *d, const int16_t *in, int n, int16_t *out);
int pcm_upsample(struct pcm_up *u, const int16_t *in, int n, int16_t *out);

int pcmsock_start(const char *path, const char *dev, int wideband);
void pcmsock_live(int on);
void pcmsock_stop(void);
void pcmsock_stats(uint32_t st[4]);

#endif
