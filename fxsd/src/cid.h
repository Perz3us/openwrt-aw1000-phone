/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2026 perz3us */
#ifndef CID_H
#define CID_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include "slic.h"

enum cid_mode { CID_OFF, CID_BELLCORE, CID_ETSI, CID_DTMF };

#define CID_MSG_MAX 64

int cid_mode_parse(const char *s);
int cid_mdmf(const char *number, const struct tm *t, uint8_t *out, size_t max);
int cid_send(struct slic *s, uint8_t ch, int mode, const char *number, const struct tm *t);

#endif
