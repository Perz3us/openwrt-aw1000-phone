/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2026 perz3us */
#ifndef WAV_H
#define WAV_H

#include <stdint.h>

#define WAV_HEADER	44

void wav_header(uint8_t h[WAV_HEADER], int rate, int channels, uint32_t data_bytes);
int wav_fix(const char *path);
int16_t wav_mix(int16_t a, int16_t b);

#endif
