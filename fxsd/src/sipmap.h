/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2026 perz3us */
#ifndef SIPMAP_H
#define SIPMAP_H

#include <stdio.h>

#define SIPMAP_NAME_MAX 32
#define SIPMAP_PAIRS 24

enum sipmap_kind { SIPMAP_METHOD, SIPMAP_ARG, SIPMAP_FIELD, SIPMAP_STATE, SIPMAP_OWNER, SIPMAP_KINDS };

struct sipmap_pair {
	enum sipmap_kind kind;
	char ours[SIPMAP_NAME_MAX];
	char theirs[SIPMAP_NAME_MAX];
};

struct sipmap {
	char object[SIPMAP_NAME_MAX];
	char event[SIPMAP_NAME_MAX];
	int audio;
	struct sipmap_pair pair[SIPMAP_PAIRS];
	int n;
};

void sipmap_defaults(struct sipmap *m);
int sipmap_load(struct sipmap *m, FILE *f);
const char *sipmap_out(const struct sipmap *m, enum sipmap_kind k, const char *ours);
const char *sipmap_in(const struct sipmap *m, enum sipmap_kind k, const char *theirs);

#endif
