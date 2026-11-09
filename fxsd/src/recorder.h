/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2026 perz3us */
#ifndef RECORDER_H
#define RECORDER_H

#include <stdint.h>

enum { REC_SRC_LINE, REC_SRC_SIP };
enum { REC_SIDE_CALLER, REC_SIDE_HOUSE };

int recorder_start(const char *dir, const char *name, int rate, int stereo, int source, long max_bytes);
void recorder_push(int side, const int16_t *s, int n);
void recorder_stop(void);
int recorder_active(void);
int recorder_hit_limit(void);
int recorder_fix_all(const char *dir);

#endif
