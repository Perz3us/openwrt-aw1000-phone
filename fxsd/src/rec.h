/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2026 perz3us */
#ifndef REC_H
#define REC_H

#include <stddef.h>
#include "clcc.h"

#define REC_FLASH_DIR	"/root/phone-recordings"
#define REC_USB_SUB	"phone-recordings"

enum rec_mode { REC_OFF, REC_ALL, REC_CHOSEN, REC_EVERY };

struct rec_rule {
	char number[CLCC_NUM_MAX];
	int always;
};

int rec_parse_mode(const char *s);
int rec_wanted(int mode, const struct rec_rule *r, int n, const char *number);
long rec_dir_bytes(const char *dir);
int rec_prune(const char *dir, long limit_bytes, long need_bytes, char removed[][64], int max);
int rec_pick_dir(const char *mounts, char *out, size_t n);

#endif
